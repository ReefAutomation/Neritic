# Installation

## Requirements

- macOS, Linux, or Windows
- Python 3.10+
- USB cable for your board
- 5V LED power supply sized for your strip
- PlatformIO CLI or VS Code PlatformIO extension

## 1) Clone the repository

```bash
git clone <your-repo-url>
cd DeepGlow
```

## 2) Select your PlatformIO environment

Available environments:

- `esp32d` (default)
- `esp32d_debug`
- `esp32`, `esp32_debug`
- `esp32c3`, `esp32c3_debug`
- `esp32s3`, `esp32s3_debug`
- `esp32c6`, `esp32c6_debug`

## 3) Build

```bash
# Default environment (esp32d)
pio run

# Example: explicit debug build
pio run -e esp32d_debug
```

## 4) Upload assets + firmware

```bash
# Upload LittleFS web assets
pio run -e esp32d_debug -t uploadfs

# Upload firmware
pio run -e esp32d_debug -t upload
```

## 5) Monitor serial output

```bash
pio device monitor -b 115200
```

## 6) First configuration

1. Connect to AP SSID `AquariumLED`
2. Open `http://192.168.4.1`
3. Set Wi-Fi credentials and timezone
4. Save and allow reboot

## Common install issues

- `command not found: pio` → install PlatformIO CLI and re-open terminal
- Build succeeds but UI is missing → run `uploadfs` again
- Board not found on upload → set upload port in `platformio.ini` or pass `--upload-port`
