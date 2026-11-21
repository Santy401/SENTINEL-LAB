#include "realtime/realtime_core.h"

#include "realtime/firmware_bridge.h"
#include "realtime/realtime_health.h"
#include "realtime/status_policy.h"

#include <Arduino.h>
#include <time.h>

#if __has_include("rf/connected.h")
#include "rf/connected.h"
#else
#include "rf/connected.example.h"
#endif

#include <stdio.h>
#include <string.h>

namespace sentinel {
namespace realtime {
namespace {

// Set once the P2 core is initialized; the health snapshot is invalid before
// that point. It is never cleared because begin() happens once per boot.
RealtimeCore* activeCore = nullptr;

bool relevantEvent(rf::EventType type) {
  switch (type) {
    case rf::EventType::DeviceConnected:
    case rf::EventType::DeviceDisconnected:
    case rf::EventType::SignalWeak:
    case rf::EventType::SignalRecovered:
      return true;
    default:
      return false;
  }
}

}  // namespace

void RealtimeCore::begin(uint16_t port, const char* bootId,
                         const AuthGuard& auth) {
  const char* id = bootId && *bootId ? bootId : "boot";
  snprintf(bootId_, sizeof(bootId_), "%s", id);
  systemSession_.begin(0, bootId_, true);

  // The first heap reading is a sample too, so health is observable before the
  // first 15 s interval elapses (and before the clock is synced).
  const uint32_t heap = firmwareFreeHeapBytes();
  freeHeapBytes_ = heap;
  minFreeHeapBytes_ = heap;
  heapSamples_ = 1;

#if defined(SENTINEL_RF_NTP_SERVER)
  if (strlen(SENTINEL_RF_NTP_SERVER) > 0) {
    configTime(0, 0, SENTINEL_RF_NTP_SERVER);
    lastNtpCheck_ = millis() - kNtpRetryMs;
    tryNtpSync(millis());
  }
#endif

  const auto& events = firmwareEvents().events();
  if (events.size()) {
    events_.begin(events.at(events.size() - 1).sequence);
  }

  server_.begin(port, bootId_, clock_, adapter_, auth);
  activeCore = this;
}

void RealtimeCore::loop(uint32_t nowMs) {
  // Health sampling runs before the clock gate so soak data is available even
  // while no NTP or authenticated gateway has supplied a date yet.
  sampleHeap(nowMs);
  tryNtpSync(nowMs);
  server_.loop(nowMs);

  if (!clock_.synced()) return;

  if (uint32_t(nowMs - lastRf_) >= kRfIntervalMs) {
    lastRf_ = nowMs;
    publishRf(nowMs);
  }
  if (uint32_t(nowMs - lastHeartbeat_) >= kHeartbeatIntervalMs) {
    lastHeartbeat_ = nowMs;
    char heartbeat[kProtocolMessageBytes]{};
    if (adapter_.buildHeartbeat(systemSession_.source(),
                                systemSession_.sessionId(),
                                systemSession_.nextSequence(), nowMs, bootId_,
                                heartbeat, sizeof(heartbeat))) {
      server_.broadcast(heartbeat, strlen(heartbeat));
    } else {
      ++droppedTelemetry_;
    }
  }
  if (uint32_t(nowMs - lastSystem_) >= 15000) {
    lastSystem_ = nowMs;
    publishSystem(nowMs);
  }
  if (uint32_t(nowMs - lastMotion_) >= 2000) {
    lastMotion_ = nowMs;
    publishMotion(nowMs);
  }
  if (uint32_t(nowMs - lastEventPoll_) >= kEventPollIntervalMs) {
    lastEventPoll_ = nowMs;
    publishEvents(nowMs);
  }
}

void RealtimeCore::sampleHeap(uint32_t nowMs) {
  if (uint32_t(nowMs - lastHeapSample_) < kHeapSampleIntervalMs) return;
  lastHeapSample_ = nowMs;

  const uint32_t heap = firmwareFreeHeapBytes();
  freeHeapBytes_ = heap;
  if (heap < minFreeHeapBytes_) minFreeHeapBytes_ = heap;
  ++heapSamples_;
}

void RealtimeCore::tryNtpSync(uint32_t nowMs) {
#if defined(SENTINEL_RF_NTP_SERVER)
  if (clock_.synced() || !strlen(SENTINEL_RF_NTP_SERVER)) return;
  if (uint32_t(nowMs - lastNtpCheck_) < kNtpRetryMs) return;
  lastNtpCheck_ = nowMs;
  const time_t now = time(nullptr);
  if (now >= 1700000000) {
    clock_.sync(static_cast<int64_t>(now) * 1000, nowMs);
  }
#else
  (void)nowMs;
#endif
}

void RealtimeCore::publishRf(uint32_t nowMs) {
  RfClientView clients[8]{};
  uint8_t channel = 0;
  bool available = false;
  const size_t count = snapshotRfClients(clients, 8, channel, available);
  if (!available) return;

  char message[kProtocolMessageBytes]{};
  if (!adapter_.buildRfTelemetry(systemSession_.source(),
                                 systemSession_.sessionId(),
                                 systemSession_.nextSequence(), nowMs, bootId_,
                                 channel, clients, count, message,
                                 sizeof(message))) {
    ++droppedTelemetry_;
    return;
  }
  server_.broadcast(message, strlen(message));
}

void RealtimeCore::publishSystem(uint32_t nowMs) {
  const v02::MotionSnapshot& motion = v02::realtimeMotionEngine().snapshot();
  SystemStatusView status;
  OperationalState state = OperationalState::Monitoring;
  if (motion.state == v02::MotionState::Alert) {
    state = OperationalState::Alert;
  } else if (motion.state == v02::MotionState::SensorOffline) {
    state = OperationalState::SensorOffline;
  }
  const CapabilityPolicy capabilities = systemCapabilityPolicy();
  status.operationalState = operationalStateName(state);
  status.capabilities = capabilities.values;
  status.capabilityCount = capabilities.count;
  // Reuse the latest health sample; both consumers report the same reading.
  status.freeHeapBytes = freeHeapBytes_;

  char message[kProtocolMessageBytes]{};
  if (!adapter_.buildSystemStatus(systemSession_.source(),
                                  systemSession_.sessionId(),
                                  systemSession_.nextSequence(), nowMs,
                                  bootId_, status, message, sizeof(message))) {
    ++droppedTelemetry_;
    return;
  }
  server_.broadcast(message, strlen(message));
}

void RealtimeCore::publishMotion(uint32_t nowMs) {
  MotionStatusView status;
  if (!snapshotMotionStatus(nowMs, status)) return;

  char message[kProtocolMessageBytes]{};
  if (!adapter_.buildMotionStatus(systemSession_.source(),
                                  systemSession_.sessionId(),
                                  systemSession_.nextSequence(), nowMs, status,
                                  message, sizeof(message))) {
    ++droppedTelemetry_;
    return;
  }
  server_.broadcast(message, strlen(message));
}

void RealtimeCore::publishEvents(uint32_t nowMs) {
  if (!eventBridgeStarted_) {
    eventBridgeStarted_ = true;
    return;
  }

  const auto& ring = firmwareEvents().events();
  for (size_t i = 0; i < ring.size(); ++i) {
    const rf::Event& event = ring.at(i);
    if (!relevantEvent(event.type)) continue;
    if (!events_.consume(event.sequence, event.simulated)) {
      if (event.simulated) ++skippedSimulatedEvents_;
      continue;
    }

    char eventId[48]{};
    snprintf(eventId, sizeof(eventId), "evt-%s-%lu", bootId_,
             static_cast<unsigned long>(event.sequence));
    EventDetailsView details;
    details.clientId = event.clientId;
    details.rssiDbm = event.rssi;

    char message[kProtocolMessageBytes]{};
    if (!adapter_.buildEvent(systemSession_.source(),
                             systemSession_.sessionId(),
                             systemSession_.nextSequence(), nowMs, eventId,
                             rf::eventName(event.type), event.timeMs, details,
                             message, sizeof(message))) {
      ++droppedTelemetry_;
      continue;
    }
    server_.broadcast(message, strlen(message));
  }
}

bool snapshotRealtimeHealth(RealtimeHealthView& output) {
  RealtimeCore* core = activeCore;
  if (!core) return false;

  output = RealtimeHealthView{};
  output.fw = "0.3.1";
  output.realtimeVersion = "0.4.0";
  // Same source as /api/rf, so soak logs can correlate the two endpoints.
  output.bootId = firmwareBootId();
  output.uptimeMs = millis();
  output.clockSynced = core->clockSynced();
  output.clientCount = static_cast<uint32_t>(core->clientCount());
  output.freeHeapBytes = core->freeHeapBytes();
  output.minFreeHeapBytes = core->minFreeHeapBytes();
  output.heapSamples = core->heapSamples();
  output.droppedFrames = core->droppedFrames();
  output.rejectedMessages = core->rejectedMessages();
  output.droppedTelemetry = core->droppedTelemetry();
  output.skippedSimulatedEvents = core->skippedSimulatedEvents();
  return true;
}

}  // namespace realtime
}  // namespace sentinel
