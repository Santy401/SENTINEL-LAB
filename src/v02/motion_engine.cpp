#include "v02/motion_engine.h"

#include <math.h>

namespace sentinel {
namespace v02 {

namespace {

float clampFloat(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

}  // namespace

void MotionEngine::begin(const MotionThresholds& thresholds, uint32_t nowMs) {
  reset(nowMs);
  configure(thresholds);
}

void MotionEngine::configure(const MotionThresholds& t) {
  thresholds_.movement = clampFloat(t.movement, 0.1f, 100.0f);
  thresholds_.alert = clampFloat(t.alert, thresholds_.movement + 0.1f, 200.0f);
  thresholds_.hysteresis = clampFloat(t.hysteresis, 0.0f,
                                      thresholds_.movement * 0.9f);
  thresholds_.gravityAlpha = clampFloat(t.gravityAlpha, 0.50f, 0.999f);

  if (t.confirmations < 1) {
    thresholds_.confirmations = 1;
  } else if (t.confirmations > 10) {
    thresholds_.confirmations = 10;
  } else {
    thresholds_.confirmations = t.confirmations;
  }

  thresholds_.timeoutMs = (t.timeoutMs < 100UL) ? 100UL : t.timeoutMs;
}

void MotionEngine::reset(uint32_t nowMs) {
  snapshot_ = MotionSnapshot{};
  snapshot_.state = MotionState::SensorOffline;
  // No sample has arrived yet: fresh() must stay false until one does.
  lastSampleMs_ = 0;
  (void)nowMs;
  aboveMovement_ = 0;
  belowMovement_ = 0;
  aboveAlert_ = 0;
  belowAlert_ = 0;
  gravityReady_ = false;
  eventCount_ = 0;
  totalEvents_ = 0;
}

bool MotionEngine::fresh(uint32_t nowMs) const {
  return lastSampleMs_ != 0 && (nowMs - lastSampleMs_) <= thresholds_.timeoutMs;
}

void MotionEngine::feed(const AccelSample& sample) {
  const uint32_t now = sample.timestampMs;

  // Gravity estimate: low-pass filter over the acceleration that includes it.
  // The linear component is what the detection thresholds are applied to.
  if (!gravityReady_) {
    snapshot_.gravityX = sample.x;
    snapshot_.gravityY = sample.y;
    snapshot_.gravityZ = sample.z;
    gravityReady_ = true;
  } else {
    const float a = thresholds_.gravityAlpha;
    const float b = 1.0f - a;
    snapshot_.gravityX = a * snapshot_.gravityX + b * sample.x;
    snapshot_.gravityY = a * snapshot_.gravityY + b * sample.y;
    snapshot_.gravityZ = a * snapshot_.gravityZ + b * sample.z;
  }

  snapshot_.rawX = sample.x;
  snapshot_.rawY = sample.y;
  snapshot_.rawZ = sample.z;
  snapshot_.linearX = sample.x - snapshot_.gravityX;
  snapshot_.linearY = sample.y - snapshot_.gravityY;
  snapshot_.linearZ = sample.z - snapshot_.gravityZ;
  snapshot_.linearMagnitude = sqrtf(snapshot_.linearX * snapshot_.linearX +
                                    snapshot_.linearY * snapshot_.linearY +
                                    snapshot_.linearZ * snapshot_.linearZ);
  snapshot_.totalMagnitude = sqrtf(sample.x * sample.x + sample.y * sample.y +
                                   sample.z * sample.z);
  snapshot_.source = sample.source;
  snapshot_.lastSampleMs = now;
  ++snapshot_.sampleCount;
  lastSampleMs_ = now;

  // A first sample after an offline period only restores the baseline. It is
  // never allowed to raise an alert by itself.
  if (snapshot_.state == MotionState::SensorOffline) {
    setState(MotionState::Stable, MotionEventType::Recovery,
             snapshot_.linearMagnitude, now);
    return;
  }

  const float m = snapshot_.linearMagnitude;
  const float movementOff = thresholds_.movement - thresholds_.hysteresis;
  const float alertOff = thresholds_.alert - thresholds_.hysteresis;

  // Counters are consecutive: any reading in the hysteresis band breaks the
  // streak. They are also cleared by setState() on every real transition.
  if (m > thresholds_.movement) {
    ++aboveMovement_;
    belowMovement_ = 0;
  } else if (m < movementOff) {
    ++belowMovement_;
    aboveMovement_ = 0;
  } else {
    aboveMovement_ = 0;
    belowMovement_ = 0;
  }

  if (m > thresholds_.alert) {
    ++aboveAlert_;
    belowAlert_ = 0;
  } else if (m < alertOff) {
    ++belowAlert_;
    aboveAlert_ = 0;
  } else {
    aboveAlert_ = 0;
    belowAlert_ = 0;
  }

  const uint8_t n = thresholds_.confirmations;
  switch (snapshot_.state) {
    case MotionState::Stable:
      if (aboveAlert_ >= n) {
        setState(MotionState::Alert, MotionEventType::AlertRaised, m, now);
      } else if (aboveMovement_ >= n) {
        setState(MotionState::Movement, MotionEventType::StateChange, m, now);
      }
      break;

    case MotionState::Movement:
      if (aboveAlert_ >= n) {
        setState(MotionState::Alert, MotionEventType::AlertRaised, m, now);
      } else if (belowMovement_ >= n) {
        setState(MotionState::Stable, MotionEventType::StateChange, m, now);
      }
      break;

    case MotionState::Alert:
      if (belowAlert_ >= n) {
        setState(MotionState::Movement, MotionEventType::AlertCleared, m, now);
      }
      break;

    default:
      break;
  }
}

void MotionEngine::update(uint32_t nowMs) {
  if (snapshot_.state == MotionState::SensorOffline || lastSampleMs_ == 0) {
    return;
  }
  if ((nowMs - lastSampleMs_) <= thresholds_.timeoutMs) {
    return;
  }

  aboveMovement_ = 0;
  belowMovement_ = 0;
  aboveAlert_ = 0;
  belowAlert_ = 0;
  snapshot_.linearX = 0.0f;
  snapshot_.linearY = 0.0f;
  snapshot_.linearZ = 0.0f;
  snapshot_.linearMagnitude = 0.0f;
  snapshot_.source = AccelSource::None;
  setState(MotionState::SensorOffline, MotionEventType::Timeout, 0.0f, nowMs);
}

void MotionEngine::setState(MotionState next, MotionEventType type,
                            float magnitude, uint32_t nowMs) {
  if (next == snapshot_.state) {
    return;
  }
  const MotionState from = snapshot_.state;
  snapshot_.state = next;
  aboveMovement_ = 0;
  belowMovement_ = 0;
  aboveAlert_ = 0;
  belowAlert_ = 0;
  pushEvent(type, from, next, magnitude, nowMs);
}

void MotionEngine::pushEvent(MotionEventType type, MotionState from,
                             MotionState to, float magnitude,
                             uint32_t nowMs) {
  MotionEvent event;
  event.timestampMs = nowMs;
  event.type = type;
  event.from = from;
  event.to = to;
  event.magnitude = magnitude;

  if (eventCount_ < settings::kMaxEvents) {
    events_[eventCount_++] = event;
  } else {
    for (size_t i = 1; i < settings::kMaxEvents; ++i) {
      events_[i - 1] = events_[i];
    }
    events_[settings::kMaxEvents - 1] = event;
  }
  ++totalEvents_;
}

}  // namespace v02
}  // namespace sentinel
