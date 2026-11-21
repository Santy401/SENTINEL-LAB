// Host-side tests for the v0.2.0 motion engine.
// No Arduino, no WiFi, no sensor powered.
// Build: g++ -std=c++17 -Wall -Wextra -Iinclude
//        test/test_motion_engine.cpp src/v02/motion_engine.cpp
#include <cassert>
#include <cmath>
#include <cstdio>

#include "v02/motion_engine.h"

using namespace sentinel::v02;

namespace {

uint32_t nowMs = 1000;

MotionThresholds testThresholds() {
  MotionThresholds t;
  t.movement = 2.0f;
  t.alert = 6.0f;
  t.hysteresis = 0.8f;
  t.gravityAlpha = 0.98f;  // nearly static gravity -> predictable tests
  t.confirmations = 3;
  t.timeoutMs = 2000;
  return t;
}

void feedMotion(MotionEngine& engine, float linearY,
                AccelSource source = AccelSource::Simulated) {
  nowMs += 50;
  AccelSample sample;
  sample.x = 0.0f;
  sample.y = linearY;
  sample.z = 9.81f;
  sample.timestampMs = nowMs;
  sample.source = source;
  engine.feed(sample);
}

void feedN(MotionEngine& engine, float linearY, int count) {
  for (int i = 0; i < count; ++i) {
    feedMotion(engine, linearY);
  }
}

void assertLastEvent(const MotionEngine& engine, MotionEventType type) {
  assert(engine.eventCount() > 0);
  assert(engine.eventAt(engine.eventCount() - 1).type == type);
}

}  // namespace

int main() {
  // ---------------------------------------------------------------------
  // 1. Boot state and recovery from the first sample.
  // ---------------------------------------------------------------------
  {
    MotionEngine engine;
    engine.begin(testThresholds(), nowMs);
    assert(engine.state() == MotionState::SensorOffline);
    assert(!engine.fresh(nowMs));

    feedMotion(engine, 0.0f);
    assert(engine.state() == MotionState::Stable);
    assert(engine.fresh(nowMs));
    assert(engine.snapshot().sampleCount == 1);
    assertLastEvent(engine, MotionEventType::Recovery);
    assert(std::fabs(engine.snapshot().gravityZ - 9.81f) < 0.01f);
  }

  // ---------------------------------------------------------------------
  // 2. STABLE -> MOVEMENT -> STABLE with 3 confirmations and hysteresis.
  // ---------------------------------------------------------------------
  {
    MotionEngine engine;
    engine.begin(testThresholds(), nowMs);
    feedN(engine, 0.0f, 2);
    assert(engine.state() == MotionState::Stable);

    feedMotion(engine, 4.0f);
    feedMotion(engine, 4.0f);
    assert(engine.state() == MotionState::Stable);  // not enough yet
    feedMotion(engine, 4.0f);
    assert(engine.state() == MotionState::Movement);
    assertLastEvent(engine, MotionEventType::StateChange);

    // Inside the hysteresis band [1.2, 2.0] the state must not change.
    // 1.8 leaves margin for the slow gravity adaptation.
    feedN(engine, 1.8f, 6);
    assert(engine.state() == MotionState::Movement);

    feedN(engine, 0.0f, 3);
    assert(engine.state() == MotionState::Stable);
  }

  // ---------------------------------------------------------------------
  // 3. MOVEMENT -> ALERT -> MOVEMENT.
  // ---------------------------------------------------------------------
  {
    MotionEngine engine;
    engine.begin(testThresholds(), nowMs);
    feedMotion(engine, 0.0f);
    feedN(engine, 4.0f, 3);
    assert(engine.state() == MotionState::Movement);

    feedN(engine, 8.0f, 3);
    assert(engine.state() == MotionState::Alert);
    assertLastEvent(engine, MotionEventType::AlertRaised);

    feedN(engine, 0.0f, 3);
    assert(engine.state() == MotionState::Movement);
    assertLastEvent(engine, MotionEventType::AlertCleared);
  }

  // ---------------------------------------------------------------------
  // 4. Timeout and recovery: only a NEW valid sample restores monitoring.
  // ---------------------------------------------------------------------
  {
    MotionEngine engine;
    MotionThresholds t = testThresholds();
    engine.begin(t, nowMs);
    feedN(engine, 0.0f, 3);
    assert(engine.state() == MotionState::Stable);
    assert(engine.fresh(nowMs));

    engine.update(nowMs + t.timeoutMs);
    assert(engine.state() == MotionState::Stable);  // boundary is inclusive

    engine.update(nowMs + t.timeoutMs + 1);
    assert(engine.state() == MotionState::SensorOffline);
    assert(!engine.fresh(nowMs + t.timeoutMs + 1));
    assert(engine.snapshot().source == AccelSource::None);
    assertLastEvent(engine, MotionEventType::Timeout);

    // update() alone must never recover the state.
    engine.update(nowMs + 10000);
    assert(engine.state() == MotionState::SensorOffline);

    feedMotion(engine, 0.0f, AccelSource::BrowserDeviceMotion);
    assert(engine.state() == MotionState::Stable);
    assert(engine.snapshot().source == AccelSource::BrowserDeviceMotion);
    assertLastEvent(engine, MotionEventType::Recovery);
  }

  // ---------------------------------------------------------------------
  // 5. Thresholds are configurable and validated.
  // ---------------------------------------------------------------------
  {
    MotionEngine engine;
    MotionThresholds t = testThresholds();
    t.movement = 10.0f;
    t.alert = 20.0f;
    t.confirmations = 5;
    engine.begin(t, nowMs);
    feedMotion(engine, 0.0f);
    feedN(engine, 8.0f, 8);
    assert(engine.state() == MotionState::Stable);  // 8 < 10

    t.alert = 1.0f;  // below movement: must be clamped above it
    engine.configure(t);
    assert(engine.thresholds().alert > engine.thresholds().movement);
  }

  // ---------------------------------------------------------------------
  // 6. Event log is a bounded ring buffer.
  // ---------------------------------------------------------------------
  {
    MotionEngine engine;
    engine.begin(testThresholds(), nowMs);
    for (int cycle = 0; cycle < 12; ++cycle) {
      feedMotion(engine, 0.0f);
      feedN(engine, 0.0f, 3);
      feedN(engine, 8.0f, 3);
      feedN(engine, 0.0f, 3);
      engine.update(nowMs + engine.thresholds().timeoutMs + 1);
    }
    assert(engine.eventCount() <= 24);
    assert(engine.totalEvents() > 24);
    assert(engine.eventAt(0).timestampMs <=
           engine.eventAt(engine.eventCount() - 1).timestampMs);
  }

  std::printf("test_motion_engine: ALL TESTS PASSED\n");
  return 0;
}
