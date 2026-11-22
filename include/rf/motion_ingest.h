#pragma once

#include "v02/motion_engine.h"

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace rf {

// Sensor Logger HTTP Push batches are bounded so a malformed or hostile body
// cannot consume unbounded heap on the ESP32. 16 KiB covers >1 s of paired
// accelerometer + gravity readings at 50 Hz.
constexpr size_t kMotionIngestMaxBodyBytes = 16384;
constexpr size_t kMotionIngestMaxReadings = 64;
constexpr size_t kMotionIngestMaxDeviceId = 64;
constexpr uint32_t kMotionIngestDefaultMinIntervalMs = 20;
constexpr uint32_t kMotionIngestMinIntervalLowMs = 10;
constexpr uint32_t kMotionIngestMinIntervalHighMs = 1000;

enum class MotionIngestStatus : uint8_t {
  Ok = 0,
  NotReady,
  BodyTooLarge,
  InvalidJson,
  InvalidPayload,
  Unauthorized,
  StaleMessage,
  RateLimited,
  NoPairedSamples
};

struct MotionIngestResult {
  MotionIngestStatus status = MotionIngestStatus::NotReady;
  int httpCode = 503;
  const char* error = "not ready";
  uint16_t accepted = 0;
  uint16_t skipped = 0;
  uint32_t retryAfterMs = 0;
};

struct MotionIngestConfig {
  // Empty means pairing mode: the first plausible device is reported as
  // pending, but no measurement is accepted until it is explicitly authorized.
  char allowedDeviceId[kMotionIngestMaxDeviceId + 1]{};
  uint32_t minIntervalMs = kMotionIngestDefaultMinIntervalMs;
};

struct MotionIngestSnapshot {
  bool ready = false;
  bool authorized = false;
  char allowedDeviceId[kMotionIngestMaxDeviceId + 1]{};
  char pendingDeviceId[kMotionIngestMaxDeviceId + 1]{};
  char lastDeviceId[kMotionIngestMaxDeviceId + 1]{};
  char lastSessionId[kMotionIngestMaxDeviceId + 1]{};
  uint64_t lastMessageId = 0;
  uint64_t lastSampleEpochNs = 0;
  uint32_t lastAcceptedMs = 0;
  uint32_t acceptedSamples = 0;
  uint32_t skippedSamples = 0;
  uint16_t lastAcceptedBatch = 0;
  uint16_t lastSkippedBatch = 0;
  int lastHttpCode = 503;
  const char* lastError = "not ready";
  bool signalLost = true;
  uint32_t sampleAgeMs = 0;
  uint32_t minIntervalMs = kMotionIngestDefaultMinIntervalMs;
#ifdef SENTINEL_REALTIME
  // P2-only counter used by the v1 motion.status adapter. It does not change
  // the v0.3.1 HTTP payload or the release firmware binary.
  uint64_t simulationRejected = 0;
#endif
};

// Receives Sensor Logger HTTP Push batches and feeds real, paired
// accelerometer + gravity readings into the existing MotionEngine. Pure C++:
// no Arduino, WiFi or Serial dependency. Time is injected for host tests.
class MotionIngest {
 public:
  void attach(v02::MotionEngine& engine) { engine_ = &engine; }

  const MotionIngestConfig& config() const { return config_; }

  // deviceId must be 1..64 printable characters. minIntervalMs is clamped to
  // 10..1000 ms. Returns false for malformed identifiers.
  bool configure(const char* deviceId, uint32_t minIntervalMs);
  bool authorize(const char* deviceId);
  void clearAuthorization();

  MotionIngestResult ingest(const char* body, size_t length, uint32_t nowMs);
  MotionIngestSnapshot snapshot(uint32_t nowMs) const;

 private:
  v02::MotionEngine* engine_ = nullptr;
  MotionIngestConfig config_;
  MotionIngestSnapshot status_;
  bool haveMessage_ = false;
  bool haveClientSample_ = false;
  uint64_t lastAcceptedClientTimeNs_ = 0;
};

}  // namespace rf
}  // namespace sentinel
