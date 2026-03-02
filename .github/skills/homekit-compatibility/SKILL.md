```skill
---
name: homekit-compatibility
description: Adds and validates HomeKit compatibility mode for DeepGlow using a bridge-friendly API contract (Homebridge/Home Assistant).
license: MIT
metadata:
  author: GitHub Copilot
  version: "1.0"
---

## HomeKit Compatibility Skill (Bridge Mode)

This skill enables HomeKit compatibility by exposing deterministic metadata and control endpoints that Homebridge or Home Assistant can map to a HomeKit Lightbulb accessory.

### Prerequisite

- Use the project Python environment for firmware commands:

```bash
source .venv/bin/activate && <command>
```

### Step 1: Enable HomeKit Bridge Mode in Config

1. Ensure config includes:
   - `homekit.enabled`
   - `homekit.bridgeMode` (default: `homebridge-http`)
   - `homekit.accessoryName`
   - `homekit.setupCode`
   - `homekit.setupId`
2. Keep defaults safe: `enabled=false`.

### Step 2: Expose HomeKit Metadata Endpoint

1. Add a route:
   - `GET /api/homekit`
2. Response must include:
   - Enabled state
   - Accessory metadata
   - Current power and brightness
   - Control endpoint hints (`/api/state`)

### Step 3: Keep State Sync Hooks

1. On power and brightness changes, publish/sync latest values to the HomeKit bridge module.
2. Keep sync non-blocking and safe in main loop.

### Step 4: Build Validation

Run:

```bash
source .venv/bin/activate && pio run -e esp32d_debug
```

### Step 5: Runtime Validation

After flashing:

1. `GET /api/homekit` returns JSON with metadata and endpoint hints.
2. `POST /api/state` with `{"power":true,"brightness":60}` changes LEDs and HomeKit state.

### Notes

- This skill implements HomeKit compatibility through a bridge contract to minimize firmware risk.
- Native HAP integration can be added later as a separate skill revision.

### Homebridge Snippet (Ready to Paste)

Use an HTTP Lightbulb-style plugin and point it to DeepGlow endpoints:

```json
{
   "accessories": [
      {
         "accessory": "HTTP-LIGHTBULB",
         "name": "DeepGlow",
         "onUrl": {
            "url": "http://192.168.1.50/api/state",
            "method": "POST",
            "headers": {
               "Content-Type": "application/json"
            },
            "body": "{\"power\":true}"
         },
         "offUrl": {
            "url": "http://192.168.1.50/api/state",
            "method": "POST",
            "headers": {
               "Content-Type": "application/json"
            },
            "body": "{\"power\":false}"
         },
         "setBrightnessUrl": {
            "url": "http://192.168.1.50/api/state",
            "method": "POST",
            "headers": {
               "Content-Type": "application/json"
            },
            "body": "{\"brightness\":%s}"
         }
      }
   ]
}
```

If your Homebridge plugin uses different field names, keep this exact API contract:
- `POST /api/state` with `{"power":true}`
- `POST /api/state` with `{"power":false}`
- `POST /api/state` with `{"brightness":0..100}`

### Generic Mapping (Plugin-agnostic)

For any Homebridge plugin that supports custom HTTP actions, map these requests:

- `On` → `POST http://<device-ip>/api/state` body `{"power":true}`
- `Off` → `POST http://<device-ip>/api/state` body `{"power":false}`
- `Set Brightness` → `POST http://<device-ip>/api/state` body `{"brightness":<value-0-100>}`
- `Get State` → `GET http://<device-ip>/api/state`

Validate first:

```bash
curl http://<device-ip>/api/homekit
curl -X POST http://<device-ip>/api/state -H "Content-Type: application/json" -d '{"power":true}'
curl -X POST http://<device-ip>/api/state -H "Content-Type: application/json" -d '{"brightness":60}'
```
