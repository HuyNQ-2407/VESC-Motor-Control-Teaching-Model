<div align="center">

# ⚡ VESC_AML

**Custom PID motor control firmware + real-time Qt monitoring/control app for a VESC-based motor controller**

*Ngo Quang Huy — HCMUTE, Automotive Mechatronics Lab (AML)*

![VESC](https://img.shields.io/badge/VESC-bldc%20fork-orange)
![ESP32](https://img.shields.io/badge/ESP32-bridge-blue?logo=espressif&logoColor=white)
![Qt](https://img.shields.io/badge/App-Qt6-41CD52?logo=qt&logoColor=white)
![License](https://img.shields.io/badge/license-GPL--3.0-blue)

</div>

---

## 📖 Overview

A full monitoring and control stack for a VESC motor controller:

- 🎛️ **Custom firmware** (fork of [vedderb/bldc](https://github.com/vedderb/bldc)) — replaces the stock ADC throttle path with a dedicated PID speed controller, and exposes target speed telemetry over the VESC comm protocol.
- 📡 **ESP32 bridge** — connects to the VESC over UART, exposes a TCP server so the Qt app can connect wirelessly, with local Hall-sensor RPM/direction sensing and an I2C LCD status display.
- 🖥️ **Qt6 desktop app** — real-time voltage/current/RPM/duty monitoring, live PID-tunable remote control, and a real-time RPM chart (actual vs. target) built with Qt Charts.

## ✨ Features

| Component | What it does |
|---|---|
| Firmware PID app | Custom PID speed loop driven by an external analog throttle or by remote (host) setpoint, replacing the stock ADC app |
| ESP32 bridge | Non-blocking TCP server, ring-buffered UART parsing, 2-channel Hall RPM + direction detection, LCD status display |
| Qt app | Live telemetry cards, PID tuning, runtime RPM/current limit control, real-time scrolling chart, terminal console |

## 🔧 Hardware

- VESC-compatible motor controller (developed against a Flipsky 4.12 board)
- ESP32 DevKit (WiFi bridge + Hall sensor interface + I2C LCD)
- 20x4 I2C LCD (address `0x27`)
- 2-channel Hall sensor motor feedback
- Custom SolidWorks-designed mounting/enclosure (see `hardware/`)

## 📂 Repository Structure

```
VESC_AML/
├── README.md
├── docs/
│   └── diagrams/              # system/wiring diagrams (.drawio)
├── app/                        # Qt6 desktop app
│   ├── CMakeLists.txt
│   ├── app.rc
│   ├── main.cpp
│   ├── mainwindow.cpp / .h
│   ├── protocol.cpp / .h
│   ├── resource.qrc
│   └── icons/
├── firmware/
│   ├── bldc/                    # fork of vedderb/bldc (GPL-3.0)
│   │   └── CUSTOM_CHANGES.md    # exactly what was modified/added, and why
│   └── esp32_bridge/
│       └── esp32_bridge.ino     # UART <-> TCP bridge + Hall sensing + LCD
└── hardware/
    └── *.SLDPRT / *.SLDASM
```

## 🚀 Getting Started

<details>
<summary><b>1. Firmware (bldc)</b></summary>

See [`firmware/bldc/CUSTOM_CHANGES.md`](firmware/bldc/CUSTOM_CHANGES.md) for exactly what's changed vs. upstream. Build using the standard `bldc` toolchain/build instructions (see upstream [vedderb/bldc](https://github.com/vedderb/bldc) for full build environment setup), then flash via VESC Tool or ST-Link.

</details>

<details>
<summary><b>2. ESP32 bridge</b></summary>

Open `firmware/esp32_bridge/esp32_bridge.ino` in the Arduino IDE (ESP32 board package + `LiquidCrystal_I2C` library required). Update `WIFI_SSID` / `WIFI_PASSWORD` for your network, then flash.

Wiring: VESC UART on ESP32 pins 16 (RX) / 17 (TX) at 115200 baud, Hall channels on pins 27/26, I2C LCD at address `0x27`.

</details>

<details>
<summary><b>3. Qt app</b></summary>

Requires Qt6 (with the Qt Charts module) on **Windows**.

Open `app/CMakeLists.txt` in Qt Creator as a project, select a Windows kit (MSVC or MinGW), and build. Alternatively, from a Qt-configured command prompt:

```bat
cd app
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

On launch, enter the ESP32's IP address (shown on its LCD after WiFi connects) and click **CONNECT**.

</details>

## ⚠️ Notes

- This is a **fork**, not a from-scratch firmware — see [`firmware/bldc/CUSTOM_CHANGES.md`](firmware/bldc/CUSTOM_CHANGES.md) for the precise diff summary before assuming any given file is original work.
- The Qt app and firmware are coupled by exact string matching in a few places (e.g. the app parses specific terminal output lines from the firmware). If you change firmware print strings, check `app/mainwindow.cpp`'s `onTerminalOutput()` and `app/protocol.cpp`'s `parseResponse()` for matching logic before assuming it's safe.

## 🙏 Credits

- Firmware based on [vedderb/bldc](https://github.com/vedderb/bldc) (GPL-3.0). See [`firmware/bldc/CUSTOM_CHANGES.md`](firmware/bldc/CUSTOM_CHANGES.md) for what was added/modified here.
- Charting in the Qt app uses [Qt Charts](https://doc.qt.io/qt-6/qtcharts-index.html), part of the Qt framework.

## 📄 License

Firmware (`firmware/bldc/`) is GPL-3.0, inherited from upstream `vedderb/bldc`. The Qt app and ESP32 bridge code are original work by the author.
