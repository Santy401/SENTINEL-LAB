#pragma once

#include "realtime/phone_motion.h"
#include "realtime/time_source.h"

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace realtime {

constexpr char kFirmwareSource[] = "esp32-s3-realtime";
constexpr uint8_t kInvalidEnvelopeCode = 1;
constexpr uint8_t kUnsupportedVersionCode = 2;
constexpr uint8_t kUnknownTypeCode = 3;
constexpr uint8_t kUnauthorizedCode = 4;
constexpr uint8_t kRateLimitedCode = 5;

struct InboundMessage {
  bool valid = false;
  uint8_t errorCode = 0;
  const char* errorText = "";
  char type[32]{};
  char source[65]{};
  uint64_t sequence = 0;
  char role[32]{};
  char supportedVersion[16]{};
  int64_t epochMs = 0;
  // True only for an authenticated, privileged session (role gateway or ios
  // with a valid bearer token). Unauthenticated peers never move the clock.
  bool syncClock = false;
  bool privileged = false;
  // motion.sample (authenticated phone only); vectors in m/s².
  bool isMotion = false;
  char motionDeviceId[65]{};
  char motionSourceType[16]{};
  char motionState[16]{};
  char motionSensorTimestamp[25]{};
  uint64_t motionSampleIndex = 0;
  MotionVector acceleration;
  MotionVector gravity;
  MotionVector linear;
};

// Per-connection identity and outbound sequence. The triple source +
// sessionId + sequence is unique because sessionId embeds the boot identifier,
// a producer kind, the connection slot and a process-wide generation counter.
// A reconnect into the same slot therefore never reuses a sessionId.
//
// Inbound identity belongs to the client: Protocol v1 deduplicates on the
// sender's own source + sessionId + sequence, so a client cannot be required
// to echo the firmware's sessionId (it only learns it after the first server
// frame, which in turn needs a synchronized clock). The first accepted source
// and sessionId are bound to the connection; later frames cannot change them.
class SessionState {
 public:
  void begin(uint8_t slot, const char* bootId, bool system = false);
  uint64_t nextSequence();
  uint64_t lastInboundSequence() const { return lastInboundSequence_; }
  bool hasInboundSequence() const { return hasInboundSequence_; }
  void recordInbound(uint64_t sequence);
  const char* sessionId() const { return sessionId_; }
  const char* source() const { return kFirmwareSource; }
  bool hasInboundSource() const { return inboundSource_[0] != '\0'; }
  const char* inboundSource() const { return inboundSource_; }
  void bindInboundSource(const char* source);
  bool hasInboundSessionId() const { return inboundSessionId_[0] != '\0'; }
  const char* inboundSessionId() const { return inboundSessionId_; }
  void bindInboundSessionId(const char* sessionId);
  bool helloReceived() const { return helloReceived_; }
  void markHello() { helloReceived_ = true; }
  bool privileged() const { return privileged_; }
  void markPrivileged() { privileged_ = true; }

 private:
  char sessionId_[65]{};
  char inboundSource_[65]{};
  char inboundSessionId_[65]{};
  uint64_t sequence_ = 0;
  uint64_t lastInboundSequence_ = 0;
  bool hasInboundSequence_ = false;
  bool helloReceived_ = false;
  bool privileged_ = false;
};

// Validation of inbound Protocol v1 frames. Validation failures are reported
// with fixed strings; rejected payloads are never echoed. Accepted frames bind
// their source to the session, so the validator updates session identity state.
const char* protocolErrorCode(uint8_t code);

class InboundValidator {
 public:
  InboundMessage validate(const char* json, size_t length, uint32_t nowMs,
                          bool authenticated, SessionState& session) const;
};

}  // namespace realtime
}  // namespace sentinel
