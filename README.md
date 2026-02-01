# 🐠 DeepGlow - Standalone Aquarium LED Controller

DeepGlow is a robust, open-source aquarium lighting controller for ESP32/ESP8266 microcontrollers. It provides automated, fish-safe LED control with advanced scheduling, custom effects, and a modern web interface. Designed for reliability and flexibility, DeepGlow supports a wide range of addressable LEDs and offers both beginner-friendly setup and advanced customization.

## Project Highlights
- **Fish Safety:** Gradual transitions, maximum brightness caps, and sunrise/sunset simulation protect aquatic life.
- **Native Effects:** Full support for WS2812FX effects, custom presets, and color arrays.
- **Scheduling:** NTP time sync, sun-based timers, day-of-week selection, and boot recovery.
- **Web Interface:** Responsive SPA for real-time control, preset management, and configuration (mobile & desktop).
- **Modular Firmware:** Easily extend or customize with C++ and Python scripts.

## Hardware Requirements
- ESP32 or ESP8266 board (2MB+ flash recommended)
- Addressable LED strip: WS2812B, SK6812, APA102, up to 512 LEDs
- 5V power supply (adequate for LED count)
- Optional: relay module for power switching

## Supported LED Types
- WS2812B (NeoPixel)
- SK6812 (RGBW)
- APA102 (DotStar)

## Wiring Overview
- Default data pin: GPIO2 (ESP8266/ESP32)
- Use level shifter (74HCT245) for reliability
- Add 470Ω resistor to data line, 1000µF capacitor across LED power
- Relay module optional for safety/scheduling

## Aquarium Safety Features
- Minimum transition times (5s+)
- Maximum brightness enforcement
- Acclimation period for new tanks
- Safe effect recommendations

## Web Interface Features
- Real-time WebSocket updates
- Visual preset management
- Schedule dashboard
- Configuration panel

## Quick Installation
1. Install PlatformIO (VS Code extension or CLI)
2. Clone the DeepGlow repository
3. Build firmware for your board
4. Upload filesystem and firmware
5. Connect to device AP and configure WiFi

### Example Commands
```bash
pip install platformio
# Clone repository
git clone <your-repo-url>
cd DeepGlow
# Build for ESP32
pio run -e esp32d_debug
# Upload filesystem
pio run -t uploadfs -e esp32d_debug
# Upload firmware
pio run -t upload -e esp32d_debug
```

## Documentation
- [Installation & Setup](docs/INSTALLATION.md)
- [Configuration](docs/CONFIGURATION.md)
- [API Reference](docs/API.md)
- [Preset Effects](docs/PRESET_EFFECTS.md)
- [Fish Safety Guidelines](docs/SAFETY_GUIDELINES.md)

For wiring diagrams, troubleshooting, and advanced usage, see the [docs](docs/) folder.
