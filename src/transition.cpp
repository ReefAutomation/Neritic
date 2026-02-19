#include "transition.h"
#include "bus_manager.h"
#include "colors.h"
#include "effects.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "state.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
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

void TransitionEngine::blendTransitionFrames(
    const PendingTransitionState &pendingTransition, const SystemState &state,
    std::vector<uint32_t> &outFrame) {
  size_t count = outFrame.size();
  float progress =
      float((uint32_t)(esp_timer_get_time() / 1000ULL) - getStartTime()) /
      float(getDuration());
  if (progress > 1.0f)
    progress = 1.0f;
  progress = progress * progress * (3.0f - 2.0f * progress); // smoothstep
  float colorFrac = getEffectTransitionFraction();
  float colorProgress = (progress < colorFrac) ? (progress / colorFrac) : 1.0f;
  bool brightnessOnly = (_startState.colors == _targetState.colors);

  // Power/brightness-only transitions must follow brightness over the full
  // duration. Do not use colorProgress blending here, otherwise output can
  // reach black early (around transitionTimes.effect window).
  if (brightnessOnly) {
    const bool isPowerOff = (_startState.brightness > 0 && _targetState.brightness == 0);
    const uint16_t startB = (uint16_t)_startState.brightness;
    const uint16_t currB = (uint16_t)_currentState.brightness;
    uint16_t referenceB = 0;
    if (isPowerOff && startB > 0) {
      referenceB = startB;
    } else {
      const uint16_t targetB = (uint16_t)_targetState.brightness;
      referenceB = (startB > targetB) ? startB : targetB;
    }

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
    renderEffectToBuffer(state.effect,
                         state.params,
                         outFrame,
                         count,
                         colors,
                         colorCount,
                         (uint8_t)referenceB);

    // Some effects keep internal temporal buffers and may emit stale luminance.
    // Enforce deterministic transition luminance by scaling every brightness-only
    // transition frame to current/reference ratio.
    if (referenceB > 0) {
      for (size_t i = 0; i < count; ++i) {
        uint8_t r, g, b, w;
        unpack_rgbw(outFrame[i], r, g, b, w);
        r = (uint8_t)lroundf((float)r * (float)currB / (float)referenceB);
        g = (uint8_t)lroundf((float)g * (float)currB / (float)referenceB);
        b = (uint8_t)lroundf((float)b * (float)currB / (float)referenceB);
        w = (uint8_t)lroundf((float)w * (float)currB / (float)referenceB);
        outFrame[i] = pack_rgbw(r, g, b, w);
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
  float progress = t * t * (3.0f - 2.0f * t); // default smoothstep

  // For power-off, smoothstep can appear "stuck" at low ranges (e.g. 10->0)
  // and then drop near the end. Use ease-out so dimming is visible earlier.
  const bool isPowerOff = (_startState.brightness > 0 && _targetState.brightness == 0);
  if (isPowerOff) {
    progress = 1.0f - (1.0f - t) * (1.0f - t); // easeOutQuad
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
