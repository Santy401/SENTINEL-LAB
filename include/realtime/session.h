#pragma once

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
  bool syncClock = false;
};

// Per-connection identity and outbound sequence. The triple source +
// sessionId + sequence is unique because sessionId embeds the boot identifier,
// a producer kind, the connection slot and a process-wide generation counter.
// A reconnect into the same slot therefore never reuses a sessionId.
class SessionState {
 public:
  void begin(uint8_t slot, const char* bootId, bool system = false);
  uint64_t nextSequence();
  uint64_t lastInboundSequence() const { return lastInboundSequence_; }
  bool hasInboundSequence() const { return hasInboundSequence_; }
  void recordInbound(uint64_t sequence);
  const char* sessionId() const { return sessionId_; }
  const char* source() const { return kFirmwareSource; }
  bool helloReceived() const { return helloReceived_; }
  void markHello() { helloReceived_ = true; }
  bool privileged() const { return privileged_; }
  void markPrivileged() { privileged_ = true; }

 private:
  char sessionId_[65]{};
  uint64_t sequence_ = 0;
  uint64_t lastInboundSequence_ = 0;
  bool hasInboundSequence_ = false;
  bool helloReceived_ = false;
  bool privileged_ = false;
};

// Stateless validation of inbound Protocol v1 frames. Validation failures are
// reported with fixed strings; rejected payloads are never echoed.
const char* protocolErrorCode(uint8_t code);

class InboundValidator {
 public:
  InboundMessage validate(const char* json, size_t length, uint32_t nowMs,
                          bool authenticated, const SessionState& session) const;
};

}  // namespace realtime
}  // namespace sentinel
