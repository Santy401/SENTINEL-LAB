#include "rf/motion_ingest.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctype.h>

namespace sentinel {
namespace rf {
namespace {

constexpr uint64_t kNsPerMs = 1000000ULL;
constexpr uint64_t kMaxPairingSkewNs = 100ULL * kNsPerMs;

struct Vec3 {
  uint64_t timeNs = 0;
  float x = 0, y = 0, z = 0;
  bool valid = false;
};

bool validDeviceId(const char* id) {
  if (id == nullptr) return false;
  const size_t len = std::strlen(id);
  if (len == 0 || len > kMotionIngestMaxDeviceId) return false;
  if (std::isspace(static_cast<unsigned char>(id[0])) ||
      std::isspace(static_cast<unsigned char>(id[len - 1]))) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(id[i]);
    if (!std::isprint(c) || std::isspace(c)) return false;
  }
  return true;
}

bool finiteAxis(float value) {
  return std::isfinite(value) && std::fabs(value) <= 500.0f;
}

bool readVec3(JsonVariantConst reading, Vec3& out) {
  JsonVariantConst values = reading["values"];
  if (!values.is<JsonObjectConst>()) return false;
  JsonVariantConst time = reading["time"];
  if (!time.is<uint64_t>()) return false;
  JsonVariantConst x = values["x"];
  JsonVariantConst y = values["y"];
  JsonVariantConst z = values["z"];
  if (!x.is<float>() || !y.is<float>() || !z.is<float>()) return false;

  out.timeNs = time.as<uint64_t>();
  out.x = x.as<float>();
  out.y = y.as<float>();
  out.z = z.as<float>();
  if (!finiteAxis(out.x) || !finiteAxis(out.y) || !finiteAxis(out.z)) return false;
  out.valid = true;
  return true;
}

uint64_t absDiff(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }

MotionIngestResult makeResult(MotionIngestStatus status, int code,
                              const char* error, uint16_t accepted = 0,
                              uint16_t skipped = 0, uint32_t retry = 0) {
  MotionIngestResult result;
  result.status = status;
  result.httpCode = code;
  result.error = error;
  result.accepted = accepted;
  result.skipped = skipped;
  result.retryAfterMs = retry;
  return result;
}

}  // namespace

bool MotionIngest::configure(const char* deviceId, uint32_t minIntervalMs) {
  if (deviceId != nullptr && *deviceId != '\0' && !validDeviceId(deviceId)) {
    return false;
  }
  if (deviceId == nullptr || *deviceId == '\0') {
    config_.allowedDeviceId[0] = '\0';
  } else {
    std::strncpy(config_.allowedDeviceId, deviceId, kMotionIngestMaxDeviceId);
    config_.allowedDeviceId[kMotionIngestMaxDeviceId] = '\0';
  }
  config_.minIntervalMs = std::max(kMotionIngestMinIntervalLowMs,
                                   std::min(kMotionIngestMinIntervalHighMs,
                                            minIntervalMs));
  return true;
}

bool MotionIngest::authorize(const char* deviceId) {
  if (!validDeviceId(deviceId)) return false;
  std::strncpy(config_.allowedDeviceId, deviceId, kMotionIngestMaxDeviceId);
  config_.allowedDeviceId[kMotionIngestMaxDeviceId] = '\0';
  if (std::strcmp(status_.pendingDeviceId, deviceId) == 0) {
    status_.pendingDeviceId[0] = '\0';
  }
  return true;
}

void MotionIngest::clearAuthorization() {
  config_.allowedDeviceId[0] = '\0';
  status_.pendingDeviceId[0] = '\0';
}

MotionIngestResult MotionIngest::ingest(const char* body, size_t length,
                                        uint32_t nowMs) {
  // Keep /api/motion/status truthful for rejected requests too.
  const auto record = [this](MotionIngestResult r) {
    status_.lastHttpCode = r.httpCode;
    status_.lastError = r.error;
    return r;
  };

  if (engine_ == nullptr)
    return record(makeResult(MotionIngestStatus::NotReady, 503,
                             "motion engine not ready"));
  if (body == nullptr || length > kMotionIngestMaxBodyBytes) {
    return record(makeResult(MotionIngestStatus::BodyTooLarge, 413,
                             "motion body too large"));
  }

  JsonDocument doc;
  const DeserializationError parseError = deserializeJson(doc, body, length);
  if (parseError || !doc.is<JsonObject>()) {
    return record(makeResult(MotionIngestStatus::InvalidJson, 400,
                             "invalid json object"));
  }

  // Simulation must never enter the native receiver under a "real" label.
  if ((doc["simulated"].is<bool>() && doc["simulated"].as<bool>()) ||
      (doc["source"].is<const char*>() &&
       std::strcmp(doc["source"].as<const char*>(), "simulated") == 0)) {
    return record(makeResult(
        MotionIngestStatus::InvalidPayload, 400,
        "simulation is not accepted on the native endpoint"));
  }

  JsonVariantConst device = doc["deviceId"];
  JsonVariantConst session = doc["sessionId"];
  JsonVariantConst messageId = doc["messageId"];
  JsonArrayConst payload = doc["payload"].as<JsonArrayConst>();
  if (!device.is<const char*>() || !validDeviceId(device.as<const char*>()) ||
      !session.is<const char*>() || !validDeviceId(session.as<const char*>()) ||
      !messageId.is<uint64_t>() || payload.isNull() ||
      payload.size() > kMotionIngestMaxReadings) {
    return record(makeResult(
        MotionIngestStatus::InvalidPayload, 400,
        "deviceId, sessionId, messageId and payload required"));
  }

  const char* deviceId = device.as<const char*>();
  const char* sessionId = session.as<const char*>();
  const uint64_t message = messageId.as<uint64_t>();
  std::strncpy(status_.lastDeviceId, deviceId, kMotionIngestMaxDeviceId);
  status_.lastDeviceId[kMotionIngestMaxDeviceId] = '\0';

  if (config_.allowedDeviceId[0] == '\0' ||
      std::strcmp(config_.allowedDeviceId, deviceId) != 0) {
    std::strncpy(status_.pendingDeviceId, deviceId, kMotionIngestMaxDeviceId);
    status_.pendingDeviceId[kMotionIngestMaxDeviceId] = '\0';
    return record(makeResult(
        MotionIngestStatus::Unauthorized, 403,
        "device not authorized; review /api/motion/status"));
  }
  status_.pendingDeviceId[0] = '\0';

  if (haveMessage_ && std::strcmp(status_.lastSessionId, sessionId) == 0 &&
      message <= status_.lastMessageId) {
    return record(makeResult(MotionIngestStatus::StaleMessage, 409,
                             "stale or duplicate messageId"));
  }
  // A new recording session has an independent monotonic sample timeline.
  if (haveMessage_ && std::strcmp(status_.lastSessionId, sessionId) != 0) {
    haveClientSample_ = false;
    lastAcceptedClientTimeNs_ = 0;
  }

  Vec3 acceleration[kMotionIngestMaxReadings]{};
  Vec3 gravity[kMotionIngestMaxReadings]{};
  size_t accelerationCount = 0;
  size_t gravityCount = 0;
  for (JsonVariantConst reading : payload) {
    const char* name = reading["name"] | "";
    if (reading["simulated"].is<bool>() && reading["simulated"].as<bool>()) {
      return record(makeResult(MotionIngestStatus::InvalidPayload, 400,
                               "simulated reading is not accepted"));
    }
    if (std::strcmp(name, "accelerometer") == 0) {
      if (accelerationCount < kMotionIngestMaxReadings &&
          readVec3(reading, acceleration[accelerationCount])) {
        ++accelerationCount;
      }
    } else if (std::strcmp(name, "gravity") == 0) {
      if (gravityCount < kMotionIngestMaxReadings &&
          readVec3(reading, gravity[gravityCount])) {
        ++gravityCount;
      }
    }
  }

  uint16_t accepted = 0;
  uint16_t skipped = 0;
  uint64_t newestTimeNs = 0;
  for (size_t i = 0; i < accelerationCount; ++i) {
    const Vec3& a = acceleration[i];
    if (a.timeNs > newestTimeNs) newestTimeNs = a.timeNs;
  }

  for (size_t i = 0; i < accelerationCount; ++i) {
    const Vec3& a = acceleration[i];
    const Vec3* best = nullptr;
    uint64_t bestDelta = UINT64_MAX;
    for (size_t j = 0; j < gravityCount; ++j) {
      const uint64_t delta = absDiff(a.timeNs, gravity[j].timeNs);
      if (delta < bestDelta) {
        bestDelta = delta;
        best = &gravity[j];
      }
    }
    // On iOS, Sensor Logger's "accelerometer" is user acceleration without
    // gravity. Never invent a gravity vector: skip unpaired samples.
    if (best == nullptr || bestDelta > kMaxPairingSkewNs) {
      ++skipped;
      continue;
    }

    const float x = a.x + best->x;
    const float y = a.y + best->y;
    const float z = a.z + best->z;
    if (!finiteAxis(x) || !finiteAxis(y) || !finiteAxis(z)) {
      ++skipped;
      continue;
    }
    if (haveClientSample_ &&
        (a.timeNs <= lastAcceptedClientTimeNs_ ||
         a.timeNs - lastAcceptedClientTimeNs_ <
             static_cast<uint64_t>(config_.minIntervalMs) * kNsPerMs)) {
      ++skipped;
      continue;
    }

    uint32_t delayMs = 0;
    if (newestTimeNs >= a.timeNs) {
      const uint64_t deltaMs = (newestTimeNs - a.timeNs) / kNsPerMs;
      delayMs = deltaMs > nowMs ? nowMs : static_cast<uint32_t>(deltaMs);
    }
    v02::AccelSample sample;
    sample.x = x;
    sample.y = y;
    sample.z = z;
    sample.timestampMs = nowMs - delayMs;
    sample.source = v02::AccelSource::NativeClient;
    engine_->feed(sample);

    lastAcceptedClientTimeNs_ = a.timeNs;
    haveClientSample_ = true;
    status_.lastSampleEpochNs = a.timeNs;
    ++accepted;
  }

  std::strncpy(status_.lastSessionId, sessionId, kMotionIngestMaxDeviceId);
  status_.lastSessionId[kMotionIngestMaxDeviceId] = '\0';
  status_.lastMessageId = message;
  haveMessage_ = true;
  status_.lastAcceptedBatch = accepted;
  status_.lastSkippedBatch = skipped;
  status_.acceptedSamples += accepted;
  status_.skippedSamples += skipped;
  if (accepted > 0) status_.lastAcceptedMs = nowMs;

  MotionIngestResult result;
  if (accepted == 0) {
    result = makeResult(MotionIngestStatus::NoPairedSamples, 422,
                        "no valid paired accelerometer + gravity samples", 0,
                        skipped);
  } else {
    result = makeResult(MotionIngestStatus::Ok, 200, "ok", accepted, skipped);
  }
  return record(result);
}

MotionIngestSnapshot MotionIngest::snapshot(uint32_t nowMs) const {
  MotionIngestSnapshot out = status_;
  out.ready = engine_ != nullptr;
  out.authorized = config_.allowedDeviceId[0] != '\0';
  out.minIntervalMs = config_.minIntervalMs;
  out.signalLost = engine_ == nullptr || !engine_->fresh(nowMs);
  out.sampleAgeMs = status_.lastAcceptedMs != 0
                        ? uint32_t(nowMs - status_.lastAcceptedMs)
                        : 0;
  return out;
}

}  // namespace rf
}  // namespace sentinel
