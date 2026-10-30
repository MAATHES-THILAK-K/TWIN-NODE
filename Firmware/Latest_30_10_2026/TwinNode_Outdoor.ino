#include <Arduino.h>
#include <ESP8266WiFi.h>

extern "C" {
  #include <espnow.h>
  #include <user_interface.h>
}

// --------------------------------------------------
// PIN CONFIGURATION
// --------------------------------------------------
#define MOSFET_PIN 13   // P-channel MOSFET: ACTIVE LOW
#define TRIG_PIN   12
#define ECHO_PIN   16

#define BUZZER_PIN 14   // N-channel MOSFET: ACTIVE HIGH

// --------------------------------------------------
// TANK DISTANCE THRESHOLDS
// Distance is measured from sensor to water surface
// --------------------------------------------------
#define TANK_EMPTY_DISTANCE_CM 25.0f
#define TANK_FULL_DISTANCE_CM   5.0f

// --------------------------------------------------
// ESP-NOW CONFIGURATION
// --------------------------------------------------
#define ESPNOW_CHANNEL 1

uint8_t receiverMacAddress[] = {
  0xD8, 0xBF, 0xC0, 0x05, 0xAF, 0x85
};

// Transmit every 30 seconds
#define SEND_INTERVAL_MS 30000UL
#define SEND_WAIT_MS       300UL

// --------------------------------------------------
// BUZZER BEEP-BEEP TIMING
// ON 150 ms, OFF 150 ms,
// ON 150 ms, OFF 600 ms, repeat
// --------------------------------------------------
#define BEEP_ON_MS       150UL
#define BEEP_GAP_MS      150UL
#define BEEP_PAUSE_MS    600UL

// --------------------------------------------------
// ESP-NOW DATA STRUCTURE
// Must match the receiver exactly
// --------------------------------------------------
typedef struct struct_message {
  float distanceCm;
  int waterPercent;
  int level;
  bool tankFull;
  bool tankEmpty;
} struct_message;

struct_message tankData;

// --------------------------------------------------
// GLOBAL VARIABLES
// --------------------------------------------------
volatile bool sendDone = false;
volatile bool sendSuccess = false;

unsigned long lastSendTime = 0;
bool firstMeasurement = true;

bool emptyTankAlarm = false;
bool alarmInitialized = false;

uint8_t alarmStep = 0;
unsigned long alarmStepStart = 0;

// --------------------------------------------------
// PRINT MAC ADDRESS
// --------------------------------------------------
void printMacAddress(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) Serial.print('0');

    Serial.print(mac[i], HEX);

    if (i < 5) Serial.print(':');
  }
}

// --------------------------------------------------
// ESP-NOW CALLBACK
// --------------------------------------------------
void onDataSent(uint8_t *macAddress, uint8_t status) {
  (void)macAddress;

  sendSuccess = (status == 0);
  sendDone = true;
}

// --------------------------------------------------
// ULTRASONIC DISTANCE
// Uses your verified sensor sequence unchanged
// --------------------------------------------------
float readUltrasonicDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration =
      pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    Serial.println("Out of range / No echo received");
    return -1.0f;
  }

  float distanceCm = (duration * 0.0343f) / 2.0f;

  Serial.print("Distance: ");
  Serial.print(distanceCm, 2);
  Serial.println(" cm");

  return distanceCm;
}

// --------------------------------------------------
// WATER PERCENTAGE
// 25 cm = 0%
//  5 cm = 100%
// --------------------------------------------------
int calculateWaterPercentage(float distanceCm) {
  if (distanceCm < 0) {
    return -1;
  }

  float percent =
      ((TANK_EMPTY_DISTANCE_CM - distanceCm) /
       (TANK_EMPTY_DISTANCE_CM - TANK_FULL_DISTANCE_CM))
       * 100.0f;

  if (percent > 100.0f) percent = 100.0f;
  if (percent < 0.0f) percent = 0.0f;

  return (int)(percent + 0.5f);
}

// --------------------------------------------------
// LEVEL CALCULATION
//
// Level 4: distance <= 5 cm
// Level 3: distance > 5 and <= 12 cm
// Level 2: distance > 12 and <= 20 cm
// Level 1: distance > 20 and <= 25 cm
// Level 0: distance > 25 cm (EMPTY)
// Level 0: invalid measurement
// --------------------------------------------------
int calculateLevel(float distanceCm) {
  if (distanceCm < 0) return 0;

  if (distanceCm <= 5.0f)  return 4;
  if (distanceCm <= 12.0f) return 3;
  if (distanceCm <= 20.0f) return 2;
  if (distanceCm <= 25.0f) return 1;

  return 0;
}

// --------------------------------------------------
// UPDATE EMPTY TANK ALARM STATE
// --------------------------------------------------
void setEmptyTankAlarm(bool enable) {
  if (emptyTankAlarm == enable) {
    return;
  }

  emptyTankAlarm = enable;
  alarmInitialized = false;

  if (!enable) {
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Empty tank alarm OFF");
  } else {
    Serial.println("EMPTY TANK! Beep-beep alarm ON");
  }
}

// --------------------------------------------------
// NON-BLOCKING BEEP-BEEP ALARM
// Keeps running between sensor measurements
// without blocking ESP-NOW or the watchdog.
// --------------------------------------------------
void updateEmptyTankAlarm() {
  if (!emptyTankAlarm) {
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  unsigned long now = millis();

  // Begin a new beep-beep cycle
  if (!alarmInitialized) {
    alarmInitialized = true;
    alarmStep = 0;
    alarmStepStart = now;

    digitalWrite(BUZZER_PIN, HIGH);
    return;
  }

  unsigned long stepDuration;

  switch (alarmStep) {
    case 0:
      stepDuration = BEEP_ON_MS;
      break;

    case 1:
      stepDuration = BEEP_GAP_MS;
      break;

    case 2:
      stepDuration = BEEP_ON_MS;
      break;

    default:
      stepDuration = BEEP_PAUSE_MS;
      break;
  }

  if (now - alarmStepStart >= stepDuration) {
    alarmStep = (alarmStep + 1) % 4;
    alarmStepStart = now;

    // Steps 0 and 2 are ON.
    // Steps 1 and 3 are OFF.
    if (alarmStep == 0 || alarmStep == 2) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

// --------------------------------------------------
// PRINT TANK DATA
// --------------------------------------------------
void printTankData() {
  Serial.println();
  Serial.println("========== TANK DATA ==========");

  Serial.print("Distance: ");

  if (tankData.distanceCm < 0) {
    Serial.println("INVALID");
  } else {
    Serial.print(tankData.distanceCm, 2);
    Serial.println(" cm");
  }

  Serial.print("Water Percentage: ");

  if (tankData.waterPercent < 0) {
    Serial.println("INVALID");
  } else {
    Serial.print(tankData.waterPercent);
    Serial.println(" %");
  }

  Serial.print("Water Level: ");
  Serial.println(tankData.level);

  switch (tankData.level) {
    case 4:
      Serial.println("Status: FULL");
      break;

    case 3:
      Serial.println("Status: HIGH");
      break;

    case 2:
      Serial.println("Status: MEDIUM");
      break;

    case 1:
      Serial.println("Status: LOW");
      break;

    default:
      if (tankData.distanceCm < 0) {
        Serial.println("Status: SENSOR ERROR");
      } else {
        Serial.println("Status: EMPTY");
      }
      break;
  }

  Serial.print("Tank Full: ");
  Serial.println(tankData.tankFull ? "YES" : "NO");

  Serial.print("Tank Empty: ");
  Serial.println(tankData.tankEmpty ? "YES" : "NO");

  Serial.println("================================");
}

// --------------------------------------------------
// MEASURE AND SEND DATA
// --------------------------------------------------
void measureAndSendData() {
  Serial.println();
  Serial.println("Starting ultrasonic measurement");

  // Keep sensor powered continuously
  // P-channel MOSFET gate LOW = ON
  float distance = readUltrasonicDistanceCM();

  int waterPercent = calculateWaterPercentage(distance);
  int level = calculateLevel(distance);

  tankData.distanceCm = distance;
  tankData.waterPercent = waterPercent;
  tankData.level = level;

  // Invalid measurements must not be treated as empty.
  tankData.tankFull =
      (distance >= 0 && level == 4);

  tankData.tankEmpty =
      (distance > TANK_EMPTY_DISTANCE_CM);

  // Empty tank -> repeating beep-beep.
  // Any valid reading at or below 25 cm -> silence.
  // Invalid readings do not activate the empty alarm.
  setEmptyTankAlarm(tankData.tankEmpty);

  printTankData();

  sendDone = false;
  sendSuccess = false;

  uint8_t result = esp_now_send(
      receiverMacAddress,
      (uint8_t *)&tankData,
      sizeof(tankData)
  );

  if (result != 0) {
    Serial.print("ESP-NOW send start failed: ");
    Serial.println(result);
    return;
  }

  Serial.println("ESP-NOW packet submitted");

  unsigned long startWait = millis();

  while (!sendDone &&
         (millis() - startWait < SEND_WAIT_MS)) {
    updateEmptyTankAlarm();
    yield();
  }

  if (!sendDone) {
    Serial.println("Send callback timeout");
  } else if (sendSuccess) {
    Serial.println("ESP-NOW Send Status: SUCCESS");
  } else {
    Serial.println("ESP-NOW Send Status: FAILED");
  }
}

// --------------------------------------------------
// ESP-NOW INITIALIZATION (also called after light sleep)
// --------------------------------------------------
bool initializeEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(1);
  wifi_set_channel(ESPNOW_CHANNEL);

  if (esp_now_init() != 0) {
    Serial.println("ERROR: ESP-NOW initialization failed");
    return false;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onDataSent);

  int peerResult = esp_now_add_peer(
      receiverMacAddress,
      ESP_NOW_ROLE_SLAVE,
      ESPNOW_CHANNEL,
      NULL,
      0
  );

  if (peerResult != 0) {
    Serial.print("ERROR: Failed to add ESP-NOW peer: ");
    Serial.println(peerResult);
    esp_now_deinit();
    return false;
  }

  Serial.println("ESP-NOW ready on channel 1");
  return true;
}

// Timed light-sleep wake callback. Do not do lengthy work here.
void lightSleepWakeupCallback() {
  // Execution resumes in enterTimedLightSleep() after the timer wakes the chip.
}

// --------------------------------------------------
// TIMED LIGHT SLEEP - NO GPIO16/RST CONNECTION NEEDED
// --------------------------------------------------
bool enterTimedLightSleep() {
  Serial.println("Entering timed LIGHT SLEEP for 30 seconds...");
  Serial.flush();

  // Turn off the sensor before sleeping (P-MOSFET active LOW).
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(MOSFET_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  // ESP-NOW cannot operate while the radio is shut down.
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);  // Required for timed/forced light sleep
  delay(1);

  // The ESP8266 Arduino core's timed-light-sleep example stops OS timers
  // before entering forced light sleep.
  extern os_timer_t* timer_list;
  timer_list = nullptr;

  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  wifi_fpm_set_wakeup_cb(lightSleepWakeupCallback);
  wifi_fpm_open();

  int sleepResult = wifi_fpm_do_sleep(SEND_INTERVAL_MS * 1000UL);
  if (sleepResult != 0) {
    Serial.print("Light-sleep request failed: ");
    Serial.println(sleepResult);
    wifi_fpm_close();
    wifi_fpm_do_wakeup();
    return false;
  }

  // Required by the ESP8266 Arduino core to let timed light sleep engage.
  // The wake callback makes this a single timed sleep interval.
  delay(SEND_INTERVAL_MS + 1UL);

  wifi_fpm_close();
  wifi_fpm_do_wakeup();
  Serial.println("Woke from timed light sleep");
  return true;
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("================================");
  Serial.println(" ESP8266 TANK LEVEL TRANSMITTER");
  Serial.println("================================");

  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(TRIG_PIN, LOW);

  // P-channel MOSFET active-LOW:
  // keep ultrasonic sensor powered
  digitalWrite(MOSFET_PIN, LOW);

  // N-channel MOSFET active-HIGH:
  // buzzer initially OFF
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("Ultrasonic sensor power: ON");
  Serial.println("Waiting for sensor stabilization...");

  delay(500);

  Serial.print("Transmitter MAC: ");
  WiFi.mode(WIFI_STA);
  Serial.println(WiFi.macAddress());

  Serial.print("Receiver MAC: ");
  printMacAddress(receiverMacAddress);
  Serial.println();

  Serial.print("Data structure size: ");
  Serial.println(sizeof(tankData));

  while (!initializeEspNow()) {
    Serial.println("Retrying ESP-NOW initialization in 1 second...");
    delay(1000);
  }

  Serial.println("Transmitter ready");

  // First measurement happens immediately.
  // Further measurements occur every 30 seconds.
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------
void loop() {
  updateEmptyTankAlarm();

  if (firstMeasurement ||
      (millis() - lastSendTime >= SEND_INTERVAL_MS)) {

    firstMeasurement = false;
    measureAndSendData();

    if (emptyTankAlarm) {
      // Keep the CPU awake while the empty-tank beep-beep alarm is active.
      // Light sleep would pause the software-controlled buzzer pattern.
      lastSendTime = millis();
      Serial.println("Empty-tank alarm active: staying awake to run buzzer");
    } else {
      // Sleep only when the empty-tank alarm is not needed.
      enterTimedLightSleep();

      // Reinitialize the radio/ESP-NOW after waking from timed light sleep.
      digitalWrite(MOSFET_PIN, LOW);  // P-channel MOSFET ON
      delay(500);                     // Ultrasonic sensor stabilization

      while (!initializeEspNow()) {
        Serial.println("Retrying ESP-NOW after wake in 1 second...");
        delay(1000);
      }

      // The sleep interval has already elapsed; take the next reading now.
      lastSendTime = millis() - SEND_INTERVAL_MS;
    }
  }

  yield();
}
