#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

extern "C" {
  #include <user_interface.h>
}
//ESP12F MAC Address: 24:4C:AB:6C:E4:F8 - LION PCB TRANSMITTER
// --------------------------------------------------
// ESP32 TRANSMITTER MAC ADDRESS
// TX MAC Address: 24:6F:28:3A:B5:68
// --------------------------------------------------
uint8_t transmitterMacAddress[] = {0x24, 0x4C, 0xAB, 0x6C, 0xE4, 0xF8};

// --------------------------------------------------
// PIN CONFIGURATION - ESP12F RECEIVER
// --------------------------------------------------
#define LED_LOW_PIN     12   // GPIO12 - LOW level LED
#define LED_MED_PIN     16   // GPIO16 - MEDIUM level LED
#define LED_HIGH_PIN    5    // GPIO5  - HIGH level LED
#define LED_FULL_PIN    4    // GPIO4  - FULL level LED

#define BUZZER_PIN      14   // GPIO14 - Buzzer

// --------------------------------------------------
// ESP-NOW CHANNEL
// Must match transmitter channel
// --------------------------------------------------
#define ESPNOW_CHANNEL  1

// --------------------------------------------------
// DATA STRUCTURE
// Must match ESP32 transmitter structure
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
// UPDATE 4 LEVEL LED STATUS
// --------------------------------------------------
void updateLevelLEDs(int waterPercent) {
  allLedsOff();

  if (waterPercent < 0) {
    Serial.println("Invalid water percentage. LEDs OFF.");
    return;
  }

  if (waterPercent <= 25) {
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
// BUZZER ONLY DURING FULL LEVEL
// --------------------------------------------------
void updateBuzzer(int waterPercent) {
  if (waterPercent > 80) {
    Serial.println("BUZZER: FULL alert");

    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    digitalWrite(BUZZER_PIN, LOW);
  }
  else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// --------------------------------------------------
// ESP-NOW RECEIVE CALLBACK - ESP8266 FORMAT
// --------------------------------------------------
void onDataReceived(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  Serial.println();
  Serial.println("Data Received!");

  Serial.print("Sender MAC: ");
  printMacAddress(mac);
  Serial.println();

  if (!isFromKnownTransmitter(mac)) {
    Serial.println("Unknown transmitter. Data ignored.");
    return;
  }

  if (len != sizeof(receivedData)) {
    Serial.println("Data size mismatch!");
    Serial.print("Received size: ");
    Serial.println(len);
    Serial.print("Expected size: ");
    Serial.println(sizeof(receivedData));
    return;
  }

  memcpy(&receivedData, incomingData, sizeof(receivedData));

  Serial.println("------ Tank Data ------");

  Serial.print("Distance: ");
  Serial.print(receivedData.distanceCm);
  Serial.println(" cm");

  Serial.print("Water Percentage: ");
  Serial.print(receivedData.waterPercent);
  Serial.println(" %");

  Serial.print("Received Level: ");
  Serial.println(receivedData.level);

  Serial.print("Tank Full: ");
  Serial.println(receivedData.tankFull ? "YES" : "NO");

  Serial.print("Tank Empty: ");
  Serial.println(receivedData.tankEmpty ? "YES" : "NO");

  Serial.println("-----------------------");

  updateLevelLEDs(receivedData.waterPercent);
  updateBuzzer(receivedData.waterPercent);
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP12F Indoor Receiver Node Started");

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

  Serial.print("Receiver ESP12F MAC Address: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Expected Transmitter MAC: ");
  printMacAddress(transmitterMacAddress);
  Serial.println();

  Serial.print("Receiver Struct Size: ");
  Serial.println(sizeof(receivedData));

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW Init Failed!");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onDataReceived);

  Serial.println("ESP-NOW Receiver Ready...");
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------
void loop() {
  // Nothing needed here.
  // Data is handled inside onDataReceived().
}