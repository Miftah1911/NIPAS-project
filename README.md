# 🌱 NIPAS — Non-Invasive Precision Agriculture System

> An autonomous IoT field robot for smallholder farmers in Bangladesh — monitors soil, detects leaf disease via HSV vision, sprays fungicide only where needed, and streams live telemetry to a WebSocket dashboard. **Zero cloud dependency.**

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![MQTT: HiveMQ](https://img.shields.io/badge/MQTT-HiveMQ%20Cloud-purple.svg)](https://www.hivemq.com/mqtt-cloud-broker/)
[![Build Cost: ৳3.4K](https://img.shields.io/badge/BOM-৳3,440–4,000-orange.svg)](#-hardware-requirements)

> **AI Usage Disclosure:** This project was developed with AI assistance. See [AI_USAGE.md](AI_USAGE.md) for full transparency about what was AI-assisted and what the team did independently.

---

## 📖 Table of Contents

- [Overview](#-overview)
- [System Architecture](#-system-architecture)
- [Repository Structure](#-repository-structure)
- [File Purpose Guide](#-file-purpose-guide)
- [Hardware Requirements](#-hardware-requirements)
- [Software Setup](#-software-setup)
- [MQTT & Internet Configuration](#-mqtt--internet-configuration)
- [Connectivity — What Works Where](#-connectivity--what-works-where)
- [2G/3G Payload Efficiency](#-2g3g-payload-efficiency)
- [Wiring Diagrams](#-wiring-diagrams)
- [Build & Flash Instructions](#-build--flash-instructions)
- [Running the Dashboard](#-running-the-dashboard)
- [Firmware Tools](#-firmware-tools)
- [How It Works](#-how-it-works)
- [Calibration](#-calibration)
- [Troubleshooting](#-troubleshooting)
- [Presentation Deck](#-presentation-deck)
- [BuildAthon Track C Compliance](#-buildathon-track-c-compliance)
- [Project Context](#-project-context)
- [Known Limitations](#-known-limitations)
- [Contributing](#-contributing)
- [License](#-license)
- [Acknowledgements](#-acknowledgements)
- [Team](#-team)
- [Contact](#-contact)

---

## 🎯 Overview

NIPAS is a low-cost (~৳3,400 / $30) autonomous field robot designed for smallholder vegetable farmers growing **tomato, chili, and brinjal** in Bangladesh.

**What it does:**
- Drives down crop rows autonomously using encoder odometry
- Stops at every plant, sweeps a camera left-to-right
- Runs **HSV color analysis** on-device to detect chlorosis (yellowing) and necrosis (brown spots)
- **Automatically sprays fungicide** only where infection is detected — dose proportional to severity
- Detects obstacles with an ultrasonic sensor and avoids them
- Turns 180° at row end and continues on the next row
- Streams live telemetry to a browser dashboard over MQTT
- **Works fully offline** — buffers readings to flash memory when network is unavailable

**Why it matters:**
- Saves ~70% water vs flood irrigation
- Reduces chemical usage by ~50% via targeted micro-dosing
- Detects disease before visible symptoms appear
- Works **offline** — no cloud subscription, no SIM required for local operation

---

## 🏗 System Architecture

**Offline-First Design:** The robot's core autonomy — driving, scanning, spraying — runs entirely offline. Only live telemetry to the dashboard requires connectivity. Data buffers to flash and syncs automatically when any network becomes available.

---

## 📁 Repository Structure

---

## 📄 File Purpose Guide

### 🖥 Frontend (Browser)

| File | Purpose | Runs on |
|---|---|---|
| **`index.html`** | Browser-based GUI — telemetry charts, KPI cards, robot status, leaf vision panel, manual controls, spray audit log, event log, settings drawer. Includes demo/realistic speed toggle. | Any modern browser |
| **`presentation.html`** | 8-slide BuildAthon pitch deck with cumulative timing bar, live demo slide, and honest limitations section | Any modern browser (arrow keys to navigate) |

### ⚙️ Firmware (ESP32)

| File | Purpose | Flashed to |
|---|---|---|
| **`NIPAS_Config.h`** | **Edit this first.** WiFi credentials (via WiFiManager), MQTT broker details, pin assignments, motion parameters, field defaults. Included by all `.ino` files. | — (header file) |
| **`NIPAS_Robot/NIPAS_Robot.ino`** | **Main production firmware.** Drives motors, reads encoders, coordinates scan→spray cycle, publishes MQTT telemetry, subscribes to dashboard commands. Includes offline flash buffer with auto-sync. | ESP32 Dev Board |
| **`NIPAS_CAM/NIPAS_CAM.ino`** | Vision module — receives `SCAN` over UART, sweeps servo, runs HSV analysis, returns JSON result | ESP32-CAM (AI-Thinker) |

### 🔧 Firmware Tools (Development & Testing)

| File | Purpose | When to use |
|---|---|---|
| **`NIPAS_Calibration.ino`** | Standalone sensor calibration tool. Prints raw ADC values, ultrasonic distances, encoder counts, and battery voltage to Serial Monitor. | **Before assembly** — find your sensor baselines |
| **`NIPAS_Simulation.ino`** | Full state machine running **without any hardware**. Motor commands print to Serial. Sensor values are synthetic. **MQTT still publishes real data.** | **Before you have hardware** — test the full MQTT pipeline |
| **`NIPAS_Test.ino`** | Hardware diagnostic. Tests each component one at a time: LED, buzzer, relay, servo, motors, ultrasonic, moisture sensor, encoders. | **The day you assemble** — verify every component works |

### 📚 Documentation

| File | Purpose |
|---|---|
| **`README.md`** | This file — full setup, wiring, calibration, connectivity guide |
| **`AI_USAGE.md`** | AI usage disclosure per BuildAthon Section 02 requirement |
| **`docs/wiring-diagrams.md`** | Detailed wiring for every component with pin tables |
| **`docs/calibration-log.md`** | Record your robot's measured values |

### 🔑 Which file do you edit when you want to...

| Change... | Edit this file |
|---|---|
| WiFi password | `firmware/NIPAS_Config.h` (managed by WiFiManager on first boot) |
| MQTT broker credentials | `firmware/NIPAS_Config.h` |
| Device ID / MQTT topics | `firmware/NIPAS_Config.h` |
| Motor speed, wheel diameter | `firmware/NIPAS_Config.h` |
| Plant spacing / row length defaults | `firmware/NIPAS_Config.h` |
| Pin assignments | `firmware/NIPAS_Robot/NIPAS_Robot.ino` (top of file) |
| HSV disease color ranges | `firmware/NIPAS_CAM/NIPAS_CAM.ino` (`isChlorosis`, `isNecrosis`) |
| State machine logic | `firmware/NIPAS_Robot/NIPAS_Robot.ino` (`loop()` switch-case) |
| Offline buffer size | `firmware/NIPAS_Robot/NIPAS_Robot.ino` (`BUFFER_MAX` constant) |
| Dashboard look & feel | `index.html` (`<style>` block) |
| Dashboard behavior | `index.html` (`<script>` block at bottom) |
| Demo vs realistic speed | `index.html` — speed toggle above the telemetry chart |

---

## 🛠 Hardware Requirements

### Bill of Materials

| Component | Purpose | Cost (৳) |
|---|---|---|
| ESP32 Dev Board | Main microcontroller | 490 – 650 |
| ESP32-CAM + FTDI Programmer | Camera + vision processing | 750 – 950 |
| Capacitive Soil Moisture v1.2 | Root-zone moisture sensor | 120 – 180 |
| DHT11 / DHT22 | Temperature + humidity | 120 – 180 |
| HC-SR04 Ultrasonic | Obstacle detection | 80 – 120 |
| IR Speed / Encoder Sensors ×2 | Wheel odometry | 100 – 140 |
| L298N Motor Driver | Dual H-bridge for motors | 150 – 180 |
| 2WD Smart Car Chassis Kit | Frame, 2× TT motors, wheels, caster | 500 – 650 |
| SG90 Micro Servo | Camera sweep | 120 – 160 |
| 5V Relay Module | Pump switching | 70 – 110 |
| 12V Mini Pump / Atomizer | Spray delivery | 250 – 380 |
| LM2596 DC-DC Buck Converter | 12V → 5V for ESP32 | 90 – 130 |
| 18650 Li-ion × 2 + Holder | Battery pack (7.4V) | 350 – 500 |
| Hydrophobic Lens Shield | Camera protection | 50 – 100 |
| Silicone Tubing + Reservoir | Fluid routing | 200 – 300 |
| **TOTAL** | | **~৳3,440 – ৳4,000** |

### Optional Add-ons

| Component | Purpose | Cost (৳) |
|---|---|---|
| A7670C 4G LTE Module | Independent cellular connectivity | 2,200 – 3,000 |
| MPU6050 Gyro | Precision 180° turns | 150 – 250 |
| Solar panel + charge controller | Extended field runtime | 800 – 1,200 |

### Tools Needed

- Soldering iron + solder
- Multimeter
- Wire strippers
- Small screwdrivers
- USB cable for ESP32
- FTDI USB-to-TTL adapter (for ESP32-CAM programming)

### Where to buy (Bangladesh)

- **Electronics.com.bd** — full range of ESP32, sensors, motors
- **BDTronics** — motors, drivers, chassis kits
- **Techshop Bangladesh** — ESP32-CAM, camera modules
- Local Dhaka markets: **Bashundhara City**, **Multiplan Center**

---

## 💻 Software Setup

### 1. Install Arduino IDE

Download from [arduino.cc/en/software](https://www.arduino.cc/en/software) — version **2.0+** recommended.

### 2. Add ESP32 board support

1. Open Arduino IDE → **File → Preferences**
2. In **Additional Board Manager URLs**, paste:
3. Click **OK**
4. Go to **Tools → Board → Boards Manager**
5. Search `esp32` → install **"esp32 by Espressif Systems"** (latest)

### 3. Install required libraries

**Tools → Manage Libraries** → install each:

| Library | Author | Purpose |
|---|---|---|
| PubSubClient | Nick O'Leary | MQTT client |
| ArduinoJson | Benoit Blanchon | JSON serialization |
| ESP32Servo | Kevin Harrington | Servo control |
| DHT sensor library | Adafruit | Temperature/humidity |
| Adafruit Unified Sensor | Adafruit | DHT dependency |
| WiFiManager | tzapu | Zero-config WiFi setup |

**Note:** `esp32-camera` is bundled with the ESP32 board package — no separate install needed.

### 4. Clone this repository

```bash
git clone https://github.com/Miftah1911/NIPAS-project.git
cd NIPAS-project