# Configuration

## WiFi
Edit in web interface or modify `config.json`:
```json
{
  "network": {
    "ssid": "YourWiFiName",
    "password": "YourPassword",
    "hostname": "AquariumLED",
    "apPassword": ""
  }
}
```

## LED
```json
{
  "led": {
    "pin": 13,
    "count": 34,
    "type": "SK6812",
    "colorOrder": "GRB",
    "relayPin": 2,
    "relayActiveHigh": true
  }
}
```

## Safety
```json
{
  "safety": {
    "maxBrightness": 80,
    "minTransitionTime": 5000
  }
}
```

## Time
```json
{
  "time": {
    "ntpServer": "pool.ntp.org",
    "timezone": "Etc/UTC",
    "latitude": 0.0,
    "longitude": 0.0,
    "dstEnabled": true
  }
}
```

## Transition Times
```json
{
  "transitionTimes": {
    "powerOn": 60000,
    "schedule": 3600000,
    "manual": 10000,
    "effect": 10000
  }
}
```

## Timers
```json
{
  "timers": [
    {
      "enabled": true,
      "type": 0,
      "hour": 0,
      "minute": 0,
      "presetId": 0,
      "brightness": 0
    },
    {
      "enabled": true,
      "type": 0,
      "hour": 7,
      "minute": 0,
      "presetId": 1,
      "brightness": 60
    }
  ]
}
```
