#include "realtime/session.h"

#include "realtime/frame_codec.h"
#include "realtime/protocol_adapter.h"

#include <ArduinoJson.h>

#include <stdio.h>
#include <string.h>

namespace sentinel {
namespace realtime {
namespace {

// Shared across every SessionState instance, including fresh peers that reuse
// the same connection slot after a disconnect.
uint64_t gSessionGeneration = 0;

const char* valueOrEmpty(JsonVariantConst variant) {
  return variant.is<const char*>() ? variant.as<const char*>() : "";
}

bool validIdentifier(const char* text, bool upperAllowed) {
  if (!text || !*text || strlen(text) > 64) return false;
  for (size_t i = 0; i < strlen(text); ++i) {
    const char c = text[i];
    const bool alpha = (c >= 'a' && c <= 'z') || (upperAllowed && c >= 'A' && c <= 'Z');
    if (!(alpha || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
          c == ':' || c == '-')) {
      return false;
    }
  }
  return true;
}

bool allowedType(const char* type) {
  return strcmp(type, "motion.sample") == 0 ||
         strcmp(type, "connection.hello") == 0 ||
         strcmp(type, "connection.heartbeat") == 0 ||
         strcmp(type, "connection.goodbye") == 0 ||
         strcmp(type, "error") == 0;
}

bool readVector(JsonVariantConst value, MotionVector& out) {
  if (!value.is<JsonObjectConst>()) return false;
  JsonObjectConst object = value.as<JsonObjectConst>();
  if (object.size() != 3) return false;
  double* fields[3] = {&out.x, &out.y, &out.z};
  const char* keys[3] = {"x", "y", "z"};
  for (int i = 0; i < 3; ++i) {
    JsonVariantConst v = object[keys[i]];
    if (!v.is<double>() && !v.is<long>()) return false;
    const double d = v.as<double>();
    if (!(d >= -500.0 && d <= 500.0)) return false;  // also rejects NaN
    *fields[i] = d;
  }
  return true;
}

bool oneOf(const char* value, const char* const* options, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(value, options[i]) == 0) return true;
  }
  return false;
}

InboundMessage reject(uint8_t code, const char* text) {
  InboundMessage result;
  result.errorCode = code;
  result.errorText = text;
  return result;
}

}  // namespace

const char* protocolErrorCode(uint8_t code) {
  switch (code) {
    case kInvalidEnvelopeCode:
      return "INVALID_ENVELOPE";
    case kUnsupportedVersionCode:
      return "UNSUPPORTED_PROTOCOL_VERSION";
    case kUnknownTypeCode:
      return "UNKNOWN_MESSAGE_TYPE";
    case kUnauthorizedCode:
      return "UNAUTHORIZED";
    case kRateLimitedCode:
      return "RATE_LIMITED";
    default:
      return "INTERNAL_ERROR";
  }
}

void SessionState::begin(uint8_t slot, const char* bootId, bool system) {
  ++gSessionGeneration;
  const char* id = bootId && *bootId ? bootId : "boot";
  if (system) {
    snprintf(sessionId_, sizeof(sessionId_), "%s-s%llu", id,
             static_cast<unsigned long long>(gSessionGeneration));
  } else {
    snprintf(sessionId_, sizeof(sessionId_), "%s-p%u-%llu", id,
             static_cast<unsigned>(slot),
             static_cast<unsigned long long>(gSessionGeneration));
  }
  sequence_ = 0;
  lastInboundSequence_ = 0;
  hasInboundSequence_ = false;
  inboundSource_[0] = '\0';
  inboundSessionId_[0] = '\0';
  helloReceived_ = false;
  privileged_ = false;
}

void SessionState::bindInboundSource(const char* source) {
  if (hasInboundSource()) return;
  snprintf(inboundSource_, sizeof(inboundSource_), "%s", source);
}

void SessionState::bindInboundSessionId(const char* sessionId) {
  if (hasInboundSessionId() || !sessionId || !*sessionId) return;
  snprintf(inboundSessionId_, sizeof(inboundSessionId_), "%s", sessionId);
}

uint64_t SessionState::nextSequence() {
  if (sequence_ >= 9007199254740991ULL) return 0;
  return ++sequence_;
}

void SessionState::recordInbound(uint64_t sequence) {
  lastInboundSequence_ = sequence;
  hasInboundSequence_ = true;
}

InboundMessage InboundValidator::validate(const char* json, size_t length,
                                           uint32_t nowMs,
                                           bool authenticated,
                                           SessionState& session) const {
  (void)nowMs;
  if (!json || length == 0 || length > kWsMaxFrameBytes) {
    return reject(kInvalidEnvelopeCode, "invalid envelope");
  }

  JsonDocument document;
  if (deserializeJson(document, json, length) || !document.is<JsonObject>()) {
    return reject(kInvalidEnvelopeCode, "invalid envelope");
  }

  const char* version = valueOrEmpty(document["protocolVersion"]);
  if (strcmp(version, kProtocolVersion) != 0) {
    return reject(kUnsupportedVersionCode, "unsupported protocol version");
  }
  const char* type = valueOrEmpty(document["type"]);
  if (!allowedType(type)) {
    return reject(kUnknownTypeCode, "message type not accepted from clients");
  }
  if (!document["sequence"].is<uint64_t>() &&
      !document["sequence"].is<uint32_t>()) {
    return reject(kInvalidEnvelopeCode, "invalid envelope");
  }
  const uint64_t sequence = document["sequence"].as<uint64_t>();
  if (sequence > 9007199254740991ULL) {
    return reject(kInvalidEnvelopeCode, "invalid envelope");
  }
  if (session.hasInboundSequence() && sequence <= session.lastInboundSequence()) {
    return reject(kRateLimitedCode, "sequence must increase");
  }

  const char* source = valueOrEmpty(document["source"]);
  if (!validIdentifier(source, false)) {
    return reject(kInvalidEnvelopeCode, "invalid envelope");
  }

  // Protocol v1 requires a 1-64 character sessionId. Legacy clients may omit
  // it; when present it must be well formed and match this peer's session.
  const JsonVariantConst sessionField = document["sessionId"];
  if (!sessionField.isNull() && !sessionField.is<const char*>()) {
    return reject(kInvalidEnvelopeCode, "invalid envelope");
  }
  const char* sessionId = valueOrEmpty(sessionField);
  if (!sessionField.isNull()) {
    if (!validIdentifier(sessionId, true)) {
      return reject(kInvalidEnvelopeCode, "invalid envelope");
    }
    if (session.hasInboundSessionId() &&
        strcmp(sessionId, session.inboundSessionId()) != 0) {
      return reject(kUnauthorizedCode, "invalid envelope");
    }
  } else if (session.hasInboundSessionId()) {
    // Once a client has identified its session it cannot drop the field.
    return reject(kUnauthorizedCode, "invalid envelope");
  }
  // The peer's source is assigned by its first accepted frame and cannot be
  // changed later in the same session.
  if (session.hasInboundSource() &&
      strcmp(source, session.inboundSource()) != 0) {
    return reject(kUnauthorizedCode, "invalid envelope");
  }

  const char* timestamp = valueOrEmpty(document["timestamp"]);
  int64_t epochMs = 0;
  if (!UtcClock::parse(timestamp, strlen(timestamp), epochMs)) {
    return reject(kInvalidEnvelopeCode, "invalid envelope");
  }
  if (!document["payload"].is<JsonObject>()) {
    return reject(kInvalidEnvelopeCode, "invalid envelope");
  }

  InboundMessage result;
  result.valid = true;
  result.sequence = sequence;
  snprintf(result.type, sizeof(result.type), "%s", type);
  snprintf(result.source, sizeof(result.source), "%s", source);
  result.epochMs = epochMs;

  if (strcmp(type, "connection.hello") == 0) {
    JsonObject payload = document["payload"].as<JsonObject>();
    const char* role = valueOrEmpty(payload["role"]);
    const char* supported = valueOrEmpty(payload["supportedVersion"]);
    if (strcmp(supported, kProtocolVersion) != 0) {
      return reject(kUnsupportedVersionCode, "unsupported protocol version");
    }
    if (strcmp(role, "firmware") == 0) {
      return reject(kInvalidEnvelopeCode, "invalid envelope");
    }
    if (strcmp(role, "gateway") == 0) {
      if (!authenticated) {
        return reject(kUnauthorizedCode, "privileged role requires a token");
      }
      result.syncClock = true;
      result.privileged = true;
    } else if (strcmp(role, "ios") == 0 || strcmp(role, "web") == 0) {
      // Native app and web dashboard are time authorities only when they
      // present the token (bearer header or sentinel.auth subprotocol);
      // without it they are read-only telemetry clients.
      if (authenticated) {
        result.syncClock = true;
        result.privileged = true;
      }
    } else if (strcmp(role, "sensor-client") != 0) {
      return reject(kInvalidEnvelopeCode, "invalid envelope");
    }
    snprintf(result.role, sizeof(result.role), "%s", role);
    snprintf(result.supportedVersion, sizeof(result.supportedVersion), "%s",
             supported);
  }

  if (strcmp(type, "motion.sample") == 0) {
    // Only an authenticated, privileged session (the iPhone app with the
    // token) may stream motion; it drives the RGB LED and is relayed.
    if (!authenticated || !session.privileged()) {
      return reject(kUnauthorizedCode, "motion requires an authenticated session");
    }
    JsonObjectConst payload = document["payload"].as<JsonObjectConst>();
    static const char* const kAllowed[] = {
        "deviceId", "sourceType", "sampleIndex", "sensorTimestamp",
        "acceleration", "gravity", "linearAcceleration", "state", "simulated"};
    for (JsonPairConst pair : payload) {
      if (!oneOf(pair.key().c_str(), kAllowed, 9)) {
        return reject(kInvalidEnvelopeCode, "invalid envelope");
      }
    }
    const char* deviceId = valueOrEmpty(payload["deviceId"]);
    const char* sourceType = valueOrEmpty(payload["sourceType"]);
    const char* state = valueOrEmpty(payload["state"]);
    static const char* const kSources[] = {"NativeClient", "SensorLogger", "CoreMotion"};
    static const char* const kStates[] = {"STABLE", "MOVEMENT", "ALERT", "SENSOR_OFFLINE"};
    if (!validIdentifier(deviceId, true) || !oneOf(sourceType, kSources, 3) ||
        !oneOf(state, kStates, 4) || !payload["simulated"].is<bool>() ||
        payload["simulated"].as<bool>() ||
        !(payload["sampleIndex"].is<uint64_t>() || payload["sampleIndex"].is<uint32_t>()) ||
        !readVector(payload["acceleration"], result.acceleration) ||
        !readVector(payload["gravity"], result.gravity) ||
        !readVector(payload["linearAcceleration"], result.linear)) {
      return reject(kInvalidEnvelopeCode, "invalid envelope");
    }
    const JsonVariantConst sensorTs = payload["sensorTimestamp"];
    if (!sensorTs.isNull()) {
      const char* ts = valueOrEmpty(sensorTs);
      int64_t ignored = 0;
      if (!UtcClock::parse(ts, strlen(ts), ignored)) {
        return reject(kInvalidEnvelopeCode, "invalid envelope");
      }
      snprintf(result.motionSensorTimestamp, sizeof(result.motionSensorTimestamp), "%s", ts);
    }
    result.isMotion = true;
    result.motionSampleIndex = payload["sampleIndex"].as<uint64_t>();
    snprintf(result.motionDeviceId, sizeof(result.motionDeviceId), "%s", deviceId);
    snprintf(result.motionSourceType, sizeof(result.motionSourceType), "%s", sourceType);
    snprintf(result.motionState, sizeof(result.motionState), "%s", state);
  }

  if (strcmp(type, "connection.heartbeat") == 0 && authenticated &&
      session.privileged()) {
    // Periodic re-sync from the same authenticated time authority bounds the
    // drift of the ESP32 crystal during long soak runs.
    result.syncClock = true;
    result.privileged = true;
  }

  session.bindInboundSource(source);
  session.bindInboundSessionId(sessionId);
  snprintf(result.source, sizeof(result.source), "%s",
           session.inboundSource());
  return result;
}

}  // namespace realtime
}  // namespace sentinel
