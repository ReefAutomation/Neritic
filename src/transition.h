#ifndef TRANSITION_H
#define TRANSITION_H

#include "config.h"
#include "debug.h"
#include "state.h"
#include <Arduino.h>

class TransitionEngine {
public:
  // Represents a pending transition (effect, params, preset)
  struct PendingTransitionState {
    uint8_t effect = 0;
    EffectParams params;
    uint8_t preset = 0;
  };
  // Blend transition frames for the current transition state
  void blendTransitionFrames(const PendingTransitionState &pendingTransition,
                             const SystemState &state,
                             std::vector<uint32_t> &outFrame);
  // Fraction of total duration for effect transition (0.0–1.0)
  float effectTransitionFraction = 0.4f; // 40% of the time for effect
  enum class Phase { None, Color, Brightness };
  Phase _phase = Phase::None;
  struct TransitionState {
    uint8_t brightness = 0;
    std::vector<uint32_t> colors;
    TransitionState() = default;
    TransitionState(uint8_t b, const std::vector<uint32_t> &c)
        : brightness(b), colors(c) {}
  };
  // Interrupt and clear any ongoing transition
  void abortTransition();
  float getEffectTransitionFraction() const { return effectTransitionFraction; }
  const std::vector<uint32_t> &getTargetFrame() const { return targetFrame; }
  const std::vector<uint32_t> &getPreviousFrame() const {
    return previousFrame;
  }
  // Frame blending API
  void setPreviousFrame(const std::vector<uint32_t> &frame);
  void setTargetFrame(const std::vector<uint32_t> &frame);
  void clearFrames();
  // Start effect (color/params) transition, then brightness transition
  void startTransition(const TransitionState &target, uint32_t duration);
  TransitionEngine();

  // Getters for transition timing
  uint32_t getStartTime() const { return _startTime; }
  uint32_t getDuration() const { return _duration; }
  // Allow external force of current brightness for smooth slider
  void forceCurrentBrightness(uint8_t value);
  // Allow external force of current color for instant color set
  void update();

  bool isTransitioning();

  // For sequential effect/brightness transition
  bool _pendingBrightnessTransition = false;
  uint8_t _pendingTargetBrightness = 0;
  uint32_t _pendingBrightnessDuration = 0;

  TransitionState _startState;
  TransitionState _targetState;
  TransitionState _currentState;
  TransitionState _previousState;

private:
  std::vector<uint32_t> previousFrame;
  std::vector<uint32_t> targetFrame;
  bool _active = false;
  uint32_t _startTime = 0;
  uint32_t _duration = 0;

  uint8_t interpolate(uint8_t start, uint8_t target, float progress);
  uint32_t interpolateColor(uint32_t start, uint32_t target, float progress);
};

#endif
