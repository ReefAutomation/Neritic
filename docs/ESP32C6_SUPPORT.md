# ESP32-C6 Support

DeepGlow includes PlatformIO environments for ESP32-C6:

- `esp32c6`
- `esp32c6_debug`

## Build and flash

```bash
pio run -e esp32c6_debug
pio run -e esp32c6_debug -t uploadfs
pio run -e esp32c6_debug -t upload
```

## Notes

- Framework: ESP-IDF via PlatformIO
- Keep `espressif32` platform updated for latest C6 fixes
- Validate Wi-Fi, OTA, timers, and effects after flashing
