# API Documentation

## Overview

DeepGlow provides a RESTful JSON API and WebSocket interface for real-time control and monitoring.

**Base URL**: `http://[device-ip]/api`

**WebSocket**: `ws://[device-ip]/ws`

## Authentication

Currently, no authentication is required. Future versions may add optional password protection.

## Rate Limiting

No rate limiting is enforced. However, excessive API calls may affect LED performance.

---

## REST API Endpoints

### State Management

#### GET /api/state

Get current system state.

**Response** (200 OK):
```json
{
  "power": true,
  "brightness": 180,
  "effect": 1,
  "transitionTime": 5000,
  "currentPreset": 2,
  "time": "14:32:05",
  "sunrise": "06:23",
  "sunset": "18:45",
  "params": {
    "speed": 128,
    "intensity": 150,
    "color1": 65535,
    "color2": 16776960
  }
}
```

**Fields**:
- `power` (boolean): System power state
- `brightness` (0-255): Current brightness level
- `effect` (0-6): Current effect ID
- `transitionTime` (ms): Default transition duration
- `currentPreset` (0-15): Active preset ID
- `time` (string): Current time (HH:MM:SS)
- `sunrise` (string): Calculated sunrise time
- `sunset` (string): Calculated sunset time
- `params` (object): Current effect parameters

#### POST /api/state

Update system state with safety enforcement.

**Request Body**:
```json
{
  "power": true,
  "brightness": 200,
  "effect": 2,
  "transitionTime": 10000,
  "params": {
    "speed": 150,
    "intensity": 200,
    "color1": 16711680,
    "color2": 65280
  }
}
```

**Response** (200 OK):
```json
{
  "success": true
}
```

**Safety Notes**:
- Brightness capped at configured maximum
- Transition time enforced minimum
- Invalid values rejected with 400 error

---

### Preset Management

#### GET /api/presets

Get all configured presets.

**Response** (200 OK):
```json
{
  "presets": [
    {
      "id": 0,
      "name": "Morning Sun",
      "brightness": 180,
      "effect": 3,
      "enabled": true,
      "params": {
        "speed": 80,
        "intensity": 200,
        "color1": 16744448,
        "color2": 16776960
      }
    },
    {
      "id": 1,
      "name": "Daylight",
      "brightness": 200,
      "effect": 0,
      "enabled": true,
      "params": {
        "speed": 128,
        "intensity": 128,
        "color1": 16777215,
        "color2": 16777215
      }
    }
  ]
}
```

#### POST /api/preset

Apply or save a preset.

**Apply Preset Request**:
```json
{
  "id": 2,
  "apply": true
}
```

**Save Preset Request**:
```json
{
  "id": 7,
  "name": "Custom Sunset",
  "brightness": 120,
  "effect": 2,
  "enabled": true,
  "params": {
    "speed": 60,
    "intensity": 150,
    "color1": 16744192,
    "color2": 16711935
  }
}
```

**Response** (200 OK):
```json
{
  "success": true
}
```

---

### Configuration

#### GET /api/config

Get system configuration.

**Response** (200 OK):
```json
{
  "led": {
    "pin": 2,
    "count": 60,
    "type": "WS2812B"
  },
  "safety": {
    "minTransitionTime": 5000,
    "maxBrightness": 200
  },
  "time": {
    "ntpServer": "pool.ntp.org",
    "timezoneOffset": -5,
    "latitude": 40.7128,
    "longitude": -74.0060,
    "dstEnabled": true
  }
}
```

#### POST /api/config

Update configuration.

**Request Body**:
```json
{
  "led": {
    "count": 120
  },
  "safety": {
    "maxBrightness": 180,
    "minTransitionTime": 8000
  },
  "time": {
    "timezoneOffset": -8,
    "latitude": 47.6062,
    "longitude": -122.3321
  }
}
```

**Response** (200 OK):
```json
{
  "success": true
}
```

**Notes**:
- Some changes require reboot (LED pin, type)
- Configuration persisted to flash
- Invalid values rejected

---

### Timer Management

#### GET /api/timers

Get all timers.

**Response** (200 OK):
```json
{
  "timers": [
    {
      "id": 0,
      "enabled": true,
      "type": 0,
      "hour": 8,
      "minute": 0,
      "days": 127,
      "offset": 0,
      "presetId": 0
    },
    {
      "id": 1,
      "enabled": true,
      "type": 1,
      "hour": 0,
      "minute": 0,
      "days": 127,
      "offset": 30,
      "presetId": 0
    }
  ]
}
```

**Timer Fields**:
- `id` (0-9): Timer identifier
- `enabled` (boolean): Timer active state
- `type` (0-2): Timer type
  - 0: Regular (specific time)
  - 1: Sunrise-based
  - 2: Sunset-based
- `hour` (0-23): Hour (regular timers)
- `minute` (0-59): Minute (regular timers)
- `days` (0-127): Bitmask of active days
  - Bit 0 = Sunday, Bit 6 = Saturday
  - 127 = all days (0b1111111)
  - 62 = weekdays (0b0111110)
- `offset` (-720 to 720): Minutes offset (sun timers)
- `presetId` (0-15): Preset to apply

#### POST /api/timer

Update a timer.

**Request Body**:
```json
{
  "id": 2,
  "enabled": true,
  "type": 2,
  "offset": -30,
  "days": 127,
  "presetId": 3
}
```

**Response** (200 OK):
```json
{
  "success": true
}
```

---

## WebSocket Protocol

### Connection

Connect to: `ws://[device-ip]/ws`

**Connection Events**:
- `WS_EVT_CONNECT`: Client connected
- `WS_EVT_DISCONNECT`: Client disconnected

### Server → Client Messages

Server broadcasts state updates automatically:
- Every 2 seconds (heartbeat)
- On state changes
- On preset application
- On configuration updates

**Message Format** (JSON):
```json
{
  "power": true,
  "brightness": 180,
  "effect": 1,
  "transitionTime": 5000,
  "currentPreset": 2,
  "time": "14:32:05",
  "sunrise": "06:23",
  "sunset": "18:45",
  "params": {
    "speed": 128,
    "intensity": 150,
    "color1": 65535,
    "color2": 16776960
  }
}
```

### Client → Server Messages

Currently, WebSocket is read-only. Use REST API for commands.

---

## Effect IDs

| ID | Name | Description |
|----|------|-------------|
| 0 | Solid Color | Static single color |
| 1 | Aquarium Ripple | Water surface light ripples |
| 2 | Gentle Wave | Smooth underwater waves |
| 3 | Sunrise Simulation | Dawn to daylight transition |
| 4 | Coral Shimmer | Subtle reef twinkling |
| 5 | Deep Ocean | Dark blue pulsing |
| 6 | Moonlight | Ultra-dim night lighting |

---

## Color Format

Colors are 24-bit RGB integers (0x000000 to 0xFFFFFF).

**Examples**:
- Red: `0xFF0000` (16711680)
- Green: `0x00FF00` (65280)
- Blue: `0x0000FF` (255)
- White: `0xFFFFFF` (16777215)
- Cyan: `0x00FFFF` (65535)
- Magenta: `0xFF00FF` (16711935)
- Yellow: `0xFFFF00` (16776960)

**Conversion**:
```javascript
// Hex to integer
const color = parseInt("00FFFF", 16); // 65535

// RGB to integer
const color = (r << 16) | (g << 8) | b;

// Integer to hex
const hex = color.toString(16).padStart(6, '0');
```

---

## Error Responses

### 400 Bad Request
Invalid JSON or parameter values.

```json
{
  "error": "Invalid JSON"
}
```

### 404 Not Found
Endpoint doesn't exist.

```json
{
  "error": "Not Found"
}
```

### 500 Internal Server Error
Server-side error.

```json
{
  "error": "Internal Error"
}
```

---

## Example Usage

### Python

```python
import requests

BASE_URL = "http://aquariumled.local/api"

# Get current state
response = requests.get(f"{BASE_URL}/state")
state = response.json()
print(f"Current brightness: {state['brightness']}")

# Set brightness
response = requests.post(f"{BASE_URL}/state", json={
    "brightness": 150,
    "transitionTime": 10000
})

# Apply preset
requests.post(f"{BASE_URL}/preset", json={
    "id": 3,
    "apply": True
})
```

### JavaScript

```javascript
const BASE_URL = "http://aquariumled.local/api";

// Get state
fetch(`${BASE_URL}/state`)
  .then(res => res.json())
  .then(state => console.log(state));

// Apply preset
fetch(`${BASE_URL}/preset`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ id: 2, apply: true })
});
```

### curl

```bash
# Get state
curl http://aquariumled.local/api/state

# Set effect
curl -X POST http://aquariumled.local/api/state \
  -H "Content-Type: application/json" \
  -d '{"effect": 1, "params": {"speed": 150}}'

# Apply preset
curl -X POST http://aquariumled.local/api/preset \
  -H "Content-Type: application/json" \
  -d '{"id": 4, "apply": true}'
```

---

## Integration Examples

### Home Assistant

```yaml
# configuration.yaml
rest_command:
  aquarium_preset:
    url: "http://aquariumled.local/api/preset"
    method: POST
    content_type: "application/json"
    payload: '{"id": {{ preset_id }}, "apply": true}'

automation:
  - alias: "Morning Aquarium"
    trigger:
      platform: sun
      event: sunrise
      offset: "+00:30:00"
    action:
      service: rest_command.aquarium_preset
      data:
        preset_id: 0
```

### Node-RED

```json
[{
    "id": "aquarium_flow",
    "type": "http request",
    "method": "POST",
    "url": "http://aquariumled.local/api/state",
    "payload": "{\"brightness\": 180}"
}]
```

---

## Rate Limits & Best Practices

1. **Avoid rapid updates**: Wait 500ms between API calls
2. **Use WebSocket**: Subscribe for state updates instead of polling
3. **Batch changes**: Combine multiple updates in single request
4. **Respect transitions**: Allow transitions to complete
5. **Error handling**: Always handle network errors gracefully

---

## Support

For API issues or feature requests:
- GitHub Issues: https://github.com/yourusername/DeepGlow/issues
- Documentation: https://github.com/yourusername/DeepGlow/wiki
