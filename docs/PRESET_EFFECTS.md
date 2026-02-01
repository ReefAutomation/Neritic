# Preset Effects

Presets define lighting scenes using effect IDs and color arrays.

**Value Ranges:**
- All speed, intensity, and brightness values are 0–100 percent in presets.json.
- Colors are always 24-bit RGB hex strings (e.g., "#FF0F00").

**Color Array Example:**
```json
["#FF0F00", "#FF5500", "#FFA000"]
```

## Actual Presets (from presets.json)
| Preset Name        | Effect ID | Effect Name   | Colors                       |
|--------------------|-----------|--------------|------------------------------|
| Off                | 0         | Solid        | ["#000000"]                 |
| Sunrise            | 1         | Sunrise      | ["#FF0F00", "#FF5500", "#FFA000"] |
| Daylight           | 0         | Solid        | ["#000000FF"]               |
| Sunset             | 2         | Sunset       | ["#FF0050", "#B400FF", "#280078"] |
| Moonlight          | 3         | Moonlight    | ["#143C5C", "#FFFFFFFF"]   |
| Lightning Storm    | 4         | Lightning    | ["#0A1832", "#FFFFFFFF"]   |

## Effect Registry (from code)
| Effect ID | Effect Name   |
|-----------|--------------|
| 0         | Solid        |
| 1         | Sunrise      |
| 2         | Sunset       |
| 3         | Moonlight    |
| 4         | Lightning    |

**Notes:**
- Preset effect IDs map directly to the effect registry above.
- Color arrays are used for palette-based effects (Sunrise, Sunset, etc.).
- Speed and intensity are optional and default to 30/50 if not specified.
