# Installation

## Prerequisites
- ESP32 or ESP8266 board
- Addressable LED strip (WS2812B, SK6812, APA102)
- PlatformIO (VS Code extension or CLI)
- 5V power supply

## Steps
1. Install PlatformIO in VS Code or via CLI
2. Clone the DeepGlow repository
3. Build firmware for your board
4. Upload filesystem (web interface)
5. Upload firmware
6. Connect to device AP and configure WiFi

## Quick Commands
```bash
pip install platformio
# Clone repository
git clone <your-repo-url>
cd DeepGlow
# Build for ESP32
aio run -e esp32d_debug
# Upload filesystem
pio run -t uploadfs -e esp32d_debug
# Upload firmware
pio run -t upload -e esp32d_debug
```

## Initial Setup
- Power on device
- Connect to AP "AquariumLED"
- Open browser to 192.168.4.1
- Configure WiFi and timezone
