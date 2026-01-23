# TWIN-NODE – Indoor Node

## Overview

The **Indoor Node** is a **battery-powered, ultra-low-power receiver and alert unit** for the TWIN-NODE water level monitoring system.
It receives water level data from the outdoor sensor node using **ESP-NOW** and provides **local LED and buzzer alerts**, without internet or Wi-Fi infrastructure.

![Indoor Node Front View](https://github.com/MAATHES-THILAK-K/TWIN-NODE/blob/main/TWIN%20NODE-INDOOR/IMAGES/FRONT_VIEW.png)

---

## Key Features

* ESP-NOW wireless communication
* Battery-powered operation
* Ultra-low-power design
* LED level indication
* Buzzer alert for critical levels
* No internet, no router, no cloud

---

## Hardware Summary

| Item          | Specification     |
| ------------- | ----------------- |
| MCU           | ESP-12F (ESP8266) |
| Communication | ESP-NOW           |
| Indicators    | LEDs              |
| Alert         | Buzzer            |
| Power Source  | Battery           |
| Design Tool   | KiCad             |

---

## Power Design

* Deep sleep enabled
* Wake on ESP-NOW receive
* Minimal standby current
* Optimized for long-term battery life

---

## Repository Contents

| Path                 | Description         |
| -------------------- | ------------------- |
| `GERBER/`            | Manufacturing files |
| `IMAGES/`            | PCB images          |
| `REPORT/`            | ERC / DRC           |
| `3D MODELS/`         | Mechanical models   |
| `TWIN_NODE2.kicad_*` | Schematic & PCB     |

---
