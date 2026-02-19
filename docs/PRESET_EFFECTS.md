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
| 5  | Lightning Storm | 4      | Dark base with white flashes |

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
