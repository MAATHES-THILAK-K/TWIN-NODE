# 🌊 Smart Water Tank Monitoring System (ESP8266 + ESP-NOW)

## 📌 Overview

This project implements an **ultra-low-power outdoor water level monitoring system** using an ESP-12F (ESP8266) and ultrasonic sensing. The system communicates with an indoor node via **ESP-NOW**, ensuring efficient, connectionless data transfer.

The design prioritizes:

* 🔋 Ultra-low power consumption (solar + Li-ion)
* ⚡ Event-driven communication
* 📡 Reliable short-range wireless communication
* 🧠 Intelligent state-based operation

---

## 🏗️ System Architecture

### 🌿 Outdoor Node (Sensor Unit)

* ESP-12F (ESP8266)
* Ultrasonic sensor (power-gated via MOSFET)
* Solar + Li-ion powered
* Operates mostly in **deep sleep**

### 🏠 Indoor Node (User Interface)

* ESP8266 (ESP-NOW receiver)
* 4 LEDs (water level indication)
* Buzzer (alerts)
* Optional mobile connectivity

---

## 🔄 System Workflow

### 🌿 Outdoor Node (State Machine)

---

### 🔹 LOW POWER MODE

```text
[Deep Sleep]

↓ Wake (every 5 minutes)

Power ON ultrasonic
Wait 50 ms stabilization

Take 3 readings → Apply median filter
Power OFF ultrasonic

IF |current_distance - last_sent| < delta_threshold:
    → Return to deep sleep (5 min)

ELSE:
    Send data via ESP-NOW

    Wait 100 ms for ACK

    IF ACK received:
        last_sent = current_distance
        Reset failure counter
        Switch to ACTIVE MODE

    ELSE:
        Retry (max 2 times)
        → Return to deep sleep (5 min)
```

---

### ⚡ ACTIVE MODE

```text
Wake every 1–2 minutes

Measure distance (filtered)

IF |current_distance - last_sent| ≥ delta_threshold:
    Send data → Wait for ACK

    IF ACK received:
        Update last_sent
        Reset failure counter

    ELSE:
        Increment failure counter

IF failure counter ≥ 3:
    Switch to LOW POWER MODE

Return to deep sleep (1–2 min)
```

---

## 🏠 Indoor Node Logic

```text
Always listening (ESP-NOW)

On receiving data:
    Send ACK immediately
    Store received distance
    Update LED indicators
    Trigger buzzer (only if critical)

Optional:
    Send data to mobile application
```

---

## 📊 Water Level Indication

Water level is computed as:

```text
Level (%) = (Tank Height - Measured Distance) / Tank Height
```

| Level (%) | Indicator |
| --------- | --------- |
| 0–25%     | LED 1     |
| 25–50%    | LED 2     |
| 50–75%    | LED 3     |
| 75–100%   | LED 4     |

---

## 🔊 Buzzer Logic

The buzzer is triggered only when:

* ✅ Tank becomes FULL
* ⚠️ Sudden drop in water level (optional)

Avoid continuous alerts to improve user experience.

---

## 🧪 Sensor Filtering

To improve accuracy in outdoor conditions:

* Take **3 consecutive readings**
* Apply **median filtering**
* Reject noise and outliers

---

## 📡 Communication Protocol

### Data Packet (Outdoor → Indoor)

```c
struct DataPacket {
  uint16_t distance;
  uint8_t state;
  uint8_t battery; // optional
};
```

### ACK Packet (Indoor → Outdoor)

```c
struct AckPacket {
  uint8_t status; // 1 = received
};
```

---

## ⚡ Power Optimization Techniques

* Deep sleep between operations
* Sensor power gating via MOSFET
* Short ESP-NOW transmission bursts
* No continuous listening on outdoor node
* Event-driven communication

---

## 🔋 Power Profile

| Mode       | Current Consumption   |
| ---------- | --------------------- |
| Deep Sleep | ~20–70 µA             |
| Active     | ~120 mA (short burst) |

---

## 🚨 Fail-Safe Mechanisms

| Condition             | System Behavior                    |
| --------------------- | ---------------------------------- |
| Indoor node OFF       | Retry → fallback to LOW POWER MODE |
| Communication failure | Retry (max 2 times)                |
| No ACK in ACTIVE MODE | Return to LOW POWER MODE           |
| Sensor noise          | Median filtering applied           |

---

## 🚀 Future Improvements

* 🔋 Battery-aware adaptive sleep intervals
* 📶 Mobile app integration
* ☁️ Cloud logging
* 📉 Data history visualization
* 🧠 Predictive water usage analytics

---

## 🎯 Key Design Goals Achieved

* ✅ Ultra-low power outdoor node
* ✅ Reliable wireless communication
* ✅ Event-driven updates
* ✅ Scalable architecture
* ✅ User-friendly indoor interface

---


## 🙌 Author

Developed by **MAATHES THILAK K**
IInd year (4th semester)
March 22 2.22am 
---
