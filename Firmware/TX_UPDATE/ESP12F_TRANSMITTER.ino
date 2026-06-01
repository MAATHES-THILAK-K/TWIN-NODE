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
#define ECHO_PIN   4    // Echo pin changed to GPIO4

// --------------------------------------------------
// TANK CONFIGURATION
// Change based on your tank
// --------------------------------------------------
#define TANK_EMPTY_DISTANCE_CM  120.0
#define TANK_FULL_DISTANCE_CM   20.0

// --------------------------------------------------
// DEEP SLEEP CONFIGURATION
// GPIO16 must be connected to RST for wake-up
// --------------------------------------------------
#define SLEEP_TIME_US 30000000UL   // 30 seconds

// --------------------------------------------------
// ULTRASONIC POWER TIMING
// --------------------------------------------------
#define SENSOR_STABILIZE_MS 500    // Your working code used 500 ms
#define SEND_WAIT_MS        300    // Wait for ESP-NOW send callback

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
volatile bool sendDone = false;
volatile bool sendSuccess = false;

// --------------------------------------------------
// SAFE MILLIS WAIT
// --------------------------------------------------
void waitMillis(unsigned long waitTime) {
  unsigned long startTime = millis();

  while (millis() - startTime < waitTime) {
    yield();   // Prevent ESP8266 watchdog reset
  }
}

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
// ULTRASONIC SENSOR POWER CONTROL
// P-Channel MOSFET Gate is ACTIVE LOW
// --------------------------------------------------
void sensorON() {
  digitalWrite(MOSFET_PIN, LOW);   // P-Channel MOSFET ON
  waitMillis(SENSOR_STABILIZE_MS); // Sensor stabilize time
}

void sensorOFF() {
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(MOSFET_PIN, HIGH);  // P-Channel MOSFET OFF
}

// --------------------------------------------------
// ESP-NOW SEND CALLBACK - ESP8266 FORMAT
// --------------------------------------------------
void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  sendDone = true;
  sendSuccess = (sendStatus == 0);

  Serial.print("Send Status: ");

  if (sendSuccess) {
    Serial.println("Success");
  } else {
    Serial.println("Failed");
  }
}

// --------------------------------------------------
// READ ULTRASONIC DISTANCE
// --------------------------------------------------
float readUltrasonicDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    Serial.println("Out of range / No echo received");
    return -1;
  }

  float distanceCm = (duration * 0.0343) / 2.0;

  Serial.print("Distance: ");
  Serial.print(distanceCm);
  Serial.println(" cm");

  return distanceCm;
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
  Serial.println("Wake -> Measure -> Send -> Sleep");

  // Turn ON ultrasonic sensor before reading
  sensorON();

  float distance = readUltrasonicDistanceCM();

  // Turn OFF ultrasonic sensor immediately after reading
  sensorOFF();

  int waterPercent = calculateWaterPercentage(distance);
  int level = calculateLevel(waterPercent);

  tankData.distanceCm = distance;
  tankData.waterPercent = waterPercent;
  tankData.level = level;
  tankData.tankFull = (level == 4);
  tankData.tankEmpty = (level == 1);

  Serial.println("DATA:");

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

  sendDone = false;
  sendSuccess = false;

  uint8_t sendResult = esp_now_send(receiverMacAddress,
                                    (uint8_t *)&tankData,
                                    sizeof(tankData));

  if (sendResult != 0) {
    Serial.println("ESP-NOW Send Start Failed");
    return;
  }

  unsigned long startWait = millis();

  while (!sendDone && (millis() - startWait < SEND_WAIT_MS)) {
    yield();
  }

  if (!sendDone) {
    Serial.println("Send Callback Timeout");
  }
}

// --------------------------------------------------
// GO TO DEEP SLEEP
// --------------------------------------------------
void goToDeepSleep() {
  sensorOFF();   // Safety: make sure ultrasonic sensor is OFF

  Serial.println("Going to Deep Sleep for 30 seconds");
  Serial.flush();

  ESP.deepSleep(SLEEP_TIME_US, WAKE_RF_DEFAULT);
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  waitMillis(100);

  Serial.println();
  Serial.println("ESP12F TX Started - Deep Sleep Mode");

  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Keep ultrasonic sensor OFF at boot
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(MOSFET_PIN, HIGH);

  Serial.println("Initial State: Ultrasonic Sensor OFF");

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
    goToDeepSleep();
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onDataSent);

  int addPeerResult = esp_now_add_peer(receiverMacAddress,
                                       ESP_NOW_ROLE_SLAVE,
                                       ESPNOW_CHANNEL,
                                       NULL,
                                       0);

  if (addPeerResult != 0) {
    Serial.println("Failed to add ESP-NOW peer");
    goToDeepSleep();
  }

  Serial.println("TX Ready");

  measureAndSendData();

  goToDeepSleep();
}

// --------------------------------------------------
// LOOP
// Not used because ESP8266 restarts after deep sleep wake
// --------------------------------------------------
void loop() {
}