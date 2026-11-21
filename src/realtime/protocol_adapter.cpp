#include "realtime/protocol_adapter.h"
#include "realtime/status_policy.h"

#include <ArduinoJson.h>

#include <cmath>
#include <string.h>

namespace sentinel {
namespace realtime {
namespace {

bool validRssi(double value) {
  return std::isfinite(value) && value >= -100 && value < 0;
}

int rssiInteger(double value) {
  long rounded = std::lround(value);
  if (rounded < -100) rounded = -100;
  if (rounded > -1) rounded = -1;
  return static_cast<int>(rounded);
}

void setRssi(JsonObject object, const char* key, double value) {
  if (validRssi(value)) {
    object[key] = rssiInteger(value);
  } else {
    object[key] = nullptr;
  }
}

template <typename F>
bool envelope(const UtcClock& clock, const char* type, const char* source,
              const char* sessionId, uint64_t sequence, uint32_t nowMs,
              F fillPayload, char* output, size_t capacity) {
  if (!output || capacity == 0 || !type || !source || !sessionId) return false;
  char timestamp[25]{};
  if (!clock.format(nowMs, timestamp, sizeof(timestamp))) return false;

  JsonDocument document;
  JsonObject root = document.to<JsonObject>();
  root["protocolVersion"] = kProtocolVersion;
  root["type"] = type;
  root["timestamp"] = timestamp;
  root["sequence"] = sequence;
  root["source"] = source;
  root["sessionId"] = sessionId;
  JsonObject payload = root["payload"].to<JsonObject>();
  fillPayload(payload);
  if (document.overflowed()) return false;

  const size_t written = serializeJson(document, output, capacity);
  return written > 0 && written < capacity;
}

}  // namespace

template <typename F>
bool ProtocolAdapter::envelope(const char* type, const char* source,
                               const char* sessionId, uint64_t sequence,
                               uint32_t nowMs, F fillPayload, char* output,
                               size_t capacity) const {
  return realtime::envelope(clock_, type, source, sessionId, sequence, nowMs,
                            fillPayload, output, capacity);
}

bool ProtocolAdapter::buildHello(const char* source, const char* sessionId,
                                 uint64_t sequence, uint32_t nowMs,
                                 const char* deviceId, const char* role,
                                 const char* const* capabilities,
                                 size_t capabilityCount, char* output,
                                 size_t capacity) const {
  if (!deviceId || !*deviceId || !role || !*role || capabilityCount > 16) {
    return false;
  }
  return envelope(
      "connection.hello", source, sessionId, sequence, nowMs,
      [&](JsonObject payload) {
        payload["deviceId"] = deviceId;
        payload["role"] = role;
        payload["supportedVersion"] = kProtocolVersion;
        JsonArray list = payload["capabilities"].to<JsonArray>();
        for (size_t i = 0; i < capabilityCount; ++i) {
          if (capabilities && capabilities[i] && *capabilities[i]) {
            list.add(capabilities[i]);
          }
        }
      },
      output, capacity);
}

bool ProtocolAdapter::buildHeartbeat(const char* source, const char* sessionId,
                                     uint64_t sequence, uint32_t nowMs,
                                     const char* bootId, char* output,
                                     size_t capacity) const {
  if (!bootId || !*bootId) return false;
  return envelope(
      "connection.heartbeat", source, sessionId, sequence, nowMs,
      [&](JsonObject payload) {
        payload["bootId"] = bootId;
        payload["uptimeMs"] = nowMs;
      },
      output, capacity);
}

bool ProtocolAdapter::buildRfTelemetry(
    const char* source, const char* sessionId, uint64_t sequence,
    uint32_t nowMs, const char* bootId, uint8_t channel,
    const RfClientView* clients, size_t clientCount, char* output,
    size_t capacity) const {
  if (!bootId || !*bootId || !clients || clientCount > 8 || channel < 1 ||
      channel > 14) {
    return false;
  }

  return envelope(
      "rf.telemetry", source, sessionId, sequence, nowMs,
      [&](JsonObject payload) {
        payload["bootId"] = bootId;
        payload["firmwareVersion"] = kFirmwareVersion;
        payload["channel"] = channel;
        size_t emitted = 0;
        for (size_t i = 0; i < clientCount; ++i) {
          if (clients[i].clientId) ++emitted;
        }
        payload["clientCount"] = emitted;
        JsonArray list = payload["clients"].to<JsonArray>();
        for (size_t i = 0; i < clientCount; ++i) {
          const RfClientView& client = clients[i];
          if (!client.clientId) continue;
          JsonObject item = list.add<JsonObject>();
          item["clientId"] = client.clientId;
          item["connected"] = client.connected;
          setRssi(item, "rssiDbm", client.rssiDbm);
          setRssi(item, "filteredRssiDbm", client.filteredRssiDbm);

          const uint32_t seen = client.lastSeenMs > nowMs
                                    ? nowMs
                                    : client.lastSeenMs;
          char lastSeen[25]{};
          if (clock_.format(seen, lastSeen, sizeof(lastSeen))) {
            item["lastSeen"] = lastSeen;
          }

          if (client.hasDistance && std::isfinite(client.distanceM)) {
            double distance = client.distanceM;
            if (distance < 0) distance = 0;
            if (distance > 10000) distance = 10000;
            JsonObject estimate = item["estimatedDistance"].to<JsonObject>();
            estimate["valueM"] = distance;
            estimate["calibrated"] = client.distanceCalibrated;
            estimate["method"] = "log-distance";
          }
        }
        payload["simulated"] = false;
      },
      output, capacity);
}

bool ProtocolAdapter::buildSystemStatus(
    const char* source, const char* sessionId, uint64_t sequence,
    uint32_t nowMs, const char* bootId, const SystemStatusView& status,
    char* output, size_t capacity) const {
  if (!bootId || !*bootId || !status.operationalState ||
      status.transportCount == 0 || status.transportCount > 2 ||
      status.capabilityCount > 8) {
    return false;
  }
  return envelope(
      "system.status", source, sessionId, sequence, nowMs,
      [&](JsonObject payload) {
        payload["firmwareVersion"] = kFirmwareVersion;
        payload["bootId"] = bootId;
        payload["uptimeMs"] = nowMs;
        payload["operationalState"] = status.operationalState;
        JsonArray transports = payload["transports"].to<JsonArray>();
        for (size_t i = 0; i < status.transportCount; ++i) {
          if (status.transports[i]) transports.add(status.transports[i]);
        }
        if (status.capabilityCount) {
          JsonArray capabilities = payload["capabilities"].to<JsonArray>();
          for (size_t i = 0; i < status.capabilityCount; ++i) {
            if (status.capabilities[i]) capabilities.add(status.capabilities[i]);
          }
        }
        payload["freeHeapBytes"] = status.freeHeapBytes;
        payload["simulated"] = false;
      },
      output, capacity);
}

bool ProtocolAdapter::buildMotionStatus(
    const char* source, const char* sessionId, uint64_t sequence,
    uint32_t nowMs, const MotionStatusView& status, char* output,
    size_t capacity) const {
  if (!validProtocolDeviceId(status.deviceId) || !status.sourceType ||
      !*status.sourceType) {
    return false;
  }
  return envelope(
      "motion.status", source, sessionId, sequence, nowMs,
      [&](JsonObject payload) {
        payload["deviceId"] = status.deviceId;
        payload["sourceType"] = status.sourceType;
        payload["paired"] = status.paired;
        payload["acceptedSamples"] = status.acceptedSamples;
        payload["lastSampleAgeMs"] = status.lastSampleAgeMs;
        if (status.lastHttpCode >= 100 && status.lastHttpCode <= 599) {
          payload["lastHttpCode"] = status.lastHttpCode;
        }
        payload["signalLost"] = status.signalLost;
        payload["simulationRejected"] = status.simulationRejected;
        payload["simulated"] = false;
      },
      output, capacity);
}

bool ProtocolAdapter::buildEvent(const char* source, const char* sessionId,
                                 uint64_t sequence, uint32_t nowMs,
                                 const char* eventId, const char* name,
                                 uint32_t occurredMs,
                                 const EventDetailsView& details, char* output,
                                 size_t capacity) const {
  if (!eventId || !*eventId || !name || !*name) return false;
  char occurredAt[25]{};
  if (!clock_.format(occurredMs, occurredAt, sizeof(occurredAt))) return false;

  return envelope(
      "event", source, sessionId, sequence, nowMs,
      [&](JsonObject payload) {
        payload["eventId"] = eventId;
        payload["name"] = name;
        payload["occurredAt"] = occurredAt;
        payload["simulated"] = false;
        const bool hasMessage = details.message && *details.message;
        const bool hasPrevious = details.previousState && *details.previousState;
        const bool hasNext = details.newState && *details.newState;
        if (details.clientId || validRssi(details.rssiDbm) || hasMessage ||
            hasPrevious || hasNext) {
          JsonObject detail = payload["details"].to<JsonObject>();
          if (details.clientId) detail["clientId"] = details.clientId;
          if (validRssi(details.rssiDbm)) {
            setRssi(detail, "rssiDbm", details.rssiDbm);
          }
          if (hasMessage) detail["message"] = details.message;
          if (hasPrevious) detail["previousState"] = details.previousState;
          if (hasNext) detail["newState"] = details.newState;
        }
      },
      output, capacity);
}

bool ProtocolAdapter::buildError(const char* source, const char* sessionId,
                                 uint64_t sequence, uint32_t nowMs,
                                 const char* code, const char* message,
                                 bool retryable, const char* relatedSource,
                                 uint64_t relatedSequence, char* output,
                                 size_t capacity) const {
  if (!code || !*code || !message || !*message) return false;
  return envelope(
      "error", source, sessionId, sequence, nowMs,
      [&](JsonObject payload) {
        payload["code"] = code;
        payload["message"] = message;
        payload["retryable"] = retryable;
        if (relatedSource && *relatedSource) {
          payload["relatedSource"] = relatedSource;
          payload["relatedSequence"] = relatedSequence;
        }
      },
      output, capacity);
}

bool ProtocolAdapter::buildGoodbye(const char* source, const char* sessionId,
                                   uint64_t sequence, uint32_t nowMs,
                                   const char* reason, char* output,
                                   size_t capacity) const {
  if (!reason || !*reason) return false;
  return envelope(
      "connection.goodbye", source, sessionId, sequence, nowMs,
      [&](JsonObject payload) { payload["reason"] = reason; }, output,
      capacity);
}

}  // namespace realtime
}  // namespace sentinel
