# HomeKit Bridge Integration

DeepGlow supports HomeKit through a bridge workflow (for example Homebridge or Home Assistant HomeKit Bridge).

This mode uses DeepGlow HTTP endpoints and exposes metadata at `/api/homekit`.

---

## 1) Enable HomeKit Bridge Mode

Set these fields in your config:

```json
{
  "homekit": {
    "enabled": true,
    "bridgeMode": "homebridge-http",
    "accessoryName": "DeepGlow",
    "setupCode": "031-45-154",
    "setupId": "DG01"
  }
}
```

You can update this via `POST /api/config` or by editing persisted config.

Bridge mode options:

- `homebridge-http` (default): bridge/API mode
- `hybrid`: enables native HomeKit scaffold hooks plus bridge mode
- `native`: enables native HomeKit scaffold hooks only (direct pairing still not implemented)

Build-time scaffold flag:

- `ENABLE_NATIVE_HOMEKIT=1` (see `esp32d_native` / `esp32d_native_debug` environments in `platformio.ini`)

---

## 2) DeepGlow Endpoints for HomeKit Mapping

- Metadata: `GET /api/homekit`
- QR image endpoint: `GET /api/homekit/qr`
- State read: `GET /api/state`
- State write: `POST /api/state`

Bridge mode metadata includes pairing identifiers, but direct Home app scan is
not supported by DeepGlow firmware itself.

Scaffold metadata fields:

- `nativeScaffoldCompiled`: true when firmware is built with `ENABLE_NATIVE_HOMEKIT`
- `nativeScaffoldStatus`: one of `not-compiled`, `disabled`, `bridge-mode`, `pending`, `adapter-ready:*`

Adapter contract (phase 2 scaffold):

- Inbound characteristic mapping hooks:
  - `nativeHomeKitApplyPowerFromAccessory(bool)`
  - `nativeHomeKitApplyBrightnessFromAccessory(uint8_t percent)`
- Outbound state callback registration:
  - `nativeHomeKitSetBackendStateCallback(...)`

These APIs allow a future HAP backend to map HomeKit characteristics to DeepGlow
state without changing existing UI/API behavior.

Current backend implementation is a stub and reports status suffixes like:

- `adapter-ready:stub-active`
- `adapter-ready:stub-missing-callbacks`

Payloads:

- Power on: `{"power":true}`
- Power off: `{"power":false}`
- Brightness: `{"brightness":0..100}`

---

## 3) Validate Before Pairing

Replace `<device-ip>` with your controller address.

```bash
curl http://<device-ip>/api/homekit
curl http://<device-ip>/api/state
curl -X POST http://<device-ip>/api/state -H "Content-Type: application/json" -d '{"power":true}'
curl -X POST http://<device-ip>/api/state -H "Content-Type: application/json" -d '{"brightness":60}'
```

Expected:

- `GET /api/homekit` returns bridge metadata and endpoint hints.
- `GET /api/homekit` confirms bridge mode status and endpoint hints.
- `POST /api/state` returns `{"success":true}`.

Pairing flow:

1. Pair your Homebridge/Home Assistant bridge in Apple Home.
2. Let the bridge expose DeepGlow as a HomeKit light accessory.
3. Use DeepGlow API mapping (`/api/state`) inside the bridge plugin config.

---

## 4) Homebridge Mapping (Generic)

For any HTTP-capable Homebridge plugin, map these actions:

- `On` action
  - Method: `POST`
  - URL: `http://<device-ip>/api/state`
  - Body: `{"power":true}`
- `Off` action
  - Method: `POST`
  - URL: `http://<device-ip>/api/state`
  - Body: `{"power":false}`
- `Set Brightness` action
  - Method: `POST`
  - URL: `http://<device-ip>/api/state`
  - Body: `{"brightness":<value-0-100>}`
- `Get State` action
  - Method: `GET`
  - URL: `http://<device-ip>/api/state`

---

## 5) Example JSON (HTTP Lightbulb-style Plugin)

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

---

## Notes

- This is bridge-mode compatibility, not native HAP accessory firmware.
- Keep your controller on a stable LAN IP or DHCP reservation for reliable HomeKit behavior.
- If your plugin uses different field names, keep the DeepGlow endpoint contract unchanged.
