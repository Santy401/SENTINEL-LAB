#include "realtime/phone_motion.h"
#include "realtime/rgb_engine.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>

using namespace sentinel::realtime;

namespace {

RgbColor run(RgbEngine& e, RgbInputs in, uint32_t from, uint32_t to) {
  for (uint32_t t = from; t <= to; t += 20) {
    in.nowMs = t;
    e.update(in);
  }
  return e.color();
}

}  // namespace

int main() {
  RgbEngine e;
  e.begin(0);
  RgbInputs in;
  // Boot: cyan fade-in, never above the brightness cap.
  RgbColor c = run(e, in, 0, 600);
  assert(e.mode() == RgbMode::Boot);
  assert(c.r == 0 && c.b > 0);
  assert(c.b <= RgbEngine::kMaxLevel);

  // Offline breathing: blue-dominant, modest level, changes slowly.
  c = run(e, in, 620, 8000);
  assert(e.mode() == RgbMode::Offline);
  assert(c.b > c.g && c.b > c.r);
  assert(c.b <= RgbEngine::kMaxLevel / 2);

  // Client connected / synced.
  in.clients = 1;
  c = run(e, in, 8020, 10000);
  assert(e.mode() == RgbMode::Client && c.g > c.r);
  in.synced = true;
  c = run(e, in, 10020, 12000);
  assert(e.mode() == RgbMode::Synced && c.g > c.b);

  // Phone shake: stronger shake -> redder and brighter, bounded.
  in.motionActive = true;
  in.intensity = 0.2f;
  RgbColor low = run(e, in, 12020, 13000);
  assert(e.mode() == RgbMode::Motion);
  in.intensity = 1.0f;
  RgbColor high = run(e, in, 13020, 14000);
  assert(high.r > low.r);
  assert(high.r <= RgbEngine::kMaxLevel && high.g <= RgbEngine::kMaxLevel);
  assert(high.r > high.g && high.r > high.b);

  // No strobe: at 50 fps a channel never jumps more than a fraction of the cap.
  int maxStep = 0;
  RgbColor prev = e.color();
  for (uint32_t t = 14020; t < 16000; t += 20) {
    in.nowMs = t;
    in.intensity = (t / 20) % 2 ? 1.0f : 0.0f;  // worst-case input toggling
    e.update(in);
    const RgbColor now = e.color();
    maxStep = std::max(maxStep, std::abs(int(now.r) - int(prev.r)));
    prev = now;
  }
  assert(maxStep <= RgbEngine::kMaxLevel / 3);

  // Event: one soft swell, then back to the base mode.
  in.motionActive = false;
  run(e, in, 16020, 17000);
  e.pulseEvent(17020);
  run(e, in, 17020, 17300);
  assert(e.mode() == RgbMode::Event);
  run(e, in, 17320, 18500);
  assert(e.mode() == RgbMode::Synced);
  assert(std::strcmp(rgbModeName(RgbMode::Motion), "motion") == 0);

  // Phone motion: magnitude, peak, decay to inactive, intensity scale.
  PhoneMotion p;
  assert(!p.active(0) && p.intensity(0) == 0.0f);
  p.ingest("iphone-0a1b2c3d", MotionVector{3, 4, 0}, 1000);
  assert(p.magnitude() == 5.0 && p.peak() == 5.0 && p.samples() == 1);
  assert(p.active(1500) && p.intensity(1500) > 0.3f);
  p.ingest("iphone-0a1b2c3d", MotionVector{0, 0, 0.1}, 1200);
  assert(p.peak() == 5.0);
  for (uint32_t t = 1400; t < 4000; t += 200) p.ingest(nullptr, MotionVector{0, 0, 40}, t);
  assert(p.intensity(4000) == 1.0f);  // clamped full scale
  assert(!p.active(4000 + PhoneMotion::kActiveMs));
  assert(p.intensity(4000 + PhoneMotion::kActiveMs) == 0.0f);
  assert(std::strcmp(p.deviceId(), "iphone-0a1b2c3d") == 0);

  std::puts("test_rgb_engine: ALL TESTS PASSED");
  return 0;
}
