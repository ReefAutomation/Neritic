#pragma once
#include <cstdint>

// Packing/unpacking
inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

inline void unpack_rgb(uint32_t color, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = (color >> 16) & 0xFF;
  g = (color >> 8) & 0xFF;
  b = color & 0xFF;
}

inline uint32_t pack_rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)w;
}

inline void unpack_rgbw(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &w) {
  r = (color >> 24) & 0xFF;
  g = (color >> 16) & 0xFF;
  b = (color >> 8) & 0xFF;
  w = color & 0xFF;
}

// Parsing
inline uint32_t parse_hex_rgbw(const char* hexstr) {
  if (hexstr[0] == '#') ++hexstr;
  size_t len = strlen(hexstr);
  uint8_t r = 0, g = 0, b = 0, w = 0;
  if (len == 6) {
    sscanf(hexstr, "%2hhx%2hhx%2hhx", &r, &g, &b);
    w = 0;
  } else if (len == 8) {
    sscanf(hexstr, "%2hhx%2hhx%2hhx%2hhx", &r, &g, &b, &w);
  }
  return pack_rgbw(r, g, b, w);
}

// Math
inline void scale_rgbw_brightness(uint8_t in_r, uint8_t in_g, uint8_t in_b, uint8_t in_w, uint8_t brightness, uint8_t &out_r, uint8_t &out_g, uint8_t &out_b, uint8_t &out_w) {
  out_r = (uint8_t)ceilf((float)in_r * brightness / 255.0f);
  out_g = (uint8_t)ceilf((float)in_g * brightness / 255.0f);
  out_b = (uint8_t)ceilf((float)in_b * brightness / 255.0f);
  out_w = (uint8_t)ceilf((float)in_w * brightness / 255.0f);
}
inline void blend_rgbw_brightness(uint32_t c0, uint32_t c1, float frac, uint8_t brightness, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) {
  r = (uint8_t)ceilf(((((c0 >> 24) & 0xFF) * (1.0f - frac) + ((c1 >> 24) & 0xFF) * frac) * brightness) / 255.0f);
  g = (uint8_t)ceilf(((((c0 >> 16) & 0xFF) * (1.0f - frac) + ((c1 >> 16) & 0xFF) * frac) * brightness) / 255.0f);
  b = (uint8_t)ceilf(((((c0 >> 8) & 0xFF) * (1.0f - frac) + ((c1 >> 8) & 0xFF) * frac) * brightness) / 255.0f);
  w = (uint8_t)ceilf(((( (c0 & 0xFF) * (1.0f - frac) + (c1 & 0xFF) * frac) * brightness) / 255.0f));
}
