#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

extern "C" {
  #include <user_interface.h>
}

// --------------------------------------------------
// PIN CONFIGURATION
// --------------------------------------------------
#define MOSFET_PIN 13   // P-Channel MOSFET Gate, ACTIVE LOW
#define TRIG_PIN   12
#define ECHO_PIN   16

// --------------------------------------------------
// TANK CONFIGURATION
// Change based on your tank
// --------------------------------------------------
#define TANK_EMPTY_DISTANCE_CM  120.0
#define TANK_FULL_DISTANCE_CM   20.0

// --------------------------------------------------
// SEND INTERVAL
// --------------------------------------------------
#define SEND_INTERVAL_MS 30000UL   // Send data every 30 seconds

// --------------------------------------------------
// ESP-NOW CHANNEL
// Must match receiver
// --------------------------------------------------
#define ESPNOW_CHANNEL 1

// --------------------------------------------------
// RECEIVER MAC ADDRESS
// Receiver ESP12F MAC Address: D8:BF:C0:05:AF:85
// --------------------------------------------------
uint8_t receiverMacAddress[] = {0xD8, 0xBF, 0xC0, 0x05, 0xAF, 0x85};

// --------------------------------------------------
// DATA STRUCTURE
// Must match receiver structure exactly
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
long duration;
float distance_cm;
unsigned long lastSendTime = 0;

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
// ESP-NOW SEND CALLBACK - ESP8266 FORMAT
// --------------------------------------------------
void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("ESP-NOW Send Status: ");

  if (sendStatus == 0) {
    Serial.println("Success");
  } else {
    Serial.println("Failed");
  }
}

// --------------------------------------------------
// ULTRASONIC DATA COLLECTION
// Based on your working test code
// --------------------------------------------------
float readUltrasonicDistanceCM() {
  // Trigger ultrasonic
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  // Read echo
  duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    Serial.println("Out of range");
    return -1;
  }

  distance_cm = (duration * 0.0343) / 2.0;

  Serial.print("Distance: ");
  Serial.print(distance_cm);
  Serial.println(" cm");

  return distance_cm;
}

// --------------------------------------------------
// WATER PERCENTAGE CALCULATION
// --------------------------------------------------
int calculateWaterPercentage(float distanceCm) {
  if (distanceCm < 0) {
    return -1;
  }

  float percent = ((TANK_EMPTY_DISTANCE_CM - distanceCm) /
                  (TANK_EMPTY_DISTANCE_CM - TANK_FULL_DISTANCE_CM)) * 100.0;

  if (percent > 100) percent = 100;
  if (percent < 0) percent = 0;

  return (int)percent;
}

// --------------------------------------------------
// 4 LEVEL CALCULATION
// 1 = LOW
// 2 = MEDIUM
// 3 = HIGH
// 4 = FULL
// --------------------------------------------------
int calculateLevel(int percent) {
  if (percent < 0) return 0;

  if (percent <= 25) return 1;       // LOW
  else if (percent <= 50) return 2;  // MEDIUM
  else if (percent <= 80) return 3;  // HIGH
  else return 4;                     // FULL
}

// --------------------------------------------------
// MEASURE AND SEND DATA
// --------------------------------------------------
void measureAndSendData() {
  Serial.println();
  Serial.println("Starting ultrasonic measurement...");

  // Sensor is already ON permanently
  float distance = readUltrasonicDistanceCM();

  int waterPercent = calculateWaterPercentage(distance);
  int level = calculateLevel(waterPercent);

  tankData.distanceCm = distance;
  tankData.waterPercent = waterPercent;
  tankData.level = level;
  tankData.tankFull = (level == 4);
  tankData.tankEmpty = (level == 1);

  Serial.println("------ Tank Data ------");

  Serial.print("Distance: ");
  Serial.print(tankData.distanceCm);
  Serial.println(" cm");

  Serial.print("Water Percentage: ");
  Serial.print(tankData.waterPercent);
  Serial.println(" %");

  Serial.print("Water Level: ");
  Serial.println(tankData.level);

  Serial.print("Tank Full: ");
  Serial.println(tankData.tankFull ? "YES" : "NO");

  Serial.print("Tank Low: ");
  Serial.println(tankData.tankEmpty ? "YES" : "NO");

  Serial.println("-----------------------");

  uint8_t sendResult = esp_now_send(receiverMacAddress,
                                    (uint8_t *)&tankData,
                                    sizeof(tankData));

  if (sendResult == 0) {
    Serial.println("Data send command success");
  } else {
    Serial.println("Data send command failed");
  }
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP12F Ultrasonic ESP-NOW Transmitter Started");

  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // --------------------------------------------------
  // KEEP ULTRASONIC ALWAYS ON
  // P-Channel MOSFET Gate is ACTIVE LOW
  // --------------------------------------------------
  digitalWrite(MOSFET_PIN, LOW);   // Sensor ON permanently

  digitalWrite(TRIG_PIN, LOW);

  Serial.println("Ultrasonic Sensor Power: ALWAYS ON");

  // --------------------------------------------------
  // ESP-NOW SETUP
  // --------------------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  wifi_set_channel(ESPNOW_CHANNEL);

  Serial.print("This ESP12F TX MAC Address: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Receiver MAC Address: ");
  printMacAddress(receiverMacAddress);
  Serial.println();

  Serial.print("Transmitter Struct Size: ");
  Serial.println(sizeof(tankData));

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);

  esp_now_register_send_cb(onDataSent);

  int addPeerResult = esp_now_add_peer(receiverMacAddress,
                                       ESP_NOW_ROLE_SLAVE,
                                       ESPNOW_CHANNEL,
                                       NULL,
                                       0);

  if (addPeerResult != 0) {
    Serial.println("Failed to add receiver peer");
    return;
  }

  Serial.println("ESP-NOW Transmitter Ready...");

  // Send first data immediately
  measureAndSendData();
  lastSendTime = millis();
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------
void loop() {
  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();
    measureAndSendData();
  }
}