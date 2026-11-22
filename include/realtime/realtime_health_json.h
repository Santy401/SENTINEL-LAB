#pragma once

#include <ArduinoJson.h>

#include "realtime/realtime_health.h"

namespace sentinel {
namespace realtime {

// Single serializer for GET /api/realtime/status, shared by the firmware route
// and the host tests so the documented shape cannot drift.
inline void writeRealtimeHealth(const RealtimeHealthView& health,
                                JsonDocument& doc) {
  doc["fw"] = health.fw;
  doc["httpApiVersion"] = health.httpApiVersion;
  doc["firmwareVersion"] = health.firmwareVersion;
  doc["realtimeVersion"] = health.realtimeVersion;
  doc["bootId"] = health.bootId;
  doc["resetReason"] = health.resetReason;
  doc["uptimeMs"] = health.uptimeMs;
  doc["clockSynced"] = health.clockSynced;
  doc["clockSource"] = health.clockSource;
  if (health.hasClockSyncAge) {
    doc["clockSyncAgeMs"] = health.clockSyncAgeMs;
  } else {
    doc["clockSyncAgeMs"] = nullptr;
  }
  doc["clientCount"] = health.clientCount;
  doc["freeHeapBytes"] = health.freeHeapBytes;
  doc["minFreeHeapBytes"] = health.minFreeHeapBytes;
  doc["largestFreeBlockBytes"] = health.largestFreeBlockBytes;
  doc["minLoopStackFreeBytes"] = health.minLoopStackFreeBytes;
  doc["heapSamples"] = health.heapSamples;
  doc["droppedFrames"] = health.droppedFrames;
  doc["rejectedMessages"] = health.rejectedMessages;
  doc["droppedTelemetry"] = health.droppedTelemetry;
  doc["skippedSimulatedEvents"] = health.skippedSimulatedEvents;
  doc["handshakeTimeouts"] = health.handshakeTimeouts;
  doc["idleTimeouts"] = health.idleTimeouts;
  doc["remoteCloses"] = health.remoteCloses;
}

}  // namespace realtime
}  // namespace sentinel
