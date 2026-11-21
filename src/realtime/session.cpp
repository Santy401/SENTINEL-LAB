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
  return strcmp(type, "connection.hello") == 0 ||
         strcmp(type, "connection.heartbeat") == 0 ||
         strcmp(type, "connection.goodbye") == 0 ||
         strcmp(type, "error") == 0;
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
  helloReceived_ = false;
  privileged_ = false;
}

void SessionState::bindInboundSource(const char* source) {
  if (hasInboundSource()) return;
  snprintf(inboundSource_, sizeof(inboundSource_), "%s", source);
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
    if (session.sessionId()[0] != '\0' &&
        strcmp(sessionId, session.sessionId()) != 0) {
      return reject(kUnauthorizedCode, "invalid envelope");
    }
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
    } else if (strcmp(role, "web") != 0 &&
               strcmp(role, "sensor-client") != 0 &&
               strcmp(role, "ios") != 0) {
      return reject(kInvalidEnvelopeCode, "invalid envelope");
    }
    snprintf(result.role, sizeof(result.role), "%s", role);
    snprintf(result.supportedVersion, sizeof(result.supportedVersion), "%s",
             supported);
  }

  session.bindInboundSource(source);
  snprintf(result.source, sizeof(result.source), "%s",
           session.inboundSource());
  return result;
}

}  // namespace realtime
}  // namespace sentinel
