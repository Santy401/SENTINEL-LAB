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
  largestFreeBlockBytes_ = firmwareLargestFreeBlockBytes();
  minLoopStackFreeBytes_ = firmwareLoopStackFreeBytes();
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
  server_.setMotionSink(&RealtimeCore::onMotion, this);
  rgb_.begin(millis());
  activeCore = this;
}

void RealtimeCore::loop(uint32_t nowMs) {
  // Health sampling runs before the clock gate so soak data is available even
  // while no NTP or authenticated gateway has supplied a date yet.
  sampleHeap(nowMs);
  tryNtpSync(nowMs);
  server_.loop(nowMs);
  updateRgb(nowMs);

  if (!clock_.synced()) return;
  if (!scheduleArmed_) armSchedule(nowMs);

  if (uint32_t(nowMs - lastRf_) >= kRfIntervalMs) {
    lastRf_ = nowMs;
    publishRf(nowMs);
  }
  if (uint32_t(nowMs - lastHeartbeat_) >= kHeartbeatIntervalMs) {
    lastHeartbeat_ = nowMs;
    if (adapter_.buildHeartbeat(systemSession_.source(),
                                systemSession_.sessionId(),
                                systemSession_.nextSequence(), nowMs, bootId_,
                                message_, sizeof(message_))) {
      server_.broadcast(message_, strlen(message_));
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
  relayMotion(nowMs);
  if (uint32_t(nowMs - lastEventPoll_) >= kEventPollIntervalMs) {
    lastEventPoll_ = nowMs;
    publishEvents(nowMs);
  }
}

// Periodic producers start in distinct phases once time is available.
// Without this, rf.telemetry, heartbeat and system.status all fired in the
// same loop as the sync hello and overflowed the per-peer queue (one
// droppedFrames observed on hardware, 2026-11-21).
void RealtimeCore::armSchedule(uint32_t nowMs) {
  scheduleArmed_ = true;
  lastRf_ = nowMs - kRfIntervalMs + 100;
  lastHeartbeat_ = nowMs - kHeartbeatIntervalMs + 350;
  lastSystem_ = nowMs - 15000 + 600;
  lastMotion_ = nowMs - 2000 + 850;
}

void RealtimeCore::sampleHeap(uint32_t nowMs) {
  if (uint32_t(nowMs - lastHeapSample_) < kHeapSampleIntervalMs) return;
  lastHeapSample_ = nowMs;

  const uint32_t heap = firmwareFreeHeapBytes();
  freeHeapBytes_ = heap;
  if (heap < minFreeHeapBytes_) minFreeHeapBytes_ = heap;
  largestFreeBlockBytes_ = firmwareLargestFreeBlockBytes();
  // The high-water mark only decreases; keep the minimum for clarity.
  const uint32_t stackFree = firmwareLoopStackFreeBytes();
  if (stackFree < minLoopStackFreeBytes_) minLoopStackFreeBytes_ = stackFree;
  ++heapSamples_;
}

void RealtimeCore::tryNtpSync(uint32_t nowMs) {
#if defined(SENTINEL_RF_NTP_SERVER)
  if (clock_.synced() || !strlen(SENTINEL_RF_NTP_SERVER)) return;
  if (uint32_t(nowMs - lastNtpCheck_) < kNtpRetryMs) return;
  lastNtpCheck_ = nowMs;
  const time_t now = time(nullptr);
  if (now >= 1700000000) {
    clock_.sync(static_cast<int64_t>(now) * 1000, nowMs, ClockSource::Ntp);
  }
#else
  (void)nowMs;
#endif
}

void RealtimeCore::publishRf(uint32_t nowMs) {
  uint8_t channel = 0;
  bool available = false;
  const size_t count = snapshotRfClients(rfClients_, 8, channel, available);
  if (!available) return;

  char* message = message_;
  if (!adapter_.buildRfTelemetry(systemSession_.source(),
                                 systemSession_.sessionId(),
                                 systemSession_.nextSequence(), nowMs, bootId_,
                                 channel, rfClients_, count, message,
                                 sizeof(message_))) {
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

  char* message = message_;
  if (!adapter_.buildSystemStatus(systemSession_.source(),
                                  systemSession_.sessionId(),
                                  systemSession_.nextSequence(), nowMs,
                                  bootId_, status, message, sizeof(message_))) {
    ++droppedTelemetry_;
    return;
  }
  server_.broadcast(message, strlen(message));
}

void RealtimeCore::publishMotion(uint32_t nowMs) {
  MotionStatusView status;
  if (!snapshotMotionStatus(nowMs, status)) return;

  char* message = message_;
  if (!adapter_.buildMotionStatus(systemSession_.source(),
                                  systemSession_.sessionId(),
                                  systemSession_.nextSequence(), nowMs, status,
                                  message, sizeof(message_))) {
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

    char* message = message_;
    if (!adapter_.buildEvent(systemSession_.source(),
                             systemSession_.sessionId(),
                             systemSession_.nextSequence(), nowMs, eventId,
                             rf::eventName(event.type), event.timeMs, details,
                             message, sizeof(message_))) {
      ++droppedTelemetry_;
      continue;
    }
    server_.broadcast(message, strlen(message));
    rgb_.pulseEvent(nowMs);
  }
}

void RealtimeCore::onMotion(void* context, const InboundMessage& m,
                            uint32_t nowMs) {
  auto* core = static_cast<RealtimeCore*>(context);
  core->phone_.ingest(m.motionDeviceId, m.linear, nowMs);
  snprintf(core->relayDeviceId_, sizeof(core->relayDeviceId_), "%s", m.motionDeviceId);
  snprintf(core->relaySource_, sizeof(core->relaySource_), "%s", m.motionSourceType);
  snprintf(core->relayState_, sizeof(core->relayState_), "%s", m.motionState);
  snprintf(core->relaySensorTs_, sizeof(core->relaySensorTs_), "%s", m.motionSensorTimestamp);
  MotionSampleView& r = core->relay_;
  r.deviceId = core->relayDeviceId_;
  r.sourceType = core->relaySource_;
  r.state = core->relayState_;
  r.sensorTimestamp = core->relaySensorTs_;
  r.sampleIndex = m.motionSampleIndex;
  r.acceleration = m.acceleration;
  r.gravity = m.gravity;
  r.linear = m.linear;
  core->relayPending_ = true;
}

void RealtimeCore::relayMotion(uint32_t nowMs) {
  if (!relayPending_ || uint32_t(nowMs - lastRelay_) < kMotionRelayMs) return;
  relayPending_ = false;
  lastRelay_ = nowMs;
  if (!adapter_.buildMotionSample(systemSession_.source(),
                                  systemSession_.sessionId(),
                                  systemSession_.nextSequence(), nowMs, relay_,
                                  message_, sizeof(message_))) {
    ++droppedTelemetry_;
    return;
  }
  server_.broadcast(message_, strlen(message_));
}

void RealtimeCore::updateRgb(uint32_t nowMs) {
  if (uint32_t(nowMs - lastRgbFrame_) < kRgbFrameMs) return;
  lastRgbFrame_ = nowMs;
  RgbInputs in;
  in.nowMs = nowMs;
  in.clients = server_.clientCount();
  in.synced = clock_.synced();
  in.motionActive = phone_.active(nowMs);
  in.intensity = phone_.intensity(nowMs);
  if (rgb_.update(in)) {
    const RgbColor c = rgb_.color();
    firmwareWriteRgb(c.r, c.g, c.b);
  }
}

bool snapshotRealtimeHealth(RealtimeHealthView& output) {
  RealtimeCore* core = activeCore;
  if (!core) return false;

  output = RealtimeHealthView{};
  output.fw = kHttpApiVersion;
  output.httpApiVersion = kHttpApiVersion;
  output.firmwareVersion = kFirmwareVersion;
  output.realtimeVersion = kFirmwareVersion;
  // Same source as /api/rf, so soak logs can correlate the two endpoints.
  output.bootId = firmwareBootId();
  output.resetReason = firmwareResetReason();
  const uint32_t now = millis();
  output.uptimeMs = now;
  const UtcClock& clock = core->clock();
  output.clockSynced = clock.synced();
  output.clockSource = clockSourceName(clock.source());
  if (clock.synced()) {
    output.hasClockSyncAge = true;
    output.clockSyncAgeMs = now - clock.syncedAtMs();
  }
  output.clientCount = static_cast<uint32_t>(core->clientCount());
  output.freeHeapBytes = core->freeHeapBytes();
  output.minFreeHeapBytes = core->minFreeHeapBytes();
  output.largestFreeBlockBytes = core->largestFreeBlockBytes();
  output.minLoopStackFreeBytes = core->minLoopStackFreeBytes();
  output.heapSamples = core->heapSamples();
  output.droppedFrames = core->droppedFrames();
  output.rejectedMessages = core->rejectedMessages();
  output.droppedTelemetry = core->droppedTelemetry();
  output.skippedSimulatedEvents = core->skippedSimulatedEvents();
  output.handshakeTimeouts = core->server().handshakeTimeouts();
  output.idleTimeouts = core->server().idleTimeouts();
  output.remoteCloses = core->server().remoteCloses();
  output.rgbEnabled = firmwareRgbEnabled();
  output.rgbPin = firmwareRgbPin();
  output.rgbMode = rgbModeName(core->rgb().mode());
  const RgbColor color = core->rgb().color();
  output.rgbR = color.r;
  output.rgbG = color.g;
  output.rgbB = color.b;
  const PhoneMotion& phone = core->phoneMotion();
  output.phoneActive = phone.active(now);
  output.phoneHasSample = phone.hasSample();
  output.phoneDeviceId = phone.deviceId();
  output.phoneLinearMs2 = phone.magnitude();
  output.phoneIntensity = phone.intensity(now);
  output.phonePeakMs2 = phone.peak();
  output.phoneSamples = phone.samples();
  output.phoneSampleAgeMs = phone.ageMs(now);
  return true;
}

}  // namespace realtime
}  // namespace sentinel
