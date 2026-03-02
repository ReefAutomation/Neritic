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

### `homekit`
```json
{
  "homekit": {
    "enabled": false,
    "bridgeMode": "homebridge-http",
    "accessoryName": "Neritic",
    "setupCode": "031-45-154",
    "setupId": "DG01"
  }
}
```
- `enabled`: turns bridge metadata mode on/off
- `bridgeMode`: profile name used by integrations (`homebridge-http`, `hybrid`,
  or `native`)
- `accessoryName`: friendly name shown in the bridge/HomeKit
- `setupCode`, `setupId`: values exposed by `/api/homekit` for bridge workflows

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

Daylight saving is always applied automatically from the selected zone's rules
(`tz` in `defaults/timezones.json`); there is no setting to disable it.

The timezone database is `defaults/timezones.json`. It is a build artifact:
`scripts/gen_timezones.py` generates every entry's `offset` and `tz` field from
the IANA database (pinned in `requirements.txt`), driven by the `ZONE_NAMES`
list in that script — the only hand-maintained input. PlatformIO runs that same
script as a pre-build step, so the file is regenerated before the firmware
embeds it and does not need to exist beforehand; run
`python scripts/gen_timezones.py` to regenerate it by hand, or
`python scripts/gen_timezones.py --validate` to check the committed copy.

Generation requires `pip install -r requirements.txt` in the environment that
runs the build; without it the build fails rather than embedding stale rules.
The firmware ships the embedded copy as a bootstrap fallback and fetches a fresh
copy from GitHub once the network is up, so rule changes take effect without a
firmware update (see `.github/workflows/timezones.yml`).

### `transitionTimes`
- `powerOn`: transition duration used on power changes; the total time spans
  from the first LED turning on to the end of the brightness ramp. The cascade
  is driven by the ramp's brightness value rather than a separate timer:
  power-on lights the center LED(s) first, then the end LEDs, then
  progressively the midpoints of the remaining gaps — each at a fixed 1%
  brightness (no ramping). Once the ramp reaches a small floor (roughly 3%
  for a 34-LED strip, scaled by LED count), every LED is on and the strip
  ramps from that floor to the target brightness. Power-off reverses this: the
  strip ramps down to the floor first, then the LEDs fade out one-by-one in
  reverse order.
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
