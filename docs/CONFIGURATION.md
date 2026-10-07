# Configuration Reference

Neritic stores runtime settings as JSON. Most users should configure via web UI, but API and file-level flows use the same structure.

## Value conventions

- Brightness, speed, intensity in API/config are percent values (`0-100`)
- Firmware converts these to internal `0-255` values
- Times are in milliseconds unless noted

## Example baseline config

```json
{
  "network": {
    "ssid": "",
    "password": "",
    "hostname": "Neritic",
    "apPassword": ""
  },
  "led": {
    "pin": 14,
    "count": 38,
    "type": "SK6812",
    "colorOrder": "GRB",
    "relayPin": 2,
    "relayActiveHigh": true
  },
  "safety": {
    "maxBrightness": 80,
    "minTransitionTime": 5000
  },
  "time": {
    "ntpServer": "pool.ntp.org",
    "timezone": "America/Santiago",
    "latitude": 0.0,
    "longitude": 0.0
  },
  "transitionTimes": {
    "powerOn": 60000,
    "schedule": 10800000,
    "manual": 10000,
    "effect": 10000
  }
}
```

## Sections

### `network`
- `ssid`, `password`: station credentials
- `hostname`: mDNS/host label
- `apPassword`: AP password (empty means default behavior)

### `led`
- `pin`: LED data pin
- `count`: number of LEDs
- `type`: LED chipset name
- `colorOrder`: e.g. `GRB`, `RGB`
- `relayPin`, `relayActiveHigh`: optional relay control

### `safety`
- `maxBrightness`: hard cap for manual/API requests
- `minTransitionTime`: minimum allowed transition duration

### `time`
- `ntpServer`: NTP host
- `timezone`: IANA zone name from `defaults/timezones.json`; its POSIX rule (offset and DST) is applied automatically
- `latitude`, `longitude`: used by sunrise/sunset logic

### `transitionTimes`
- `powerOn`: transition duration used on power changes; power-on fades up the
  center LED(s), then the end LEDs, then progressively lights the midpoints of
  the remaining gaps until the strip is full. Power-off fades from the ends
  inward. The same spatial transition applies when brightness changes to/from
  0% or a preset changes to/from a fully black Off effect.
- `schedule`: timer-driven transition
- `manual`: user/API transition
- `effect`: effect change transition

### `timers`
Each timer includes:
- `enabled`
- `type` (`0` regular, `1` sunrise, `2` sunset)
- `hour`, `minute`
- `presetId`
- `brightness` (percent)

## Applying configuration via API

`POST /api/config` accepts partial updates:

```json
{
  "safety": {
    "maxBrightness": 75,
    "minTransitionTime": 7000
  },
  "time": {
    "timezone": "America/New_York"
  }
}
```
