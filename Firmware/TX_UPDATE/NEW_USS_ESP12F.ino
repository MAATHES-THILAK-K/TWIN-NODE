#include <Arduino.h>

// --------------------------------------------------
// PIN CONFIGURATION
// --------------------------------------------------
#define MOSFET_PIN 13   // P-Channel MOSFET Gate, ACTIVE LOW
#define TRIG_PIN   12
#define ECHO_PIN   4    // New ECHO pin

// --------------------------------------------------
// GLOBAL VARIABLES
// --------------------------------------------------
long duration;
float distance_cm;

// --------------------------------------------------
// ULTRASONIC SENSOR POWER CONTROL
// --------------------------------------------------
void sensorON() {
  digitalWrite(MOSFET_PIN, LOW);   // P-Channel MOSFET ON
  Serial.println("MOSFET ON  -> Ultrasonic Sensor ON");
  delay(500);                      // Sensor stabilize time
}

void sensorOFF() {
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(MOSFET_PIN, HIGH);  // P-Channel MOSFET OFF
  Serial.println("MOSFET OFF -> Ultrasonic Sensor OFF");
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

  duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    Serial.println("Out of range / No echo received");
    return -1;
  }

  distance_cm = (duration * 0.0343) / 2.0;

  Serial.print("Echo Duration: ");
  Serial.print(duration);
  Serial.println(" us");

  Serial.print("Distance: ");
  Serial.print(distance_cm);
  Serial.println(" cm");

  return distance_cm;
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP12F MOSFET + Ultrasonic GPIO4 Test Started");

  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(MOSFET_PIN, HIGH);  // Sensor OFF initially

  Serial.println("Initial State: Sensor OFF");
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------
void loop() {
  Serial.println();
  Serial.println("----- TEST START -----");

  sensorON();

  readUltrasonicDistanceCM();

  sensorOFF();

  Serial.println("----- TEST END -----");

  delay(3000);
}
