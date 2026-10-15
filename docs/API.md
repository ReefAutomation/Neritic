# API Reference

Base URL: `http://<device-ip>`

- REST: `/api/*`
- WebSocket: `/ws`

## Conventions

- Request/response body format: JSON (unless noted)
- CORS preflight is supported for key POST endpoints
- Most write endpoints return `{ "success": true }` on success

## REST Endpoints

### Firmware and OTA

- `GET /api/version`
  - Returns firmware version:
  ```json
  { "version": "v1.0.0" }
  ```
  - Version strings are canonical Git tag strings and always carry the `v`
    prefix:
    - `vX.Y.Z` — release build: HEAD is the released tag. A release never
      carries a stamp, even with uncommitted changes in the tree.
    - `vX.Y.Z-YYYYmmdd-HHMMSS` — test build: HEAD is not a released tag. The
      stamp is UTC.
    The same string is reported by `GET /api/update` as `current` and as the
    `version` mDNS TXT record, so it compares directly with the `version` field
    of the release manifest.

- `GET /api/update`
  - Returns `current` (the version above) and the remote release manifest:
  ```json
  { "current": "v1.0.0", "latest": [ { "env": "esp32c6", "version": "v1.0.0", "url": "…" } ] }
  ```

- `POST /api/update`
  - Starts remote OTA process.

- `POST /ota`
  - Local OTA binary upload endpoint.

### Device command

- `POST /api/command`
  - Currently recognized command:
    - `reboot`
  - Example:
  ```json
  { "command": "reboot" }
  ```

### State

- `GET /api/state`
  - Returns current runtime state (power, brightness, effect, params, time info).

- `POST /api/state`
  - Partial state update.
  - Example:
  ```json
  {
    "power": true,
    "brightness": 65,
    "transitionTime": 8000,
    "effect": 2,
    "params": {
      "speed": 40,
      "intensity": 35,
      "colors": ["#FF5500", "#280078"]
    }
  }
  ```

### Effects and presets

- `GET /api/effects`
  - Returns effect registry metadata.

- `GET /api/presets`
  - Returns saved presets.

- `POST /api/preset`
  - Apply preset:
  ```json
  { "id": 2, "apply": true }
  ```
  - Save preset definition:
  ```json
  {
    "id": 2,
    "name": "Daylight",
    "effect": 0,
    "enabled": true,
    "params": {
      "speed": 30,
      "intensity": 50,
      "colors": ["#FFFFFFFF"]
    }
  }
  ```

### Configuration and timer

- `GET /api/config`
  - Returns full persisted config.

- `POST /api/config`
  - Partial config update.

- `POST /api/timer`
  - Update one timer by `id`.
  - Example:
  ```json
  {
    "id": 1,
    "enabled": true,
    "type": 0,
    "hour": 7,
    "minute": 30,
    "presetId": 1,
    "brightness": 60
  }
  ```

- `POST /api/factory_reset`
  - Resets config and reboots.

- `GET /api/timezones`
  - Returns the supported timezone list, each entry carrying the GMT offset the
    device currently has in effect for that zone:

    ```json
    [
      { "name": "America/Santiago", "gmt": "GMT -3" },
      { "name": "Europe/Berlin", "gmt": "GMT +2" }
    ]
    ```

  - `gmt` is empty until the device has synchronized its clock, since the
    offset depends on the current date; daylight saving is applied
    automatically from the zone's rules.
  - The list is loaded from the runtime timezone data (`/data/timezones.json`
    on the filesystem) when present, otherwise from the embedded bootstrap list.

## WebSocket

Endpoint: `ws://<device-ip>/ws`

Behavior:
- Server broadcasts state updates
- Heartbeat/periodic updates are sent by firmware

Typical message shape mirrors `GET /api/state` output.

## Error behavior

Common failures:
- `400` invalid JSON or invalid IDs
- `409` OTA already in progress
- `500` internal save/reset failures
- `502` remote manifest unavailable for `GET /api/update`
