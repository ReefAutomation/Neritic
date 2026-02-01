// Utility functions for Home and App

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
  if (val === 0) return "0s";
  if (val < 60) return val + "s";
  if (val < 3600) return Math.round(val / 60) + "m";
  return Math.round(val / 3600) + "h";
}

export function rgbwHexToPreview(hex) {
  // Accepts #RRGGBB or #RRGGBBWW
  if (!hex || typeof hex !== "string") return "#000";
  if (hex.length === 9) {
    // #RRGGBBWW, add white channel to each RGB
    const r = Number.parseInt(hex.slice(1, 3), 16);
    const g = Number.parseInt(hex.slice(3, 5), 16);
    const b = Number.parseInt(hex.slice(5, 7), 16);
    const w = Number.parseInt(hex.slice(7, 9), 16);
    const rr = Math.min(255, r + w);
    const gg = Math.min(255, g + w);
    const bb = Math.min(255, b + w);
    return `rgb(${rr},${gg},${bb})`;
  }
  return hex.slice(0, 7);
}
