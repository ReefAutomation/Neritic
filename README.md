# Neritic

Standalone ESP-IDF based aquarium LED controller for ESP32-family boards.

Neritic provides:
- Real-time LED control with smooth transitions
- Presets, timers, sunrise/sunset scheduling, and safety limits
- Embedded web UI + REST API + WebSocket updates
- OTA update support

## Supported Environments

From `platformio.ini`:

- `esp32d` (default)
- `esp32d_debug`
- `esp32`
- `esp32_debug`
- `esp32c3`
- `esp32c3_debug`
- `esp32s3`
- `esp32s3_debug`
- `esp32c6`
- `esp32c6_debug`

## Quick Start


1. Install PlatformIO (CLI). Recommended: install the CLI into a Python virtual environment and use the pinned dependencies in `requirements.txt`:

```bash
# create and activate a venv (recommended to keep tools isolated)
python3 -m venv .venv
source .venv/bin/activate

# upgrade pip then install pinned tooling from this repo
python -m pip install --upgrade pip
pip install -r requirements.txt

# verify installation
pio --version
```

These instructions install the PlatformIO CLI into a local virtual environment. On Windows use the equivalent venv activation command for your shell.

2. Clone this repository
3. Build firmware
4. Upload filesystem assets
5. Upload firmware

```bash
git clone <your-repo-url>
cd Neritic

# Build (default env: esp32d)
pio run

# Build explicit environment
pio run -e esp32d_debug

# Upload filesystem and firmware
pio run -e esp32d_debug -t uploadfs
pio run -e esp32d_debug -t upload

# Serial monitor
pio device monitor -b 115200
```

## Default Runtime Access

- AP hostname / SSID default: `AquariumLED`
- AP setup page: `http://192.168.4.1`
- REST base path: `/api/*`
- WebSocket endpoint: `/ws`

## Documentation

- [Installation](docs/INSTALLATION.md)
- [Quick Start](docs/QUICKSTART.md)
- [Configuration](docs/CONFIGURATION.md)
- [API](docs/API.md)
- [Preset Effects](docs/PRESET_EFFECTS.md)
- [Wiring](docs/WIRING.md)
- [Safety Guidelines](docs/SAFETY_GUIDELINES.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [ESP32-C6 Notes](docs/ESP32C6_SUPPORT.md)

## Notes

- Brightness, speed, and intensity exposed by API/UI are percentage values (`0-100`).
- Internal LED values are converted to `0-255` in firmware.
- Filesystem is LittleFS and static assets are embedded during build.
