#pragma once

#include "v02/accel_source.h"
#include "v02/settings.h"

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace v02 {

enum class MotionState : uint8_t {
  Stable = 0,
  Movement = 1,
  Alert = 2,
  SensorOffline = 3
};

enum class MotionEventType : uint8_t {
  StateChange = 0,
  AlertRaised = 1,
  AlertCleared = 2,
  Timeout = 3,
  Recovery = 4
};

struct MotionThresholds {
  float movement     = settings::kDefaultMovementThreshold;
  float alert        = settings::kDefaultAlertThreshold;
  float hysteresis   = settings::kDefaultHysteresis;
  float gravityAlpha = settings::kDefaultGravityAlpha;
  uint8_t confirmations = settings::kDefaultConfirmations;
  uint32_t timeoutMs = settings::kDefaultTimeoutMs;
};

struct MotionEvent {
  uint32_t timestampMs = 0;
  MotionEventType type = MotionEventType::StateChange;
  MotionState from = MotionState::SensorOffline;
  MotionState to = MotionState::SensorOffline;
  float magnitude = 0.0f;
};

struct MotionSnapshot {
  MotionState state   = MotionState::SensorOffline;
  float rawX = 0.0f, rawY = 0.0f, rawZ = 0.0f;
  float gravityX = 0.0f, gravityY = 0.0f, gravityZ = 0.0f;
  float linearX = 0.0f, linearY = 0.0f, linearZ = 0.0f;
  float linearMagnitude = 0.0f;
  float totalMagnitude = 0.0f;
  AccelSource source = AccelSource::None;
  uint32_t lastSampleMs = 0;
  uint32_t sampleCount = 0;
};

// Pure logic: no Arduino, WiFi or Serial dependencies. Time is injected so the
// engine can be validated on a host with simulated samples.
class MotionEngine {
 public:
  void begin(const MotionThresholds& thresholds, uint32_t nowMs);
  void configure(const MotionThresholds& thresholds);
  const MotionThresholds& thresholds() const { return thresholds_; }

  void feed(const AccelSample& sample);
  void update(uint32_t nowMs);

  MotionState state() const { return snapshot_.state; }
  const MotionSnapshot& snapshot() const { return snapshot_; }
  bool fresh(uint32_t nowMs) const;

  size_t eventCount() const { return eventCount_; }
  const MotionEvent& eventAt(size_t index) const { return events_[index]; }
  uint32_t totalEvents() const { return totalEvents_; }

  void reset(uint32_t nowMs);

 private:
  void setState(MotionState next, MotionEventType type, float magnitude,
                uint32_t nowMs);
  void pushEvent(MotionEventType type, MotionState from, MotionState to,
                 float magnitude, uint32_t nowMs);

  MotionThresholds thresholds_;
  MotionSnapshot snapshot_;
  uint32_t lastSampleMs_ = 0;
  uint8_t aboveMovement_ = 0;
  uint8_t belowMovement_ = 0;
  uint8_t aboveAlert_ = 0;
  uint8_t belowAlert_ = 0;
  bool gravityReady_ = false;

  MotionEvent events_[settings::kMaxEvents];
  size_t eventCount_ = 0;
  uint32_t totalEvents_ = 0;
};

inline const char* motionStateName(MotionState state) {
  switch (state) {
    case MotionState::Stable:        return "STABLE";
    case MotionState::Movement:      return "MOVEMENT";
    case MotionState::Alert:         return "ALERT";
    default:                         return "SENSOR_OFFLINE";
  }
}

inline const char* motionEventName(MotionEventType type) {
  switch (type) {
    case MotionEventType::StateChange: return "StateChange";
    case MotionEventType::AlertRaised: return "AlertRaised";
    case MotionEventType::AlertCleared: return "AlertCleared";
    case MotionEventType::Timeout:     return "Timeout";
    default:                           return "Recovery";
  }
}

}  // namespace v02
}  // namespace sentinel
