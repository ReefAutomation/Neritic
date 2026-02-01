# API Documentation

## Overview

DeepGlow provides a RESTful JSON API and WebSocket interface for real-time control and monitoring.

**Value Ranges & Conversion:**
- All brightness, speed, and intensity values are always 0–100 percent in API and config files. Conversion to 0–255 for hardware is handled internally.
- Colors are always 24-bit RGB hex strings (e.g., "#FF00FF") or integers (0–16777215).


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
  "brightness": 80,
  "effect": 1,
  "preset": 2,
  "transitionTime": 5000,
  "time": "14:32:05",
  "sunrise": "06:23",
  "sunset": "18:45",
  "params": {
    "speed": 30,
    "intensity": 50,
    "colors": ["#FF0000", "#00FF00"]
  }
}
```

**Fields:**
- `power` (boolean): System power state
- `brightness` (0–100): Current brightness (percent)
- `effect` (integer): Current effect ID
- `preset` (integer): Active preset ID
- `transitionTime` (ms): Transition duration
- `time` (string): Current time (HH:MM:SS)
- `sunrise` (string): Calculated sunrise time
- `sunset` (string): Calculated sunset time
- `params` (object): Effect parameters
  - `speed` (0–100): Effect speed (percent)
  - `intensity` (0–100): Effect intensity (percent)
  - `colors` (array): Array of color hex strings

#### POST /api/state

Update system state with safety enforcement.

**Request Body:**
```json
{
  "power": true,
  "brightness": 80,
  "effect": 2,
  "transitionTime": 10000,
  "params": {
    "speed": 60,
    "intensity": 40,
    "colors": ["#FF0000", "#0000FF"]
  }
}
```

**Response** (200 OK):
```json
{
  "success": true
}
```

**Safety Notes:**
- Brightness capped at configured maximum (percent)
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
      "name": "Off",
      "effect": 0,
      "enabled": true,
      "params": {
        "speed": 30,
        "colors": ["#000000"]
      }
    },
    {
      "id": 1,
      "name": "Sunrise",
      "effect": 1,
      "enabled": true,
      "params": {
        "speed": 30,
        "colors": ["#FF0F00", "#FF5500", "#FFA000"]
      }
    }
    // ...
  ]
}
```
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
  "id": 3,
  "name": "Custom Sunset",
  "effect": 2,
  "enabled": true,
  "params": {
    "speed": 60,
    "intensity": 40,
    "colors": ["#FF0050", "#B400FF", "#280078"]
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
  "network": {
    "ssid": "",
    "hostname": "AquariumLED",
    "apPassword": ""
  },
  "led": {
    "pin": 13,
    "count": 34,
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
    "timezone": "Etc/UTC",
    "latitude": 0.0,
    "longitude": 0.0,
    "dstEnabled": true
  },
  "transitionTimes": {
    "powerOn": 60000,
    "schedule": 3600000,
    "manual": 10000,
    "effect": 10000
  },
  "timers": [
    {
      "enabled": true,
      "type": 0,
      "hour": 0,
      "minute": 0,
      "presetId": 0,
      "brightness": 0
    }
    // ...
  ]
}
```

#### POST /api/config

Update configuration.

**Request Body:**
```json
{
  "led": {
    "count": 120
  },
  "safety": {
    "maxBrightness": 90,
    "minTransitionTime": 8000
  },
  "time": {
    "timezone": "America/Los_Angeles",
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

**Notes:**
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
      "hour": 7,
      "minute": 0,
      "presetId": 1,
      "brightness": 60
    },
    {
      "id": 1,
      "enabled": true,
      "type": 0,
      "hour": 11,
      "minute": 0,
      "presetId": 2,
      "brightness": 100
    }
    // ...
  ]
}
```

**Timer Fields:**
- `id` (integer): Timer identifier
- `enabled` (boolean): Timer active state
- `type` (integer): Timer type
  - 0: Regular (specific time)
  - 1: Sunrise-based
  - 2: Sunset-based
- `hour` (0–23): Hour (regular timers)
- `minute` (0–59): Minute (regular timers)
- `presetId` (integer): Preset to apply
- `brightness` (0–100): Timer brightness (percent)

#### POST /api/timer

Update a timer.

**Request Body:**
```json
{
  "id": 2,
  "enabled": true,
  "type": 0,
  "hour": 19,
  "minute": 0,
  "presetId": 3,
  "brightness": 60
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

Colors are 24-bit RGB hex strings ("#RRGGBB") or integers (0–16777215).

**Examples:**
- Red: "#FF0000"
- Green: "#00FF00"
- Blue: "#0000FF"
- White: "#FFFFFF"
- Cyan: "#00FFFF"
- Magenta: "#FF00FF"
- Yellow: "#FFFF00"

**Conversion:**
```javascript
// Hex string to integer
const color = parseInt("FF00FF", 16); // 16711935

// RGB to hex string
const hex = '#' + ((r << 16) | (g << 8) | b).toString(16).padStart(6, '0');
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
  "error": "Internal server error"
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
