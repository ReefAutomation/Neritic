# Preset Effects

Presets are stored in `defaults/presets.json` and exposed via `/api/presets`.

## Value conventions

- `effect`: numeric effect ID
- `speed`, `intensity`: generally consumed as percentages (`0-100`) in API/UI
- `colors`: hex strings (`#RRGGBB` or effect-specific variants)

## Default presets

| ID | Name            | Effect | Notes |
|----|-----------------|--------|-------|
| 0  | Off             | 0      | Solid off color |
| 1  | Sunrise         | 1      | Warm multi-color ramp |
| 2  | Daylight        | 0      | White daylight scene |
| 3  | Sunset          | 2      | Orange/magenta dusk palette |
| 4  | Moonlight       | 3      | Low-intensity cool tones |
| 5  | Lightning Storm | 4      | Diffuse blue lightning over deep-water glow |
| 6  | Kelp Forest     | 5      | Swaying greens and teal |
| 7  | Coral Reef      | 6      | Tropical palette with moving caustics |
| 8  | Bioluminescent Bay | 7   | Pulsing cyan plankton over deep blue |
| 9  | Tidal Surge     | 8      | Flowing coastal colors and foam highlights |

## Effect IDs

| ID | Effect |
|----|--------|
| 0  | Solid |
| 1  | Sunrise |
| 2  | Sunset |
| 3  | Moonlight |
| 4  | Lightning |
| 5  | Kelp Forest |
| 6  | Coral Reef |
| 7  | Bioluminescence |
| 8  | Tidal Surge |

## Updating a preset

Use `POST /api/preset` with `id` and full preset fields:

```json
{
  "id": 3,
  "name": "Sunset",
  "effect": 2,
  "enabled": true,
  "params": {
    "speed": 25,
    "intensity": 45,
    "colors": ["#FF0050", "#B400FF", "#280078"]
  }
}
```

Apply an existing preset immediately:

```json
{
  "id": 3,
  "apply": true
}
```
