# Safety Guidelines

These recommendations prioritize fish health and electrical safety.

## Lighting safety

- Avoid instant large brightness jumps
- Keep `minTransitionTime` at `5000 ms` or higher
- Start new tanks at reduced max brightness
- Prefer sunrise/sunset style effects over strobes

## Suggested brightness plan

- Day: `65-80%`
- Evening: `35-60%`
- Night/moonlight: `5-20%`

Acclimation example:
- Days 1-3: cap at `40%`
- Days 4-7: cap at `60%`
- Day 8+: increase gradually to target

## Electrical safety

- Use an adequate 5V PSU with headroom
- Keep controller and supply dry and ventilated
- Use common ground between ESP and LED supply
- Add inline data resistor (`~470Ω`)
- Add bulk capacitor (`~1000µF`) across LED power rails
- Use GFCI/RCD protection near aquariums

## Runtime safeguards to configure

- `safety.maxBrightness`
- `safety.minTransitionTime`
- conservative timer brightness values for early/late hours
