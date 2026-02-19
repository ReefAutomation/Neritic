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
  { "version": "1.0.0" }
  ```

- `GET /api/update`
  - Returns current version and remote manifest information.

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
  - Returns supported timezone list.

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
