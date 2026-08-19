# ESP32-S3 Pour-Over Kettle Evaluator

[**中文 README**](README.zh-CN.md) | **English**

An ESP32-S3 + MAX31855 thermocouple tool for **recording and evaluating the temperature-control performance of pour-over coffee kettles** (e.g. Fellow / Brewista, Timemore). It measures the water temperature curve, records sessions with kettle/water metadata into CSV files, and scores the controller's PID behavior — fully offline, usable anywhere.

![webui](https://img.shields.io/badge/WebUI-embedded%2C%20zero%20dependency-blue) ![platform](https://img.shields.io/badge/PlatformIO-espressif32%406.12.0-orange)

## Features

- **Real-time temperature chart** — canvas-drawn, no external assets, works without internet (AP mode)
- **Kettle management** — name / empty weight / lid weight / nominal capacity, edit & delete, persisted on LittleFS
- **Weighing integration** — 有盖/无盖 toggle, enter total weight → water volume computed in real time (`water = total − dry − lid`), synced to the device and written into every CSV row
- **Target & reference temperatures** — target temp (default 93 °C) and initial reference temp (default 25 °C, used to cancel out thermocouple offset in analysis); both persisted
- **Recording to PSRAM** — up to 250,000 samples (~4 MB), auto-flushed to LittleFS CSV on stop; filename embeds kettle + start/end temps:
  `fellow_23.5c-98.4c_20260819_163000.csv`
- **In-browser CSV analysis** — pick a saved session and get an instant report: score card, metrics table, three-segment chart (blue=unheated, red=rise, green=steady), inferred heating power
- **Python analysis** (`tools/analyze.py`) — stdlib-only, generates the same HTML report offline; scoring: rise time 30, overshoot 25, steady bias 25, steady oscillation 20
- **Dual-network** — open Host AP `ESP32S3-Thermo` at `4.4.4.1` with captive portal, plus STA on your saved Wi-Fi for NTP sync
- **Serial CLI** — full command set: files, time, wifi, host toggle, record, stats…

## Hardware

| Component | Spec |
|---|---|
| **MCU** | Espressif **ESP32-S3** — Xtensa LX7 dual-core @ 240 MHz, 512 KB SRAM, Wi-Fi 802.11 b/g/n + BLE 5 |
| **Module** | ESP32-S3-WROOM-1 — **16 MB quad flash + 8 MB octal PSRAM** (YD-ESP32-S3 board) |
| **Temp sensor** | **MAX31855** K-type thermocouple amplifier — SPI, 0.25 °C resolution, built-in cold-junction compensation, fault flags (open / short GND / short VCC) |
| **Thermocouple** | K type, e.g. waterproof probe — range covers 15–110 °C |
| **Record button** | tactile button, active-low |
| **Status LED** | onboard WS2812 RGB (GPIO48) |

## Pinout & Wiring

| Signal | Pin | Wiring |
|---|---|---|
| MAX31855 **MISO** (DO) | GPIO4 | sensor DO → GPIO4 (SPI in) |
| MAX31855 **SCK** (CLK) | GPIO6 | sensor CLK ← GPIO6 (SPI clock, 1 MHz, mode 0) |
| MAX31855 **CS** | GPIO7 | sensor CS ← GPIO7 (chip select, manual) |
| Record **button** | GPIO38 | button to GND, internal pull-up enabled |
| Onboard **RGB LED** | GPIO48 | WS2812 (NEO_GRB, 800 kHz), brightness 10% |

Wiring note: MAX31855 uses 3-wire SPI (MISO/SCK/CS — no MOSI needed). Power the sensor module with 3.3 V and common GND; the thermocouple connects to the module's T+ / T− screw terminals.

Board: **YD-ESP32-S3** (16 MB flash / 8 MB octal PSRAM). The board definition lives in [`boards/YD-ESP32-S3.json`](boards/YD-ESP32-S3.json) (qio_opi, 80 MHz) — **do not delete it**: without it the firmware boot-loops (`rst:0x3`) on this board.

LED states: green default · blue STA connected · yellow STA failed · **amber = AP client joined** · red recording (highest priority). Brightness fixed at 10%.

## Quick Start

```bash
pio run -t upload        # build & flash
pio device monitor -b 115200
```

Wi-Fi credentials are **not hardcoded**. Copy `wifi-config.example.txt` → `wifi-config.txt`, fill in your SSID/password, then build — or leave it empty and set them later from the WebUI / serial `wifi` command (they persist to LittleFS).

> macOS flashing notes: esptool auto-reset works (sync at 115200, then 921600 for flashing). If the auto-download circuit doesn't respond, hold **BOOT**, tap **RESET**, release, then flash with `--before no_reset` at 115200.

## Usage

### Access

- **AP mode (default)**: join `ESP32S3-Thermo` (open network) → captive portal opens `http://4.4.4.1/` automatically
- **STA mode**: browse `http://<dhcp-ip>/` on the same LAN (IP is printed on serial after connect)

### WebUI

| Area | What it does |
|---|---|
| Real-time tab | temp + record button, kettle selector, lid toggle, total weight → water, target/initial temps, live chart |
| CSV analysis tab | pick a saved session → full report (score, metrics, three-segment chart, inferred power) |
| Kettle management | create/edit/delete kettles (name, dry weight, lid weight, capacity) |
| CSV files | download / delete / clear all saved sessions |
| Wi-Fi settings | set SSID/password and reconnect |
| Network mode | toggle Host(AP) ⇄ STA |

### Serial CLI (115200)

| Command | Function |
|---|---|
| `help` | list commands |
| `ls` / `del <file>` / `clr` | list / delete / clear CSV files |
| `show` / `cat <file>` | show current buffer / file content |
| `time Y M D h m s` | set RTC time |
| `sync` | NTP re-sync |
| `wifi <ssid> <pass>` | connect & save new Wi-Fi |
| `host` | toggle Host(AP) ⇄ STA |
| `rec` / `stop` | start / stop recording |
| `temp` | read temperature once |
| `stats` | memory / recording / network stats |

### CSV format

```
# kettle:Fellow-黑色, cap:525ml, lid:on, dry:578.2g, lid_w:65.5g, total:1169.8g, water:526.1g, init:24.0c
time,temperature_c,water_g,target_c
2026-08-19 17:20:13,27.50,526.1,93.0
...
```

Header comment carries the kettle snapshot (PV/water/SV all present → PID evaluation ready). `init` is the reference temperature used by the analysis to cancel constant thermocouple offset.

## Scoring Methodology

| Metric | Weight | Reference |
|---|---|---|
| Rise time | 30 | vs. theoretical fastest (inferred power) |
| Overshoot | 25 | ≤0.5 °C full marks |
| Steady bias | 25 | ≤0.3 °C full marks |
| Steady oscillation | 20 | ±0.5 °C good · ±1 °C fair · ±2 °C poor · ≥±3 °C unacceptable |

Analysis starts at the first 0.25 °C rise (differential detection), the steady segment is the whole interval from "first enter ±0.5 °C and never leave ±1 °C" to the end; time axis zeroes at the rise start.

## Project Structure

```
├── README.md / README.zh-CN.md   # docs (EN primary, CN mirror)
├── plan.md                       # design & change log (Chinese)
├── platformio.ini                # espressif32@6.12.0
├── partitions.csv                # 16 MB: app 6 MB + LittleFS ~9.9 MB
├── boards/YD-ESP32-S3.json       # board def — required, do not delete
├── wifi-config.example.txt       # template (copy to wifi-config.txt, gitignored)
├── include/config.h              # pins, network, sampling params
├── scripts/gen_wifi_config.py    # build hook: wifi-config.txt → include/wifi_config.h
├── src/
│   ├── main.cpp                  # WiFi/AP/portal/HTTP API/CLI/button/LED/kettles
│   ├── max31855.cpp/h            # thermocouple driver
│   ├── recorder.cpp/h            # PSRAM recording + CSV persistence
│   └── html.h                    # embedded WebUI (incl. in-browser analysis)
└── tools/analyze.py              # offline Python analysis → HTML report
```

## Troubleshooting

- **Boot loop `rst:0x3`** — wrong platform/board combo. Use `espressif32@6.12.0` + `boards/YD-ESP32-S3.json` (7.0.1 + custom partitions loops on this board).
- **Chinese-named CSV downloads 404** — older firmware limited filenames to 40 bytes; reflash.
- **`steadyMean` toFixed error in analysis** — reflash (fixed).
- **No serial data on upload** — hold BOOT + tap RESET, upload with `--before no_reset`.
