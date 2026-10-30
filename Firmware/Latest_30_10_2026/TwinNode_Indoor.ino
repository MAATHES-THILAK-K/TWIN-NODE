#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

extern "C" {
  #include <user_interface.h>
}

// --------------------------------------------------
// TRANSMITTER MAC ADDRESS - LION PCB
// --------------------------------------------------
uint8_t transmitterMacAddress[] = {
  0x24, 0x4C, 0xAB, 0x6C, 0xE4, 0xF8
};

// --------------------------------------------------
// PIN CONFIGURATION - ESP12F RECEIVER
// --------------------------------------------------
#define LED_LOW_PIN     12
#define LED_MED_PIN     16
#define LED_HIGH_PIN    5
#define LED_FULL_PIN    4

#define BUZZER_PIN      14

#define ESPNOW_CHANNEL  1

// --------------------------------------------------
// BUZZER TIMING
// FULL: Continuous ON
// EMPTY: 200 ms ON, 1000 ms OFF
// --------------------------------------------------
#define EMPTY_BEEP_ON_MS   200UL
#define EMPTY_BEEP_GAP_MS 1000UL

// --------------------------------------------------
// DATA STRUCTURE
// Must match the transmitter exactly
// --------------------------------------------------
typedef struct struct_message {
  float distanceCm;
  int waterPercent;
  int level;
  bool tankFull;
  bool tankEmpty;
} struct_message;

struct_message receivedData;

// --------------------------------------------------
// RECEIVED TANK STATUS
// --------------------------------------------------
volatile bool tankIsFull = false;
volatile bool tankIsEmpty = false;
volatile bool measurementValid = false;

volatile int receivedWaterPercent = -1;
volatile bool newDataReceived = false;

// Buzzer state
bool buzzerIsOn = false;
unsigned long buzzerChangeTime = 0;

// --------------------------------------------------
// PRINT MAC ADDRESS
// --------------------------------------------------
void printMacAddress(uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
}

// --------------------------------------------------
// CHECK KNOWN TRANSMITTER
// --------------------------------------------------
bool isFromKnownTransmitter(uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != transmitterMacAddress[i]) {
      return false;
    }
  }
  return true;
}

// --------------------------------------------------
// TURN OFF ALL LEDS
// --------------------------------------------------
void allLedsOff() {
  digitalWrite(LED_LOW_PIN, LOW);
  digitalWrite(LED_MED_PIN, LOW);
  digitalWrite(LED_HIGH_PIN, LOW);
  digitalWrite(LED_FULL_PIN, LOW);
}

// --------------------------------------------------
// UPDATE LEDS FROM TANK STATUS
// --------------------------------------------------
void updateLevelLEDs(int waterPercent,
                     bool tankFull,
                     bool tankEmpty,
                     bool valid) {
  allLedsOff();

  if (!valid || waterPercent < 0) {
    Serial.println("Invalid measurement: LEDs OFF");
    return;
  }

  // FULL: Keep the FULL LED ON
  if (tankFull) {
    digitalWrite(LED_FULL_PIN, HIGH);
    Serial.println("LED Status: FULL");
  }
  // EMPTY: Use the LOW LED as the empty indicator
  else if (tankEmpty) {
    digitalWrite(LED_LOW_PIN, HIGH);
    Serial.println("LED Status: EMPTY");
  }
  else if (waterPercent <= 25) {
    digitalWrite(LED_LOW_PIN, HIGH);
    Serial.println("LED Status: LOW");
  }
  else if (waterPercent <= 50) {
    digitalWrite(LED_MED_PIN, HIGH);
    Serial.println("LED Status: MEDIUM");
  }
  else if (waterPercent <= 80) {
    digitalWrite(LED_HIGH_PIN, HIGH);
    Serial.println("LED Status: HIGH");
  }
  else {
    digitalWrite(LED_FULL_PIN, HIGH);
    Serial.println("LED Status: FULL");
  }
}

// --------------------------------------------------
// NON-BLOCKING BUZZER CONTROL
// FULL: Continuous sound
// EMPTY: Short beep, 1-second OFF gap, repeat
// OTHER LEVELS: OFF
// --------------------------------------------------
void updateBuzzer() {
  unsigned long now = millis();

  // Invalid measurement: buzzer OFF
  if (!measurementValid) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerIsOn = false;
    return;
  }

  // FULL TANK: Continuous buzzer
  if (tankIsFull) {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerIsOn = true;
    return;
  }

  // EMPTY TANK: Repeating beep with 1-second gap
  if (tankIsEmpty) {
    if (buzzerIsOn) {
      // Turn buzzer OFF after the short beep
      if (now - buzzerChangeTime >= EMPTY_BEEP_ON_MS) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerIsOn = false;
        buzzerChangeTime = now;
      }
    }
    else {
      // Turn buzzer ON after 1-second OFF gap
      if (now - buzzerChangeTime >= EMPTY_BEEP_GAP_MS) {
        digitalWrite(BUZZER_PIN, HIGH);
        buzzerIsOn = true;
        buzzerChangeTime = now;
      }
    }

    return;
  }

  // LOW / MEDIUM / HIGH: Buzzer OFF
  digitalWrite(BUZZER_PIN, LOW);
  buzzerIsOn = false;
}

// --------------------------------------------------
// ESP-NOW RECEIVE CALLBACK
// --------------------------------------------------
void onDataReceived(uint8_t *mac,
                    uint8_t *incomingData,
                    uint8_t len) {
  if (!isFromKnownTransmitter(mac)) {
    Serial.println("Unknown transmitter. Ignored.");
    return;
  }

  if (len != sizeof(receivedData)) {
    Serial.println("Data size mismatch!");
    return;
  }

  memcpy(&receivedData, incomingData, sizeof(receivedData));

  Serial.println();
  Serial.println("------ TANK DATA ------");

  Serial.print("Distance: ");
  Serial.print(receivedData.distanceCm);
  Serial.println(" cm");

  Serial.print("Water Percentage: ");
  Serial.print(receivedData.waterPercent);
  Serial.println(" %");

  Serial.print("Water Level: ");
  Serial.println(receivedData.level);

  Serial.print("Tank Full: ");
  Serial.println(receivedData.tankFull ? "YES" : "NO");

  Serial.print("Tank Empty: ");
  Serial.println(receivedData.tankEmpty ? "YES" : "NO");

  Serial.println("-----------------------");

  // A negative distance or percentage means invalid data.
  bool valid = (receivedData.distanceCm >= 0 &&
                receivedData.waterPercent >= 0);

  measurementValid = valid;
  receivedWaterPercent = receivedData.waterPercent;

  tankIsFull = valid && receivedData.tankFull;
  tankIsEmpty = valid && receivedData.tankEmpty;

  // Update the LEDs using the latest received status.
  updateLevelLEDs(
    receivedData.waterPercent,
    tankIsFull,
    tankIsEmpty,
    valid
  );

  // Reset the empty beep cycle when status changes.
  if (tankIsEmpty && valid) {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerIsOn = true;
    buzzerChangeTime = millis();
  }
  else if (tankIsFull && valid) {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerIsOn = true;
  }
  else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerIsOn = false;
    buzzerChangeTime = millis();
  }

  newDataReceived = true;
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP12F INDOOR RECEIVER STARTED");

  pinMode(LED_LOW_PIN, OUTPUT);
  pinMode(LED_MED_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_FULL_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  allLedsOff();
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  wifi_set_channel(ESPNOW_CHANNEL);

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Expected Transmitter MAC: ");
  printMacAddress(transmitterMacAddress);
  Serial.println();

  Serial.print("Data Structure Size: ");
  Serial.println(sizeof(receivedData));

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW initialization FAILED!");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onDataReceived);

  Serial.println("ESP-NOW receiver ready!");
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------
void loop() {
  // Non-blocking buzzer control.
  // No delay() is used, so ESP-NOW can keep receiving.
  updateBuzzer();

  yield();
}
