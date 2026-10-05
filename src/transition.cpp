#include "transition.h"
#include "bus_manager.h"
#include "colors.h"
#include "effects.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "state.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <vector>

extern Configuration config;
static const char *TAG = "transition";

TransitionEngine::TransitionEngine() {}

static void blendFrames(const std::vector<uint32_t> &prevFrame,
                        const std::vector<uint32_t> &nextFrame,
                        float blendFactor, std::vector<uint32_t> &blended) {
  for (size_t i = 0; i < blended.size(); ++i) {
    uint32_t prev = prevFrame[i];
    uint32_t next = nextFrame[i];
    uint8_t r, g, b, w;
    blend_rgbw_brightness(prev, next, blendFactor, 255, r, g, b, w);
    blended[i] = pack_rgbw(r, g, b, w);
  }
}

// Scales every pixel of a frame rendered at `referenceB` down to `uniformB`.
static void scaleFrameBrightness(std::vector<uint32_t> &frame,
                                 uint16_t uniformB, uint16_t referenceB) {
  if (referenceB == 0 || uniformB >= referenceB)
    return;
  for (size_t i = 0; i < frame.size(); ++i) {
    uint8_t r, g, b, w;
    unpack_rgbw(frame[i], r, g, b, w);
    r = (uint8_t)lroundf((float)r * (float)uniformB / (float)referenceB);
    g = (uint8_t)lroundf((float)g * (float)uniformB / (float)referenceB);
    b = (uint8_t)lroundf((float)b * (float)uniformB / (float)referenceB);
    w = (uint8_t)lroundf((float)w * (float)uniformB / (float)referenceB);
    frame[i] = pack_rgbw(r, g, b, w);
  }
}

static void applyRadialWipe(std::vector<uint32_t> &frame, float progress,
                            bool turningOn) {
  if (frame.empty())
    return;

  const float center = (float(frame.size()) - 1.0f) * 0.5f;
  const float maxDistance = center;
  const float feather = fmaxf(1.0f, float(frame.size()) * 0.015f);
  const float radius = (turningOn ? progress : 1.0f - progress) *
                       (maxDistance + feather);

  for (size_t i = 0; i < frame.size(); ++i) {
    float coverage = (radius - fabsf(float(i) - center)) / feather;
    coverage = fmaxf(0.0f, fminf(1.0f, coverage));
    coverage = coverage * coverage * (3.0f - 2.0f * coverage);

    uint8_t r, g, b, w;
    blend_rgbw_brightness(0, frame[i], coverage, 255, r, g, b, w);
    frame[i] = pack_rgbw(r, g, b, w);
  }
}

// Builds the staged one-by-one activation order: center first, then the outer
// borders, then the midpoints between already lit LEDs, and so on. Each index
// is assigned the stage at which it lights up during power-on.
static uint16_t
buildActivationStages(size_t count,
                      std::array<uint16_t, MAX_LED_COUNT> &activationStage) {
  activationStage.fill(std::numeric_limits<uint16_t>::max());
  std::array<size_t, MAX_LED_COUNT> active;
  size_t activeCount = 0;
  auto activate = [&](size_t index, uint16_t stage) {
    if (activationStage[index] != std::numeric_limits<uint16_t>::max())
      return;
    activationStage[index] = stage;
    active[activeCount++] = index;
  };

  const float center = (float(count) - 1.0f) * 0.5f;
  const size_t centerLeft = (count - 1) / 2;
  const size_t centerRight = count / 2;
  activate(centerLeft, 0);
  activate(centerRight, 0);
  activate(0, 1);
  activate(count - 1, 1);

  uint16_t stage = 2;
  while (activeCount < count) {
    std::sort(active.begin(), active.begin() + activeCount);
    const size_t anchorsAtStart = activeCount;
    for (size_t i = 1; i < anchorsAtStart; ++i) {
      const size_t left = active[i - 1];
      const size_t right = active[i];
      if (right - left <= 1)
        continue;

      size_t midpoint = (left + right) / 2;
      if (right <= size_t(center))
        midpoint = (left + right + 1) / 2;
      activate(midpoint, stage);
    }
    ++stage;
  }

  uint16_t stageCount = 0;
  for (size_t i = 0; i < count; ++i)
    stageCount = std::max(stageCount, uint16_t(activationStage[i] + 1));
  return stageCount;
}

static void applyPowerOnWipe(std::vector<uint32_t> &frame, float progress,
                             const std::array<uint16_t, MAX_LED_COUNT> &stages,
                             uint16_t stageCount) {
  if (frame.empty())
    return;

  if (frame.size() > MAX_LED_COUNT) {
    applyRadialWipe(frame, progress, true);
    return;
  }

  for (size_t i = 0; i < frame.size(); ++i) {
    const float phase = progress * stageCount - stages[i];
    float coverage = fmaxf(0.0f, fminf(1.0f, phase));
    coverage = coverage * coverage * (3.0f - 2.0f * coverage);

    uint8_t r, g, b, w;
    blend_rgbw_brightness(0, frame[i], coverage, 255, r, g, b, w);
    frame[i] = pack_rgbw(r, g, b, w);
  }
}

static void applyPowerOffWipe(std::vector<uint32_t> &frame, float progress,
                              const std::array<uint16_t, MAX_LED_COUNT> &stages,
                              uint16_t stageCount) {
  if (frame.empty())
    return;

  if (frame.size() > MAX_LED_COUNT) {
    applyRadialWipe(frame, progress, false);
    return;
  }

  for (size_t i = 0; i < frame.size(); ++i) {
    // Reverse of the power-on order: the last LEDs to light are the first to
    // fade out, while the center LEDs stay lit the longest.
    const float phase =
        progress * stageCount - (stageCount - 1 - stages[i]);
    float offCoverage = fmaxf(0.0f, fminf(1.0f, phase));
    offCoverage = offCoverage * offCoverage * (3.0f - 2.0f * offCoverage);
    const float coverage = 1.0f - offCoverage;

    uint8_t r, g, b, w;
    blend_rgbw_brightness(0, frame[i], coverage, 255, r, g, b, w);
    frame[i] = pack_rgbw(r, g, b, w);
  }
}

static bool frameIsBlack(const std::vector<uint32_t> &frame) {
  for (uint32_t pixel : frame) {
    if ((pixel & 0xFFFFFFFFU) != 0)
      return false;
  }
  return true;
}

static float smoothstep01(float v) {
  v = fmaxf(0.0f, fminf(1.0f, v));
  return v * v * (3.0f - 2.0f * v);
}

static uint8_t lerpU8(uint16_t a, uint16_t b, float t) {
  float v = (1.0f - t) * (float)a + t * (float)b;
  if (v < 0.0f)
    v = 0.0f;
  if (v > 255.0f)
    v = 255.0f;
  return (uint8_t)lroundf(v);
}

// The cascade completes once the ramp brightness reaches this floor (hex).
// One brightness step is allocated per activation stage so LEDs light strictly
// one-by-one (center → borders → gaps); the uniform ramp begins after.
static uint16_t cascadeFloorBrightness(uint16_t stageCount) {
  return stageCount;
}

// Brightness-driven cascade: the ramp's current brightness (0..target) decides
// both the one-by-one spatial spread and the LED intensity. While below the
// cascade floor the spread runs at a fixed 1% power (no ramping); once every
// LED is on, intensity ramps from that floor to the target. Non-power
// brightness changes (no zero crossing) simply follow `currB`.
struct CascadePhase {
  uint16_t bright = 0;       // uniform intensity this frame
  float cascadeProgress = 1; // 0..1 within the cascade segment
  bool inCascade = false;
};

static CascadePhase computeCascadePhase(uint16_t currB, uint16_t startB,
                                        uint16_t targetB,
                                        uint16_t cascadeFloor,
                                        bool turningOn, bool turningOff) {
  CascadePhase p;
  p.bright = currB; // default: follow the global ramp unchanged

  if (turningOn) {
    const uint16_t cascadeEnd = std::min<uint16_t>(targetB, cascadeFloor);
    if (cascadeEnd == 0)
      return p;
    if (currB < cascadeEnd) {
      p.inCascade = true;
      p.cascadeProgress = (float)currB / (float)cascadeEnd;
      p.bright = 1;
    } else if (targetB > cascadeEnd) {
      const float t =
          (float)(currB - cascadeEnd) / (float)(targetB - cascadeEnd);
      p.bright = lerpU8(1, targetB, t);
    }
  } else if (turningOff) {
    const uint16_t cascadeStart = std::min<uint16_t>(startB, cascadeFloor);
    if (cascadeStart == 0)
      return p;
    if (currB > cascadeStart) {
      const float t =
          (float)(startB - currB) / (float)(startB - cascadeStart);
      p.bright = lerpU8(startB, 1, t);
    } else {
      p.inCascade = true;
      p.cascadeProgress = (float)(cascadeStart - currB) / (float)cascadeStart;
      p.bright = 1;
    }
  }
  return p;
}

void TransitionEngine::blendTransitionFrames(
    const PendingTransitionState &pendingTransition, const SystemState &state,
    std::vector<uint32_t> &outFrame) {
  size_t count = outFrame.size();
  const uint32_t elapsedMs =
      (uint32_t)(esp_timer_get_time() / 1000ULL) - getStartTime();
  const uint32_t duration = getDuration() > 0 ? getDuration() : 1;
  float rawProgress = float(elapsedMs) / float(duration);
  if (rawProgress > 1.0f)
    rawProgress = 1.0f;
  float progress = smoothstep01(rawProgress);
  float colorFrac = getEffectTransitionFraction();
  float colorProgress = (progress < colorFrac) ? (progress / colorFrac) : 1.0f;
  bool brightnessOnly = (_startState.colors == _targetState.colors);
  const bool turningOn =
      _startState.brightness == 0 && _targetState.brightness > 0;
  const bool turningOff =
      _startState.brightness > 0 && _targetState.brightness == 0;

  // The cascade is driven by the ramp's current brightness, not by time: LEDs
  // light one-by-one while the ramp is below `cascadeFloor` (the brightness at
  // which every LED is lit at min power), then all LEDs ramp together.
  std::array<uint16_t, MAX_LED_COUNT> activationStage;
  uint16_t stageCount = 0;
  if (count > 0 && count <= MAX_LED_COUNT) {
    stageCount = buildActivationStages(count, activationStage);
  } else if (count > MAX_LED_COUNT) {
    stageCount = (uint16_t)ceilf(log2f((float)count));
  }
  const uint16_t cascadeFloor = cascadeFloorBrightness(stageCount);
  const uint16_t currB = (uint16_t)_currentState.brightness;

  // Power/brightness-only transitions must follow brightness over the full
  // duration. Do not use colorProgress blending here, otherwise output can
  // reach black early (around transitionTimes.effect window).
  if (brightnessOnly) {
    const bool isPowerOff = (_startState.brightness > 0 && _targetState.brightness == 0);
    const bool preserveLightningFlashes =
        state.effect == 4 && state.power && !isPowerOff;
    const uint16_t startB = (uint16_t)_startState.brightness;
    const uint16_t targetB = (uint16_t)_targetState.brightness;
    const uint16_t referenceB = (startB > targetB) ? startB : targetB;

    std::array<uint32_t, 8> colors = {0};
    size_t colorCount = 1;
    if (!_currentState.colors.empty()) {
      colorCount = _currentState.colors.size();
      if (colorCount > colors.size())
        colorCount = colors.size();
      for (size_t i = 0; i < colorCount; ++i) {
        colors[i] = _currentState.colors[i];
      }
    } else {
      colors = parse_colors_vec(state.params.colors);
      colorCount = state.params.colors.empty() ? 1 : state.params.colors.size();
      if (colorCount > colors.size())
        colorCount = colors.size();
    }

    CascadePhase phase = computeCascadePhase(
        currB, startB, targetB, cascadeFloor, turningOn, turningOff);

    renderEffectToBuffer(state.effect,
                         state.params,
                         outFrame,
                         count,
                         colors,
                         colorCount,
                         (uint8_t)(preserveLightningFlashes ? phase.bright
                                                            : referenceB));

    // Lightning keeps its own flashes: render directly at the phase brightness
    // and skip the cascade mask.
    if (!preserveLightningFlashes) {
      scaleFrameBrightness(outFrame, phase.bright, referenceB);
      if (phase.inCascade) {
        if (turningOn)
          applyPowerOnWipe(outFrame, phase.cascadeProgress, activationStage,
                           stageCount);
        else
          applyPowerOffWipe(outFrame, phase.cascadeProgress, activationStage,
                            stageCount);
      }
    }
    return;
  }

  std::vector<uint32_t> prevFrame(count, 0);
  std::vector<uint32_t> nextFrame(count, 0);
  if (state.prevEffect == 0) {
    prevFrame = getPreviousFrame();
  } else {
    auto prevColors = parse_colors_vec(state.prevParams.colors);
    size_t prevColorCount =
        state.prevParams.colors.size() > 0 ? state.prevParams.colors.size() : 1;
    uint8_t prevBrightness = _currentState.brightness;
    renderEffectToBuffer(state.prevEffect,
                         state.prevParams,
                         prevFrame,
                         count,
                         prevColors,
                         prevColorCount,
                         prevBrightness);
  }
  auto nextColors = parse_colors_vec(pendingTransition.params.colors);
  size_t nextColorCount = pendingTransition.params.colors.size() > 0
                              ? pendingTransition.params.colors.size()
                              : 1;
  uint8_t nextBrightness = _targetState.brightness;
  renderEffectToBuffer(pendingTransition.effect,
                       pendingTransition.params,
                       nextFrame,
                       count,
                       nextColors,
                       nextColorCount,
                       nextBrightness);

  const bool toBlack = frameIsBlack(nextFrame) && !frameIsBlack(prevFrame);
  const bool fromBlack = frameIsBlack(prevFrame) && !frameIsBlack(nextFrame);

  if (toBlack || turningOff) {
    // Effect/preset change toward black/off. Ramp the previous effect down to
    // the cascade floor, then fade the LEDs out one-by-one in reverse order at
    // that fixed floor brightness.
    const uint16_t startB = (uint16_t)_startState.brightness;
    const uint16_t targetB = (uint16_t)_targetState.brightness;
    CascadePhase phase = computeCascadePhase(
        currB, startB, targetB, cascadeFloor, false, true);

    std::vector<uint32_t> fadeFrame(count, 0);
    auto prevColors = parse_colors_vec(state.prevParams.colors);
    size_t prevColorCount =
        state.prevParams.colors.size() > 0 ? state.prevParams.colors.size() : 1;
    renderEffectToBuffer(state.prevEffect, state.prevParams, fadeFrame, count,
                         prevColors, prevColorCount, (uint8_t)startB);
    scaleFrameBrightness(fadeFrame, phase.bright, startB);
    if (phase.inCascade)
      applyPowerOffWipe(fadeFrame, phase.cascadeProgress, activationStage,
                        stageCount);
    outFrame = fadeFrame;
    return;
  }
  if (fromBlack || turningOn) {
    // Effect/preset change from black/off. Light the target effect one-by-one
    // at the fixed cascade floor, then ramp it up to the target brightness.
    const uint16_t startB = (uint16_t)_startState.brightness;
    const uint16_t targetB = (uint16_t)_targetState.brightness;
    CascadePhase phase = computeCascadePhase(
        currB, startB, targetB, cascadeFloor, true, false);

    std::vector<uint32_t> fadeFrame(count, 0);
    renderEffectToBuffer(pendingTransition.effect,
                         pendingTransition.params,
                         fadeFrame,
                         count,
                         nextColors,
                         nextColorCount,
                         (uint8_t)targetB);
    scaleFrameBrightness(fadeFrame, phase.bright, targetB);
    if (phase.inCascade)
      applyPowerOnWipe(fadeFrame, phase.cascadeProgress, activationStage,
                       stageCount);
    outFrame = fadeFrame;
    return;
  }

  ::blendFrames(prevFrame, nextFrame, colorProgress, outFrame);
}

void TransitionEngine::abortTransition() {
  _active = false;
  _phase = Phase::None;
  clearFrames();
}
void TransitionEngine::startTransition(const TransitionState &targetState,
                                       uint32_t duration) {
  // Support variable number of colors for transition
  _phase = Phase::Brightness;
  _pendingBrightnessTransition = false;
  _startState = _currentState;
  _targetState = targetState;
  _startTime = (uint32_t)(esp_timer_get_time() / 1000ULL);
  _duration = duration;
  _active = true;
  // If current colors vector is empty or size mismatch, initialize
  if (_startState.colors.size() != _targetState.colors.size()) {
    _startState.colors = std::vector<uint32_t>(_targetState.colors.size(), 0);
  }

  if (_startState.brightness == 0 || _targetState.brightness == 0) {
    ESP_LOGD(TAG,
             "Power transition start: start=%u target=%u durationMs=%u",
             (unsigned)_startState.brightness,
             (unsigned)_targetState.brightness,
             (unsigned)_duration);
  }
}
// Frame blending API
void TransitionEngine::setPreviousFrame(const std::vector<uint32_t> &frame) {
  this->previousFrame = frame;
}
void TransitionEngine::setTargetFrame(const std::vector<uint32_t> &frame) {
  this->targetFrame = frame;
}
void TransitionEngine::clearFrames() {
  previousFrame.clear();
  targetFrame.clear();
}
void TransitionEngine::forceCurrentBrightness(uint8_t value) {
  _currentState.brightness = value;
}

void TransitionEngine::update() {
  if (!_active) {
    _phase = Phase::None;
    return;
  }

  const bool isPowerTransition =
      (_startState.brightness == 0 || _targetState.brightness == 0);
  static uint32_t lastPowerLogMs = 0;
  static uint8_t lastLoggedBrightness = 0xFF;

  uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000ULL) - _startTime;
  if (elapsed >= _duration) {
    if (isPowerTransition) {
      ESP_LOGD(TAG,
               "Power transition done: start=%u final=%u target=%u elapsedMs=%u",
               (unsigned)_startState.brightness,
               (unsigned)_currentState.brightness,
               (unsigned)_targetState.brightness,
               (unsigned)elapsed);
    }
    _currentState = _targetState;
    _active = false;
    _phase = Phase::None;
    return;
  }

  // Calculate progress (0.0 to 1.0)
  float t = (float)elapsed / (float)_duration;
  // Power transitions (0↔target) use a linear ramp: the one-by-one cascade
  // drives the spatial fade, and a linear brightness means the first LED turns
  // on at the very start and the last LED turns off at the very end — no
  // dead zones from easing at the endpoints.
  float progress = t;
  if (!isPowerTransition) {
    progress = t * t * (3.0f - 2.0f * t); // smoothstep for color/effect
  }

  // Brightness always transitions over full duration
  _currentState.brightness =
      interpolate(_startState.brightness, _targetState.brightness, progress);

  if (isPowerTransition) {
    const uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if ((nowMs - lastPowerLogMs) >= 250U &&
        _currentState.brightness != lastLoggedBrightness) {
      ESP_LOGD(TAG,
               "Power transition step: elapsed=%u/%u current=%u start=%u target=%u",
               (unsigned)elapsed,
               (unsigned)_duration,
               (unsigned)_currentState.brightness,
               (unsigned)_startState.brightness,
               (unsigned)_targetState.brightness);
      lastPowerLogMs = nowMs;
      lastLoggedBrightness = _currentState.brightness;
    }
  }

  // Use transitionTimes.effect to determine the fraction of the transition for
  // effect/color
  float colorFrac = 1.0f;
  if (config.transitionTimes.effect > 0 && _duration > 0) {
    colorFrac = float(config.transitionTimes.effect) / float(_duration);
    if (colorFrac > 1.0f)
      colorFrac = 1.0f;
    if (colorFrac < 0.01f)
      colorFrac = 0.01f;
  }
  float colorProgress = (progress < colorFrac) ? (progress / colorFrac) : 1.0f;
  _currentState.colors.resize(_targetState.colors.size(), 0);
  for (size_t i = 0; i < _targetState.colors.size(); ++i) {
    if (colorProgress < 1.0f) {
      _currentState.colors[i] = interpolateColor(
          _startState.colors[i], _targetState.colors[i], colorProgress);
    } else {
      _currentState.colors[i] = _targetState.colors[i];
    }
  }
}

bool TransitionEngine::isTransitioning() { return _active; }

uint8_t TransitionEngine::interpolate(uint8_t start, uint8_t target,
                                      float progress) {
  float value = (1.0f - progress) * (float)start + progress * (float)target;
  if (value < 0.0f)
    value = 0.0f;
  if (value > 255.0f)
    value = 255.0f;
  return (uint8_t)lroundf(value);
}

uint32_t TransitionEngine::interpolateColor(uint32_t start, uint32_t target,
                                            float progress) {
  uint8_t r, g, b, w;
  blend_rgbw_brightness(start, target, progress, 255, r, g, b, w);
  return pack_rgbw(r, g, b, w);
}
