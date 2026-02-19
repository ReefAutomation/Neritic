# Troubleshooting

## 1) Build or upload fails

### `pio: command not found`

- Install PlatformIO CLI and restart terminal.

### Upload fails / port not found

- Check USB cable/data support
- Verify serial device path
- Close any app locking the serial port
- Retry with explicit port:

```bash
pio run -e esp32d_debug -t upload --upload-port /dev/cu.usbserial-XXXX
```

## 2) Web UI not loading

- Re-upload filesystem:

```bash
pio run -e esp32d_debug -t uploadfs
```

- Confirm device responds at `http://192.168.4.1` in AP mode
- After Wi-Fi setup, use device IP from serial logs

## 3) LED strip is dark or flickering

- Verify common ground
- Verify DIN direction (not DOUT)
- Add/confirm 470Ω data resistor
- Check PSU current capacity
- Lower brightness temporarily

## 4) Timers do not trigger

- Confirm NTP/timezone settings in config
- Confirm timer is enabled and has valid `presetId`
- Ensure device has network access for time sync

## 5) API calls return errors

- `400`: malformed JSON or invalid ID
- `409`: OTA already in progress
- `500`: config save/reset failure

## Useful diagnostics

```bash
# Build
pio run -e esp32d_debug

# Monitor serial output
pio device monitor -b 115200

# Query state
curl -s http://<device-ip>/api/state | jq

# Query config
curl -s http://<device-ip>/api/config | jq
```
