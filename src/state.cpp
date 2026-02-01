#include "state.h"
#include "bus_manager.h"
#include "colors.h"
#include "debug.h"
#include "effects.h"
#include "transition.h"
#include "webserver.h"
#include <stdint.h>

// --- Global variables ---
EffectParams transitionPrevParams;
TransitionEngine::PendingTransitionState pendingTransition;
extern volatile uint8_t g_effectSpeed;
SystemState state;
extern BusManager busManager;
std::vector<uint32_t> *g_outputFramePtr = nullptr;
#include <array>
std::array<uint32_t, 8> color = {0x000000, 0x000000, 0x000000, 0x000000,
                                 0x000000, 0x000000, 0x000000, 0x000000};
size_t colorCount = 2;
extern Configuration config;
extern Scheduler scheduler;
extern TransitionEngine transition;
extern WebServerManager webServer;
extern void *strip;
extern int8_t lastScheduledPreset;

// --- Public API ---
void applyPreset(uint8_t presetId, uint8_t brightness);
void setPower(bool power);
void setBrightness(uint8_t brightness);
void setEffect(uint8_t effect, const EffectParams &params);
void setUserColor(const uint32_t *newColor, size_t count);
void updateLEDs();

// --- Static/internal helpers ---
static bool hasValidPresetColors(const std::vector<String> &presetColorsVec);
static void captureCurrentBusFrame(std::vector<uint32_t> &frame);
static void
fillArrayFromPresetColors(const std::vector<String> &presetColorsVec,
                          std::array<uint32_t, 8> &arr);
static void setPendingTransitionFromPreset(const Preset &preset, size_t n);
static void captureCurrentFrameForTransition();
static void renderFrameToBus(const std::vector<uint32_t> &frame);
static void commitPendingTransition();
static void renderAnimationFrame(size_t count, uint8_t brightness);
static void handlePowerOff();
static void handleTransition(size_t count,
                             std::vector<uint32_t> &g_lastOutputFrame);
static void handleAnimation(size_t count,
                            std::vector<uint32_t> &g_lastOutputFrame);

// --- Implementation ---

// Static/internal helpers
static bool hasValidPresetColors(const std::vector<String> &presetColorsVec) {
  for (const auto &hex : presetColorsVec) {
    if (parse_hex_rgbw(hex.c_str()) == 0x00000000)
      return false;
  }
  return true;
}
static void captureCurrentBusFrame(std::vector<uint32_t> &frame) {
  size_t count = busManager.getPixelCount();
  frame.resize(count);
  for (size_t i = 0; i < count; ++i) {
    frame[i] = busManager.getPixelColor(i);
  }
}
static void
fillArrayFromPresetColors(const std::vector<String> &presetColorsVec,
                          std::array<uint32_t, 8> &arr) {
  size_t n = presetColorsVec.size();
  for (size_t i = 0; i < n && i < 8; ++i) {
    arr[i] = parse_hex_rgbw(presetColorsVec[i].c_str());
  }
  for (size_t i = n; i < 8; ++i) {
    arr[i] = 0x00000000;
  }
}
static void setPendingTransitionFromPreset(const Preset &preset, size_t n) {
  pendingTransition.effect = preset.effect;
  pendingTransition.params = preset.params;
  pendingTransition.params.colors.clear();
  for (size_t i = 0; i < n; ++i) {
    char hex[11];
    snprintf(hex, sizeof(hex), "#%08X", color[i]);
    pendingTransition.params.colors.push_back(String(hex));
  }
  pendingTransition.preset = preset.id;
}
static void captureCurrentFrameForTransition() {
  BusNeoPixel *neo = busManager.getNeoPixelBus();
  size_t count = busManager.getPixelCount();
  std::vector<uint32_t> prevFrame(count);
  for (size_t i = 0; i < count; ++i) {
    prevFrame[i] = neo ? neo->getPixelColor(i) : 0;
  }
  transition.setPreviousFrame(prevFrame);
}
static void renderFrameToBus(const std::vector<uint32_t> &frame) {
  for (size_t i = 0; i < frame.size(); ++i) {
    uint32_t c = frame[i];
    uint8_t r, g, b, w;
    unpack_rgbw(c, r, g, b, w);
    busManager.setPixelColor(i, pack_rgbw(r, g, b, w));
  }
  busManager.show();
}
static void commitPendingTransition() {
  state.effect = pendingTransition.effect;
  state.params = pendingTransition.params;
  state.preset = pendingTransition.preset;
  state.brightness = transition._targetState.brightness;
  if (state.effect == 0 && state.params.colors.size() > 0) {
    color[0] = parse_hex_rgbw(state.params.colors[0].c_str());
  }
  setEffect(state.effect, state.params);
  transition.clearFrames();
}
static void renderAnimationFrame(size_t count, uint8_t brightness) {
  std::vector<uint32_t> animFrame(count, 0);
  auto animColors = parse_colors_vec(state.params.colors);
  size_t animColorCount =
      state.params.colors.size() > 0 ? state.params.colors.size() : 1;
  renderEffectToBuffer(state.effect, state.params, animFrame, count, animColors,
                       animColorCount, brightness);
  renderFrameToBus(animFrame);
}
static void handlePowerOff() {
  busManager.turnOffLEDs();
  state.inTransition = false;
  state.brightness = 0;
  digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? LOW : HIGH);
  static std::vector<uint32_t> g_lastOutputFrame;
  g_lastOutputFrame.clear();
  g_outputFramePtr = &g_lastOutputFrame;
}
static void handleTransition(size_t count,
                             std::vector<uint32_t> &g_lastOutputFrame) {
  transition.blendTransitionFrames(pendingTransition, state, g_lastOutputFrame);
  renderFrameToBus(g_lastOutputFrame);
}
static void handleAnimation(size_t count,
                            std::vector<uint32_t> &g_lastOutputFrame) {
  uint8_t currentBrightness = transition._currentState.brightness;
  state.inTransition = false;
  state.brightness = currentBrightness;
  std::vector<uint32_t> animFrame(count, 0);
  auto animColors = parse_colors_vec(state.params.colors);
  size_t animColorCount =
      state.params.colors.size() > 0 ? state.params.colors.size() : 1;
  renderEffectToBuffer(state.effect, state.params, animFrame, count, animColors,
                       animColorCount, currentBrightness);
  renderFrameToBus(animFrame);
  g_lastOutputFrame = animFrame;
  if (state.power) {
    digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? HIGH : LOW);
  }
}

// --- Public API implementation ---
void applyPreset(uint8_t presetId, uint8_t brightness) {
  transition.abortTransition();
  auto it =
      std::find_if(config.presets.begin(), config.presets.end(),
                   [presetId](const Preset &p) { return p.id == presetId; });
  if (it == config.presets.end() || !it->enabled)
    return;
  Preset &preset = *it;
  uint8_t safeBrightness = std::min(brightness, config.safety.maxBrightness);
  state.brightness = safeBrightness;

  state.prevEffect = state.effect;
  state.prevParams = state.params;
  colorCount =
      preset.params.colors.size() > 0 ? preset.params.colors.size() : 1;
  fillArrayFromPresetColors(preset.params.colors, color);
  if (preset.effect == 1 && !hasValidPresetColors(preset.params.colors))
    return;

  transition._previousState = transition._currentState;
  bool doTransition = (state.prevEffect >= 0);
  webServer.applyTransitionTimeLimit(state.transitionTime);

  size_t count = busManager.getPixelCount();
  std::vector<uint32_t> prevFrame;
  captureCurrentBusFrame(prevFrame);
  transition.setPreviousFrame(prevFrame);

  std::vector<uint32_t> targetFrame(count, 0);
  std::array<uint32_t, 8> presetColors = {0};
  fillArrayFromPresetColors(preset.params.colors, presetColors);
  size_t presetColorCount =
      preset.params.colors.size() > 0 ? preset.params.colors.size() : 1;
  uint8_t presetBrightnessHex = (brightness > 0 ? brightness : 255);
  presetBrightnessHex =
      std::min(presetBrightnessHex, config.safety.maxBrightness);
  renderEffectToBuffer(preset.effect, preset.params, targetFrame, count,
                       presetColors, presetColorCount, presetBrightnessHex);
  transition.setTargetFrame(targetFrame);

  transition.forceCurrentBrightness(transition._previousState.brightness);
  transition.startTransition(
      {safeBrightness,
       std::vector<uint32_t>(color.begin(), color.begin() + colorCount)},
      state.transitionTime);

  setPendingTransitionFromPreset(preset, preset.params.colors.size());
  state.power = true;
  state.inTransition = true;
  state.preset = preset.id;
  webServer.broadcastState();
}
void setPower(bool power) {
  if (state.power == power) {
    return;
  }
  state.power = power;
  digitalWrite(config.led.relayPin,
               power ? (config.led.relayActiveHigh ? HIGH : LOW)
                     : (config.led.relayActiveHigh ? LOW : HIGH));
  uint8_t targetBrightness = power ? state.brightness : 0;
  // Use powerOn transition time for power changes
  state.transitionTime = config.transitionTimes.powerOn;
  webServer.applyTransitionTimeLimit(state.transitionTime);
  if (power) {
    transition.forceCurrentBrightness(state.brightness);
  }
  if (transition._currentState.brightness != targetBrightness ||
      !transition.isTransitioning()) {
    // Use current colors for effect transition
    auto curColors = transition._currentState.colors;
    transition.startTransition({targetBrightness, curColors},
                               state.transitionTime);
  }
  webServer.broadcastState();
}
void setBrightness(uint8_t brightness) {
  webServer.applyBrightnessLimit(brightness);
  state.brightness = brightness;
  state.transitionTime = config.transitionTimes.manual;
  webServer.applyTransitionTimeLimit(state.transitionTime);
  uint8_t current = transition._currentState.brightness;
  if (brightness == current)
    return;
  if (!transition.isTransitioning()) {
    transition.forceCurrentBrightness(current);
  }
  captureCurrentFrameForTransition();
  transition.startTransition({brightness, transition._currentState.colors},
                             state.transitionTime);
  webServer.broadcastState();
}
void setEffect(uint8_t effect, const EffectParams &params) {
  state.effect = effect;
  state.params = params;
  state.params.colors.clear();
  // Only push actual preset colors, not padded black entries
  size_t n = colorCount;
  for (size_t i = 0; i < n; ++i) {
    char hex[11];
    snprintf(hex, sizeof(hex), "#%08X", color[i]);
    state.params.colors.push_back(String(hex));
  }

  BusNeoPixel *neo = busManager.getNeoPixelBus();
  if (!neo || !neo->getStrip())
    return;
  if (effect < effectRegistry.size() && effectRegistry[effect].fn) {
    effectRegistry[effect].fn();
  }
}
void setUserColor(const uint32_t *newColor, size_t count) {
  colorCount = count;
  state.params.colors.clear();
  for (size_t i = 0; i < 8; ++i) {
    char hex[11];
    snprintf(hex, sizeof(hex), "#%08X", color[i]);
    state.params.colors.push_back(String(hex));
  }
  // Use effect transition time for color/effect changes
  state.transitionTime = config.transitionTimes.manual;
  setEffect(state.effect, state.params);
}
void updateLEDs() {
  BusNeoPixel *neo = busManager.getNeoPixelBus();
  if (!neo || !neo->getStrip())
    return;
  if (!state.power) {
    handlePowerOff();
    return;
  }
  size_t count = busManager.getPixelCount();
  static bool pendingCommit = false;
  static std::vector<uint32_t> g_lastOutputFrame;
  g_lastOutputFrame.resize(count, 0);
  if (transition.isTransitioning()) {
    pendingCommit = true;
    handleTransition(count, g_lastOutputFrame);
  } else {
    if (pendingCommit) {
      commitPendingTransition();
      pendingCommit = false;
    }
    handleAnimation(count, g_lastOutputFrame);
  }
  g_outputFramePtr = &g_lastOutputFrame;
}
