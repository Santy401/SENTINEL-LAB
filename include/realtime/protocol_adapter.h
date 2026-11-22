#pragma once

#include "realtime/time_source.h"

#include <cmath>
#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace realtime {

// All firmware-generated Protocol v1 frames stay below the WebSocket frame
// cap. Larger envelopes remain valid at the contract level, but the ESP32
// adapter intentionally uses a smaller bounded transport buffer.
constexpr size_t kProtocolMessageBytes = 1016;
constexpr char kProtocolVersion[] = "1.0.0";
// Version of the realtime firmware image. The HTTP API keeps reporting
// kHttpApiVersion in /api/state ("fw") so v0.3.1 clients stay compatible.
constexpr char kFirmwareVersion[] = "0.4.2";
constexpr char kHttpApiVersion[] = "0.3.1";

struct RfClientView {
  uint32_t clientId = 0;
  bool connected = false;
  double rssiDbm = NAN;
  double filteredRssiDbm = NAN;
  uint32_t lastSeenMs = 0;
  bool hasDistance = false;
  bool distanceCalibrated = false;
  double distanceM = NAN;
};

struct SystemStatusView {
  uint32_t uptimeMs = 0;
  const char* operationalState = "IDLE";
  const char* transports[2] = {"http", "websocket"};
  size_t transportCount = 2;
  const char* const* capabilities = nullptr;
  size_t capabilityCount = 0;
  uint32_t freeHeapBytes = 0;
};

struct MotionStatusView {
  const char* deviceId = "";
  const char* sourceType = "None";
  bool paired = false;
  uint64_t acceptedSamples = 0;
  uint32_t lastSampleAgeMs = 0;
  bool signalLost = true;
  uint64_t simulationRejected = 0;
  uint16_t lastHttpCode = 0;
};

struct EventDetailsView {
  uint32_t clientId = 0;
  double rssiDbm = NAN;
  const char* message = "";
  const char* previousState = "";
  const char* newState = "";
};

// Builds strict Protocol v1 envelopes into a caller-owned fixed buffer. No
// message is produced before UtcClock has been synchronized, so the firmware
// never labels uptime as wall-clock time.
class ProtocolAdapter {
 public:
  explicit ProtocolAdapter(UtcClock& clock) : clock_(clock) {}

  bool buildHello(const char* source, const char* sessionId, uint64_t sequence,
                  uint32_t nowMs, const char* deviceId, const char* role,
                  const char* const* capabilities, size_t capabilityCount,
                  char* output, size_t capacity) const;

  bool buildHeartbeat(const char* source, const char* sessionId,
                      uint64_t sequence, uint32_t nowMs, const char* bootId,
                      char* output, size_t capacity) const;

  bool buildRfTelemetry(const char* source, const char* sessionId,
                        uint64_t sequence, uint32_t nowMs, const char* bootId,
                        uint8_t channel, const RfClientView* clients,
                        size_t clientCount, char* output,
                        size_t capacity) const;

  bool buildSystemStatus(const char* source, const char* sessionId,
                         uint64_t sequence, uint32_t nowMs, const char* bootId,
                         const SystemStatusView& status, char* output,
                         size_t capacity) const;

  bool buildMotionStatus(const char* source, const char* sessionId,
                         uint64_t sequence, uint32_t nowMs,
                         const MotionStatusView& status, char* output,
                         size_t capacity) const;

  bool buildEvent(const char* source, const char* sessionId,
                  uint64_t sequence, uint32_t nowMs, const char* eventId,
                  const char* name, uint32_t occurredMs,
                  const EventDetailsView& details, char* output,
                  size_t capacity) const;

  bool buildError(const char* source, const char* sessionId,
                  uint64_t sequence, uint32_t nowMs, const char* code,
                  const char* message, bool retryable,
                  const char* relatedSource, uint64_t relatedSequence,
                  char* output, size_t capacity) const;

  bool buildGoodbye(const char* source, const char* sessionId,
                    uint64_t sequence, uint32_t nowMs, const char* reason,
                    char* output, size_t capacity) const;

 private:
  template <typename F>
  bool envelope(const char* type, const char* source, const char* sessionId,
                uint64_t sequence, uint32_t nowMs, F fillPayload,
                char* output, size_t capacity) const;

  UtcClock& clock_;
};

}  // namespace realtime
}  // namespace sentinel
