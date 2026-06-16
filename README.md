# LOGI4W Production Firmware

Official production firmware for the Centri LOGI4W WiFi propane tank monitor.

## Overview
- **MCU:** ESP32-C6-WROOM-1
- **Framework:** ESP-IDF v5.5.2
- **Features:** WiFi, BLE provisioning, AWS IoT MQTT, FOTA, sensor drivers, deep sleep
- **Source:** Imported from Perforce (`//Engineering/Projects/Centri/LOGI4/Firmware/LOGI4W/`)

## Building

### Prerequisites
- ESP-IDF v5.5.2

### Build Steps
```bash
# Set up ESP-IDF environment
export IDF_PATH=/path/to/esp-idf-v5.5.2
source $IDF_PATH/export.sh

# Build
idf.py set-target esp32c6
idf.py build
```

### Flash
```bash
idf.py -p PORT flash
```

## Project Structure
```
├── main/              Application source code
│   ├── CLibFiles/     C library wrappers
│   ├── MQTT/          AWS IoT MQTT client
│   ├── StateMachines/ Device state machines
│   ├── certs/         TLS certificates
│   ├── drivers/       Sensor and peripheral drivers
│   ├── hal/           Hardware abstraction layer
│   ├── helpers/       Utility functions
│   ├── interfaces/    Communication interfaces
│   ├── logi/          LOGI4W application logic
│   └── main.cpp       Entry point
├── components/        External components
│   ├── esp-aws-iot/   AWS IoT SDK for ESP-IDF
│   ├── qrcode/        QR code generation (BLE provisioning)
│   └── FreeRTOS-Libraries-Integration-Tests/
├── fota_images/       Pre-built FOTA update images
├── partitions.csv     Flash partition table
├── sdkconfig.defaults Default Kconfig settings
└── version.txt        Firmware version (1.0.0)
```
