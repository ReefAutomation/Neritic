# Quick Start (10-15 minutes)

This guide gets a new board running quickly with safe defaults.

## Hardware checklist

- ESP32-family board
- Addressable LED strip (WS2812B/SK6812/APA102)
- 5V power supply
- Shared ground between board and LED supply
- 470Ω resistor on data line

## Flash and boot

```bash
cd Neritic

# Build + upload filesystem + upload firmware
pio run -e esp32d_debug
pio run -e esp32d_debug -t uploadfs
pio run -e esp32d_debug -t upload

# Open monitor
pio device monitor -b 115200
```

## Initial web setup

1. Join Wi-Fi network `Neritic`
2. Browse to `http://192.168.4.1`
3. Configure:
   - Home Wi-Fi SSID/password
   - Timezone
   - LED count/type
4. Save and wait for reboot

## Verify operation

1. Open device UI by local IP or mDNS hostname
2. Toggle power and change brightness
3. Apply `Sunrise` preset
4. Confirm transitions are smooth and no flicker

## Recommended first safety settings

- `maxBrightness`: `70-80`
- `minTransitionTime`: `5000` ms or more
- Avoid fast flashing effects during fish acclimation

## HomeKit (optional)

Bridge/HomeKit mapping checklist:

- [ ] Enable bridge mode in config (`homekit.enabled: true`)
- [ ] Give the controller a stable LAN address (DHCP reservation)
- [ ] `GET /api/homekit` returns the expected metadata
- [ ] `POST /api/state` with `{"power":true}` and `{"brightness":60}` works
- [ ] Map the bridge plugin actions to `POST /api/state` (`power`, `brightness`)

Full guide: [HOMEKIT.md](HOMEKIT.md)

## Next

- Fine tune configuration: [CONFIGURATION.md](CONFIGURATION.md)
- Connect API clients: [API.md](API.md)
- Set up HomeKit bridging: [HOMEKIT.md](HOMEKIT.md)
- If anything fails: [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
