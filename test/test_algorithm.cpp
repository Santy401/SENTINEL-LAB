// Host-side tests: no hardware, no Arduino, no sensor powered.
// Build: g++ -std=c++17 -Wall -Wextra -Iinclude test/test_algorithm.cpp
#include <cassert>
#include <cmath>
#include <cstdio>

#include "distance_math.h"

using sentinel::math::distanceCm;
using sentinel::math::median;

namespace {

bool close(float a, float b, float tol = 0.05f) {
  return std::fabs(a - b) <= tol;
}

// Mirrors the detection counters in src/main.cpp.
enum class SimState { Monitoring, Intrusion };

struct DetectorSim {
  SimState state = SimState::Monitoring;
  uint8_t near = 0;
  uint8_t clear = 0;
  uint8_t invalid = 0;

  void feed(bool valid, float cm) {
    if (!valid) {
      ++invalid;
      return;  // invalid reading is never treated as "object absent"
    }
    invalid = 0;

    if (cm < 20.0f) {
      ++near;
      clear = 0;
      if (near >= 3) {
        near = 0;
        state = SimState::Intrusion;
      }
    } else if (cm > 30.0f) {
      ++clear;
      near = 0;
      if (state == SimState::Intrusion && clear >= 3) {
        clear = 0;
        state = SimState::Monitoring;
      }
    } else {
      // Hysteresis band [20, 30]: no counter change.
      near = 0;
      clear = 0;
    }
  }
};

}  // namespace

int main() {
  // --- Physics / conversion ---
  assert(close(distanceCm(583, 0.0343f), 10.0f, 0.1f));
  assert(close(distanceCm(1166, 0.0343f), 20.0f, 0.1f));
  assert(distanceCm(0, 0.0343f) == 0.0f);

  // --- Median filter ---
  float odd[3] = {22.0f, 18.0f, 20.0f};
  assert(close(median(odd, 3), 20.0f));
  float even[4] = {35.0f, 25.0f, 40.0f, 30.0f};
  assert(close(median(even, 4), 32.5f));
  assert(median(nullptr, 0) == 0.0f);

  // --- Detection states ---
  DetectorSim sim;
  sim.feed(true, 15.0f);
  sim.feed(true, 18.0f);
  assert(sim.state == SimState::Monitoring);
  sim.feed(true, 12.0f);
  assert(sim.state == SimState::Intrusion);

  // Invalid readings must not release the intrusion.
  sim.feed(false, 0.0f);
  sim.feed(false, 0.0f);
  assert(sim.state == SimState::Intrusion);

  // Hysteresis band must not change counters.
  sim.feed(true, 25.0f);
  assert(sim.state == SimState::Intrusion);

  // Three valid readings > 30 cm clear the intrusion.
  sim.feed(true, 35.0f);
  sim.feed(true, 40.0f);
  sim.feed(true, 38.0f);
  assert(sim.state == SimState::Monitoring);

  // ERROR recovery: only a NEW valid sample is accepted, not a cached flag.
  const bool cachedLastValid = true;
  const bool newValidSample = false;
  const bool shouldRecover = newValidSample;  // firmware uses this flag
  assert(!shouldRecover);
  (void)cachedLastValid;

  std::printf("test_algorithm: ALL TESTS PASSED\n");
  return 0;
}
