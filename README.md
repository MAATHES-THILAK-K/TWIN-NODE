## 📌 Project Overview

**TWIN-NODE** is a **two-node, ultra-low-power water level monitoring system** for overhead tanks.
It uses **ESP-NOW wireless communication**, requires **no internet**, and is optimized for **solar-powered outdoor operation**.

This project is **designed for manufacturable PCBs**, with all components chosen for reliability and low power.

---

## 🧩 System Architecture

| Node                             | Description                                                                             |
| -------------------------------- | --------------------------------------------------------------------------------------- |
| **NODE-1 (Outdoor Sensor Node)** | Measures water level using a waterproof ultrasonic sensor and transmits data wirelessly |
| **NODE-2 (Indoor Display Node)** | Receives water level data and alerts the user using LEDs and a buzzer                   |

---

## 🔧 NODE-1 – Outdoor Sensor Node

| Feature       | Description                               |
| ------------- | ----------------------------------------- |
| Controller    | ESP-12F (ESP8266)                         |
| Sensor        | SR04M / JSN-SR04T Waterproof Ultrasonic   |
| Power Source  | 18650 Li-ion Battery                      |
| Charging      | Solar Panel + TP4056                      |
| Regulation    | RT9080-3.3 V LDO                          |
| Communication | ESP-NOW                                   |
| Power Saving  | Deep Sleep + GPIO-controlled sensor power |
| Installation  | Weatherproof Enclosure                    |

---

## 🔋 Low-Power Design

| Technique                      | Purpose                           |
| ------------------------------ | --------------------------------- |
| Deep Sleep Mode                | Minimizes battery consumption     |
| GPIO-controlled Sensor Power   | Cuts sensor standby current       |
| LDO with low quiescent current | Efficient voltage regulation      |
| ESP-NOW                        | Low-energy wireless communication |

---

## 📂 Repository Structure

| Folder            | Purpose                                                |
| ----------------- | ------------------------------------------------------ |
| `TWIN NODE V1.0/` | Stable, tested hardware revision                       |
| `TWIN NODE V2.0/` | Updated hardware revision (new features, improvements) |
| `Gerber/`         | Manufacturing-ready files                              |
| `IMAGES/`         | PCB renders and photos                                 |
| `REPORT/`         | ERC, DRC, design checks                                |
| `3D MODELS/`      | Mechanical references                                  |
| `LICENSE`         | Open-source licensing information                      |
| `README.md`       | Project overview and documentation                     |

---

## 🚀 Engineering Note

* Designed for **low-power operation**
* **Solar and battery-friendly**
* **Manufacturable PCB**
* Versioned structure makes it easy to track revisions

---

### 👤 Author

**MAATHES-THILAK-K (KMT)**
Embedded Systems | PCB Design | Robotics

---
