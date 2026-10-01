# ESP32-C3 + L6362A

Native ESP-IDF firmware, not Arduino. Follow the complete
[wiring and build guide](../../docs/examples/esp32-l6362a.md).

```sh
platformio run -d examples/esp32_l6362a
platformio run -d examples/esp32_l6362a -t upload --upload-port /dev/ttyUSB0
platformio device monitor --port /dev/ttyUSB0 --baud 115200
```

Board: ESP32-C3-DevKitM-1. Framework: ESP-IDF 5.4.0, PlatformIO
espressif32 6.10.0. No Wi-Fi/Bluetooth initialization. NVM intentionally
unsupported (`-1`). Actual-firmware LabWired twin and analog C/Q validation
are pending. Firmware compilation and portable tests do not establish them.
