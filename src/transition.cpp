#include "transition.h"
#include "bus_manager.h"
#include "colors.h"
#include "effects.h"
#include "state.h"
#include <Arduino.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

extern Configuration config;

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
  float progress = float(millis() - getStartTime()) / float(getDuration());
  if (progress > 1.0f)
    progress = 1.0f;
  progress = progress * progress * (3.0f - 2.0f * progress); // smoothstep
  float colorFrac = getEffectTransitionFraction();
  float colorProgress = (progress < colorFrac) ? (progress / colorFrac) : 1.0f;
  bool brightnessOnly =
      (pendingTransition.effect == state.effect &&
       pendingTransition.params.colors == state.params.colors);
  std::vector<uint32_t> prevFrame(count, 0);
  std::vector<uint32_t> nextFrame(count, 0);
  if (brightnessOnly) {
    auto colors = parse_colors_vec(pendingTransition.params.colors);
    size_t colorCount = pendingTransition.params.colors.size() > 0
                            ? pendingTransition.params.colors.size()
                            : 1;
    uint8_t prevBrightness = _currentState.brightness;
    uint8_t nextBrightness = _targetState.brightness;
    renderEffectToBuffer(pendingTransition.effect, pendingTransition.params,
                         prevFrame, count, colors, colorCount, prevBrightness);
    renderEffectToBuffer(pendingTransition.effect, pendingTransition.params,
                         nextFrame, count, colors, colorCount, nextBrightness);
  } else {
    if (state.prevEffect == 0) {
      prevFrame = getPreviousFrame();
    } else {
      auto prevColors = parse_colors_vec(state.prevParams.colors);
      size_t prevColorCount = state.prevParams.colors.size() > 0
                                  ? state.prevParams.colors.size()
                                  : 1;
      uint8_t prevBrightness = _currentState.brightness;
      renderEffectToBuffer(state.prevEffect, state.prevParams, prevFrame, count,
                           prevColors, prevColorCount, prevBrightness);
    }
    auto nextColors = parse_colors_vec(pendingTransition.params.colors);
    size_t nextColorCount = pendingTransition.params.colors.size() > 0
                                ? pendingTransition.params.colors.size()
                                : 1;
    uint8_t nextBrightness = _targetState.brightness;
    renderEffectToBuffer(pendingTransition.effect, pendingTransition.params,
                         nextFrame, count, nextColors, nextColorCount,
                         nextBrightness);
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
  _startTime = millis();
  _duration = duration;
  _active = true;
  // If current colors vector is empty or size mismatch, initialize
  if (_startState.colors.size() != _targetState.colors.size()) {
    _startState.colors = std::vector<uint32_t>(_targetState.colors.size(), 0);
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

  uint32_t elapsed = millis() - _startTime;
  if (elapsed >= _duration) {
    _currentState = _targetState;
    _active = false;
    _phase = Phase::None;
    return;
  }

  // Calculate progress (0.0 to 1.0)
  float progress = (float)elapsed / (float)_duration;
  progress = progress * progress * (3.0 - 2.0 * progress);

  // Brightness always transitions over full duration
  _currentState.brightness =
      interpolate(_startState.brightness, _targetState.brightness, progress);

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
  return start + (uint8_t)ceilf((float)(target - start) * progress);
}

uint32_t TransitionEngine::interpolateColor(uint32_t start, uint32_t target,
                                            float progress) {
  uint8_t r, g, b, w;
  blend_rgbw_brightness(start, target, progress, 255, r, g, b, w);
  return pack_rgbw(r, g, b, w);
}
