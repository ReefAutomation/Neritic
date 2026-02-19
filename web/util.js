// Utility functions for Home and App

// Centralized: Convert RGBW values to display color (returns [r,g,b])
export function rgbwToDisplayColor(r, g, b, w) {
  // If input is pure white (r=g=b, w>0, and r in 0..255), force white
  if (r === g && g === b && w > 0 && r >= 0 && r <= 255) {
    return [255, 255, 255];
  }
  // For all other colors, blend W into RGB
  return [Math.min(255, r + w), Math.min(255, g + w), Math.min(255, b + w)];
}

// 0-59 = seconds, 60-119 = minutes, 120-127 = hours
export function steppedTransitionValue(val) {
  const v = Number(val);
  if (v <= 59) return v; // seconds
  if (v <= 119) return (v - 59) * 60; // minutes
  if (v <= 127) return (v - 119) * 3600; // hours
  return 28800; // max 8h
}

export function formatTransitionTime(val) {
  val = Number(val);
  if (val === 0) return '0s';
  if (val < 60) return `${val}s`;
  if (val < 3600) return `${Math.round(val / 60)}m`;
  return `${Math.round(val / 3600)}h`;
}

// Sort timers: sunrise/sunset types (1,2) first, then by time.
export function sortTimers(timers) {
  return [...timers].sort((a, b) => {
    // Sunrise and Sunset types (1,2) always at the top
    if (a.type === 1 || a.type === 2) return -1;
    if (b.type === 1 || b.type === 2) return 1;
    // Otherwise, sort by hour then minute
    return (
      (a.hour ?? 0) * 60 +
      (a.minute ?? 0) -
      ((b.hour ?? 0) * 60 + (b.minute ?? 0))
    );
  });
}

export function rgbwHexToPreview(hex) {
  // Accepts #RRGGBB or #RRGGBBWW
  if (!hex || typeof hex !== 'string') return '#000';
  if (hex.length === 9) {
    // #RRGGBBWW, use centralized logic
    const r = Number.parseInt(hex.slice(1, 3), 16);
    const g = Number.parseInt(hex.slice(3, 5), 16);
    const b = Number.parseInt(hex.slice(5, 7), 16);
    const w = Number.parseInt(hex.slice(7, 9), 16);
    const [rr, gg, bb] = rgbwToDisplayColor(r, g, b, w);
    return `rgb(${rr},${gg},${bb})`;
  }
  return hex.slice(0, 7);
}
