/*
 * ============================================================
 *   💧 SMART WATER TANK MONITORING SYSTEM
 *      INDOOR NODE FIRMWARE — ESP8266 (ESP-NOW Receiver)
 * ============================================================
 *
 *  Author   : MAATHES THILAK K
 *  Version  : 1.0.0
 *  Board    : ESP8266 (NodeMCU / Wemos D1 Mini)
 *  Protocol : ESP-NOW (Connectionless)
 *  Created  : March 22, 2025
 *
 *  Description:
 *    This firmware runs on the INDOOR node of a smart water
 *    tank monitoring system. It:
 *      - Listens for ESP-NOW packets from the outdoor sensor node
 *      - Immediately sends an ACK back to the outdoor node
 *      - Computes water level % from received distance
 *      - Drives 4 LEDs to indicate tank fill level
 *      - Triggers a buzzer on FULL / CRITICAL LOW conditions
 *      - Detects outdoor node disconnection via timeout
 *      - Provides full debug info over Serial (115200 baud)
 *
 *  ⚠️  IMPORTANT BEFORE FLASHING:
 *    - Set OUTDOOR_NODE_MAC to the outdoor node's MAC address
 *    - Adjust TANK_HEIGHT_CM to your actual tank height
 *    - Verify all pin definitions for your PCB layout
 *
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <espnow.h>

// ============================================================
//  ⚙️  CONFIGURABLE PARAMETERS  — Edit these freely
// ============================================================

// --- Tank Physical Configuration ---
#define TANK_HEIGHT_CM        100     // Total empty tank height in cm
                                      // (distance from sensor to tank floor when empty)

// --- Communication Thresholds ---
#define DELTA_THRESHOLD_CM    3       // Minimum distance change (cm) considered significant
                                      // Matching the outdoor node's threshold

// --- Timing Configuration (milliseconds) ---
#define ACK_SEND_DELAY_MS     5       // Small delay before sending ACK (stabilise RF)
#define ACTIVE_CHECK_MS       5000    // Interval to refresh display when no new data arrives
#define DATA_TIMEOUT_MS       600000  // 10 minutes: warn if no packet received within this window

// --- Buzzer Behaviour ---
#define BUZZER_DURATION_MS    300     // Duration of each individual beep (ms)
#define BUZZER_GAP_MS         200     // Gap between beeps (ms)
#define BUZZER_FULL_BEEPS     3       // Number of beeps when tank is FULL
#define BUZZER_LOW_BEEPS      5       // Number of rapid beeps for CRITICAL LOW

// --- Water Level Alert Thresholds (%) ---
#define LEVEL_FULL_PCT        95      // Tank considered FULL above this %
#define LEVEL_CRITICAL_LOW_PCT 10     // Tank considered CRITICALLY LOW below this %

// --- LED Level Bands (%) ---
//   LED1 lights when level ≥ LED1_THRESHOLD
//   LED2 lights when level ≥ LED2_THRESHOLD  … and so on
#define LED1_THRESHOLD        5       // 0–25% band  (show LED1 even at very low level)
#define LED2_THRESHOLD        25      // 25–50% band
#define LED3_THRESHOLD        50      // 50–75% band
#define LED4_THRESHOLD        75      // 75–100% band

// ============================================================
//  📌  PIN DEFINITIONS  — Adjust for your wiring
// ============================================================

#define LED1_PIN      D1    // GPIO 5  — Level 0–25%
#define LED2_PIN      D2    // GPIO 4  — Level 25–50%
#define LED3_PIN      D5    // GPIO 14 — Level 50–75%
#define LED4_PIN      D6    // GPIO 12 — Level 75–100%
#define BUZZER_PIN    D7    // GPIO 13 — Active buzzer

// ============================================================
//  📡  OUTDOOR NODE MAC ADDRESS  — ⚠️ UPDATE THIS!
// ============================================================
//  Run the outdoor node once with Serial Monitor open.
//  It will print its MAC like:  Outdoor MAC: AA:BB:CC:DD:EE:FF
//  Replace the bytes below accordingly.

uint8_t OUTDOOR_NODE_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
//                              ↑ Replace with actual outdoor node MAC

// ============================================================
//  📦  DATA STRUCTURES  (must match outdoor node exactly)
// ============================================================

typedef struct __attribute__((packed)) DataPacket {
  uint16_t distance;   // Measured distance in cm (median-filtered)
  uint8_t  nodeState;  // 0 = LOW_POWER mode, 1 = ACTIVE mode
  uint8_t  battery;    // Battery level % (0 if not implemented)
} DataPacket;

typedef struct __attribute__((packed)) AckPacket {
  uint8_t status;      // 1 = ACK OK
} AckPacket;

// ============================================================
//  🗃️  GLOBAL STATE
// ============================================================

volatile bool     g_newDataFlag    = false;   // Set in ISR-like callback, consumed in loop()
DataPacket        g_latestPacket   = {0, 0, 0};
uint16_t          g_lastDistance   = 0;       // Last displayed/processed distance
unsigned long     g_lastDataMillis = 0;       // Timestamp of last received packet
unsigned long     g_lastRefreshMs  = 0;       // Timestamp of last LED refresh
bool              g_tankWasFull    = false;   // Latched flag — prevents repeat alerts
bool              g_tankWasLow     = false;   // Latched flag — prevents repeat alerts
bool              g_timeoutWarned  = false;   // Has timeout warning fired this cycle?
uint32_t          g_packetCount    = 0;       // Total packets received since boot

// ============================================================
//  🔔  ESP-NOW RECEIVE CALLBACK
//  Called automatically when a packet arrives.
//  Keep it short — just store data + send ACK.
// ============================================================

void ICACHE_RAM_ATTR onDataReceived(uint8_t *senderMAC, uint8_t *rawData, uint8_t len) {

  if (len != sizeof(DataPacket)) {
    Serial.printf("[WARN] Unexpected packet size: %d bytes (expected %d)\n",
                  len, sizeof(DataPacket));
    return;
  }

  // Copy safely into global struct
  memcpy(&g_latestPacket, rawData, sizeof(DataPacket));
  g_newDataFlag    = true;
  g_lastDataMillis = millis();
  g_packetCount++;

  // ── Send ACK immediately ──────────────────────────────────
  delay(ACK_SEND_DELAY_MS);
  AckPacket ack;
  ack.status = 1;
  esp_now_send(senderMAC, (uint8_t *)&ack, sizeof(AckPacket));
  // ─────────────────────────────────────────────────────────

  Serial.printf("[RX #%lu] Dist=%d cm | State=%s | Batt=%d%%\n",
    (unsigned long)g_packetCount,
    g_latestPacket.distance,
    g_latestPacket.nodeState == 1 ? "ACTIVE" : "LOW_POWER",
    g_latestPacket.battery
  );
  Serial.println("[TX] ACK sent to outdoor node");
}

// ============================================================
//  💧  WATER LEVEL CALCULATION
//  Returns 0–100 (percentage fill)
// ============================================================

uint8_t calcWaterLevel(uint16_t distanceCm) {
  if (distanceCm == 0)                    return 100; // Sensor at bottom = full
  if (distanceCm >= TANK_HEIGHT_CM)       return 0;   // Sensor max range = empty

  int32_t filled = (int32_t)TANK_HEIGHT_CM - (int32_t)distanceCm;
  uint8_t level  = (uint8_t)((filled * 100L) / TANK_HEIGHT_CM);
  return (uint8_t)constrain(level, 0, 100);
}

// ============================================================
//  💡  LED CONTROL
// ============================================================

void updateLEDs(uint8_t levelPct) {
  digitalWrite(LED1_PIN, (levelPct >= LED1_THRESHOLD) ? HIGH : LOW);
  digitalWrite(LED2_PIN, (levelPct >= LED2_THRESHOLD) ? HIGH : LOW);
  digitalWrite(LED3_PIN, (levelPct >= LED3_THRESHOLD) ? HIGH : LOW);
  digitalWrite(LED4_PIN, (levelPct >= LED4_THRESHOLD) ? HIGH : LOW);
}

void allLEDsOff() {
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
  digitalWrite(LED4_PIN, LOW);
}

void allLEDsOn() {
  digitalWrite(LED1_PIN, HIGH);
  digitalWrite(LED2_PIN, HIGH);
  digitalWrite(LED3_PIN, HIGH);
  digitalWrite(LED4_PIN, HIGH);
}

// Blink all LEDs together — used for startup animation & warnings
void blinkAllLEDs(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    allLEDsOn();
    delay(onMs);
    allLEDsOff();
    if (i < times - 1) delay(offMs);
  }
}

// Cascade blink (LED1 → LED4 → LED1) — startup flair
void cascadeLEDs(int sweeps, int stepMs) {
  for (int s = 0; s < sweeps; s++) {
    uint8_t pins[4] = { LED1_PIN, LED2_PIN, LED3_PIN, LED4_PIN };
    // Up sweep
    for (int i = 0; i < 4; i++) {
      allLEDsOff();
      digitalWrite(pins[i], HIGH);
      delay(stepMs);
    }
    // Down sweep
    for (int i = 2; i >= 0; i--) {
      allLEDsOff();
      digitalWrite(pins[i], HIGH);
      delay(stepMs);
    }
  }
  allLEDsOff();
}

// ============================================================
//  🔊  BUZZER CONTROL
// ============================================================

void beep(int count, int onMs, int offMs) {
  for (int i = 0; i < count; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(onMs);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < count - 1) delay(offMs);
  }
}

// ============================================================
//  🚨  ALERT LOGIC  (called once per new data packet)
//  Uses latched flags to fire alert ONCE per transition,
//  not on every packet while condition persists.
// ============================================================

void checkAndTriggerAlerts(uint8_t levelPct) {

  // ── FULL TANK ─────────────────────────────────────────────
  if (levelPct >= LEVEL_FULL_PCT) {
    if (!g_tankWasFull) {
      g_tankWasFull = true;
      Serial.println("╔══════════════════════════════════╗");
      Serial.println("║  🟢  ALERT: TANK IS FULL (≥95%)  ║");
      Serial.println("╚══════════════════════════════════╝");
      // Three distinct beeps: tank is full
      beep(BUZZER_FULL_BEEPS, BUZZER_DURATION_MS, BUZZER_GAP_MS);
    }
  } else {
    g_tankWasFull = false;  // Reset latch once level drops
  }

  // ── CRITICAL LOW ─────────────────────────────────────────
  if (levelPct <= LEVEL_CRITICAL_LOW_PCT) {
    if (!g_tankWasLow) {
      g_tankWasLow = true;
      Serial.println("╔═══════════════════════════════════════════╗");
      Serial.println("║  🔴  ALERT: CRITICAL LOW WATER (≤10%)    ║");
      Serial.println("╚═══════════════════════════════════════════╝");
      // Rapid short beeps: urgent
      beep(BUZZER_LOW_BEEPS, 100, 100);
    }
  } else {
    g_tankWasLow = false;
  }
}

// ============================================================
//  📋  SERIAL STATUS DISPLAY
// ============================================================

void printStatus(uint16_t distanceCm, uint8_t levelPct) {
  char levelBar[22];  // 20-char bar + \0
  int  filled = (levelPct * 20) / 100;
  for (int i = 0; i < 20; i++) levelBar[i] = (i < filled) ? '#' : '-';
  levelBar[20] = '\0';

  Serial.println();
  Serial.println("┌─────────────────────────────────┐");
  Serial.printf ("│  Distance  : %4d cm             │\n", distanceCm);
  Serial.printf ("│  Level     : %3d%%               │\n", levelPct);
  Serial.printf ("│  [%s]  │\n", levelBar);
  Serial.printf ("│  LEDs      : %s%s%s%s           │\n",
    (levelPct >= LED1_THRESHOLD) ? "1 " : "_ ",
    (levelPct >= LED2_THRESHOLD) ? "2 " : "_ ",
    (levelPct >= LED3_THRESHOLD) ? "3 " : "_ ",
    (levelPct >= LED4_THRESHOLD) ? "4 " : "_ "
  );
  Serial.printf ("│  Packets   : %-6lu               │\n",
    (unsigned long)g_packetCount);
  Serial.println("└─────────────────────────────────┘");
}

// ============================================================
//  🔧  HELPER: Print local MAC on Serial
// ============================================================

void printMACAddress() {
  Serial.print("  Indoor Node MAC : ");
  Serial.println(WiFi.macAddress());
}

// ============================================================
//  ⚡  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║   💧 WATER TANK MONITOR — INDOOR NODE   ║");
  Serial.println("║   ESP8266  |  ESP-NOW Receiver  v1.0    ║");
  Serial.println("╚══════════════════════════════════════════╝");

  // ── GPIO Init ───────────────────────────────────────────
  pinMode(LED1_PIN,   OUTPUT);
  pinMode(LED2_PIN,   OUTPUT);
  pinMode(LED3_PIN,   OUTPUT);
  pinMode(LED4_PIN,   OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  allLEDsOff();
  digitalWrite(BUZZER_PIN, LOW);

  // ── Startup Sequence ────────────────────────────────────
  cascadeLEDs(2, 80);              // Sweep animation
  beep(1, 150, 0);                 // Single boot beep
  delay(200);

  // ── WiFi (Station mode, disconnected — required for ESP-NOW) ──
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println();
  Serial.println("  [CONFIG]");
  printMACAddress();
  Serial.printf ("  Tank Height     : %d cm\n",  TANK_HEIGHT_CM);
  Serial.printf ("  Delta Threshold : %d cm\n",  DELTA_THRESHOLD_CM);
  Serial.printf ("  ACK Delay       : %d ms\n",  ACK_SEND_DELAY_MS);
  Serial.printf ("  Refresh Interval: %d ms\n",  ACTIVE_CHECK_MS);
  Serial.printf ("  Timeout Warning : %d ms\n",  DATA_TIMEOUT_MS);
  Serial.printf ("  Full alert at   : %d%%\n",   LEVEL_FULL_PCT);
  Serial.printf ("  Low alert at    : %d%%\n",   LEVEL_CRITICAL_LOW_PCT);

  // ── ESP-NOW Init ─────────────────────────────────────────
  if (esp_now_init() != 0) {
    Serial.println("\n  [ERROR] ESP-NOW initialization FAILED!");
    Serial.println("  Entering fault loop — check board & power.");
    while (true) {
      blinkAllLEDs(3, 80, 80);
      delay(600);
    }
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataReceived);

  // Register outdoor node as a peer (required to send ACK back)
  if (esp_now_add_peer(OUTDOOR_NODE_MAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0) != 0) {
    Serial.println("\n  [WARN] Could not register outdoor node peer.");
    Serial.println("  Verify OUTDOOR_NODE_MAC is correct.");
    // Non-fatal — receive still works; ACK will fail
  }

  g_lastDataMillis = millis();
  g_lastRefreshMs  = millis();

  Serial.println("\n  [OK]  ESP-NOW initialised. Listening...");
  Serial.println("══════════════════════════════════════════");

  // Show "waiting" state on LEDs
  allLEDsOff();
  blinkAllLEDs(1, 300, 0);  // Single blink = ready
}

// ============================================================
//  🔁  MAIN LOOP
// ============================================================

void loop() {
  unsigned long now = millis();

  // ── 1. Process incoming data ──────────────────────────────
  if (g_newDataFlag) {
    g_newDataFlag = false;
    g_timeoutWarned = false;     // Connection resumed — reset timeout flag

    uint16_t dist  = g_latestPacket.distance;
    uint8_t  level = calcWaterLevel(dist);

    updateLEDs(level);
    checkAndTriggerAlerts(level);
    printStatus(dist, level);

    g_lastDistance  = dist;
    g_lastRefreshMs = now;
  }

  // ── 2. Periodic LED refresh ───────────────────────────────
  //    (Keeps LEDs correct even without new data)
  if (now - g_lastRefreshMs >= ACTIVE_CHECK_MS) {
    g_lastRefreshMs = now;
    if (g_lastDistance > 0) {
      uint8_t level = calcWaterLevel(g_lastDistance);
      updateLEDs(level);
      Serial.printf("[Refresh] Level: %3d%% | Distance: %d cm\n",
                    level, g_lastDistance);
    }
  }

  // ── 3. Outdoor node timeout detection ─────────────────────
  //    If no packet for DATA_TIMEOUT_MS → warn once per cycle
  if ((now - g_lastDataMillis >= DATA_TIMEOUT_MS) && !g_timeoutWarned) {
    g_timeoutWarned = true;
    unsigned long minsSince = (now - g_lastDataMillis) / 60000UL;

    Serial.println();
    Serial.println("╔══════════════════════════════════════════╗");
    Serial.printf ("║  ⚠️  NO DATA for %3lu min — node offline?  ║\n",
                    minsSince);
    Serial.println("╚══════════════════════════════════════════╝");

    // Visual warning: slow blink all LEDs once
    blinkAllLEDs(2, 400, 300);

    // Restore level display after blink
    if (g_lastDistance > 0) {
      updateLEDs(calcWaterLevel(g_lastDistance));
    }
  }

  // ── 4. Yield to system (ESP8266 watchdog) ─────────────────
  yield();
}

// ============================================================
//  END OF FILE
// ============================================================
