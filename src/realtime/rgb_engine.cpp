#include "realtime/rgb_engine.h"

#include <math.h>

namespace sentinel {
namespace realtime {
namespace {

constexpr float kPi = 3.14159265f;

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

void set(float out[3], float r, float g, float b, float level) {
  out[0] = r * level;
  out[1] = g * level;
  out[2] = b * level;
}

void lerp(const float a[3], const float b[3], float t, float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
}

}  // namespace

const char* rgbModeName(RgbMode mode) {
  switch (mode) {
    case RgbMode::Boot: return "boot";
    case RgbMode::Offline: return "offline";
    case RgbMode::Client: return "client";
    case RgbMode::Synced: return "synced";
    case RgbMode::Motion: return "motion";
    case RgbMode::Event: return "event";
  }
  return "offline";
}

void RgbEngine::begin(uint32_t nowMs) {
  bootMs_ = nowMs;
  lastMs_ = nowMs;
  started_ = true;
  for (float& c : current_) c = 0;
  output_ = RgbColor{};
  mode_ = RgbMode::Boot;
}

void RgbEngine::pulseEvent(uint32_t nowMs) {
  eventMs_ = nowMs;
  eventActive_ = true;
}

void RgbEngine::target(const RgbInputs& in, uint32_t bootMs, RgbMode& mode,
                       float out[3]) {
  const uint32_t sinceBoot = in.nowMs - bootMs;
  if (sinceBoot < kBootMs) {
    mode = RgbMode::Boot;
    set(out, 0.0f, 0.8f, 1.0f, static_cast<float>(sinceBoot) / kBootMs);
    return;
  }
  if (in.motionActive && in.intensity > 0.04f) {
    mode = RgbMode::Motion;
    // Teal (calm) -> amber (moderate) -> red (strong shake).
    static const float teal[3] = {0.0f, 0.85f, 0.7f};
    static const float amber[3] = {1.0f, 0.55f, 0.0f};
    static const float red[3] = {1.0f, 0.05f, 0.08f};
    const float i = clamp01(in.intensity);
    float hue[3];
    if (i < 0.5f) {
      lerp(teal, amber, i / 0.5f, hue);
    } else {
      lerp(amber, red, (i - 0.5f) / 0.5f, hue);
    }
    const float level = 0.35f + 0.65f * i;
    set(out, hue[0], hue[1], hue[2], level);
    return;
  }
  if (in.clients == 0) {
    mode = RgbMode::Offline;
    const float phase = static_cast<float>(sinceBoot % kBreathPeriodMs) /
                        kBreathPeriodMs;
    const float breath = 0.5f - 0.5f * cosf(2 * kPi * phase);
    set(out, 0.1f, 0.3f, 1.0f, 0.18f + 0.22f * breath);
    return;
  }
  if (!in.synced) {
    mode = RgbMode::Client;
    set(out, 0.0f, 0.9f, 0.75f, 0.4f);
    return;
  }
  mode = RgbMode::Synced;
  set(out, 0.1f, 1.0f, 0.45f, 0.45f);
}

bool RgbEngine::update(const RgbInputs& in) {
  if (!started_) begin(in.nowMs);
  float goal[3];
  RgbMode mode = RgbMode::Offline;
  target(in, bootMs_, mode, goal);

  if (eventActive_) {
    const uint32_t dt = in.nowMs - eventMs_;
    if (dt >= kEventMs) {
      eventActive_ = false;
    } else {
      // One soft white swell (half sine), blended over the base colour.
      const float w = sinf(kPi * static_cast<float>(dt) / kEventMs) * 0.7f;
      for (float& c : goal) c = c + (1.0f - c) * w;
      mode = RgbMode::Event;
    }
  }

  const uint32_t dt = in.nowMs - lastMs_;
  lastMs_ = in.nowMs;
  const float tau = mode == RgbMode::Motion ? 90.0f : 250.0f;
  const float alpha = 1.0f - expf(-static_cast<float>(dt) / tau);
  RgbColor next;
  uint8_t* channels[3] = {&next.r, &next.g, &next.b};
  for (int i = 0; i < 3; ++i) {
    current_[i] += (clamp01(goal[i]) - current_[i]) * alpha;
    // Approximate perceptual gamma so low levels fade smoothly.
    const float v = current_[i] * current_[i];
    *channels[i] = static_cast<uint8_t>(lroundf(v * kMaxLevel));
  }
  mode_ = mode;
  const bool changed =
      next.r != output_.r || next.g != output_.g || next.b != output_.b;
  output_ = next;
  return changed;
}

}  // namespace realtime
}  // namespace sentinel
