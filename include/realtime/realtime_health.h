#pragma once

#include <stdint.h>

namespace sentinel {
namespace realtime {

// Additive read-only view for local hardware soak validation. It mirrors
// counters that the firmware already owns; it is not part of Protocol v1 and
// does not alter any HTTP v0.3.1 response.
//
// Version semantics: "fw" is the HTTP API compatibility level (0.3.1, kept
// for existing clients, ADR-0003); "firmwareVersion" and "realtimeVersion"
// identify the realtime image actually running.
struct RealtimeHealthView {
  const char* fw = "0.3.1";
  const char* httpApiVersion = "0.3.1";
  const char* firmwareVersion = "";
  const char* realtimeVersion = "";
  const char* bootId = "";
  const char* resetReason = "UNKNOWN";
  uint32_t uptimeMs = 0;
  bool clockSynced = false;
  const char* clockSource = "none";
  bool hasClockSyncAge = false;
  uint32_t clockSyncAgeMs = 0;
  uint32_t clientCount = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t minFreeHeapBytes = 0;
  uint32_t largestFreeBlockBytes = 0;
  uint32_t minLoopStackFreeBytes = 0;
  uint32_t heapSamples = 0;
  uint32_t droppedFrames = 0;
  uint32_t rejectedMessages = 0;
  uint32_t droppedTelemetry = 0;
  uint32_t skippedSimulatedEvents = 0;
  uint32_t handshakeTimeouts = 0;
  uint32_t idleTimeouts = 0;
  uint32_t remoteCloses = 0;
  bool rgbEnabled = false;
  int rgbPin = -1;
  const char* rgbMode = "offline";
  uint8_t rgbR = 0;
  uint8_t rgbG = 0;
  uint8_t rgbB = 0;
  bool phoneActive = false;
  bool phoneHasSample = false;
  const char* phoneDeviceId = "";
  double phoneLinearMs2 = 0;
  float phoneIntensity = 0;
  double phonePeakMs2 = 0;
  uint32_t phoneSamples = 0;
  uint32_t phoneSampleAgeMs = 0;
};

// Fills the view from the active core. Returns false when the core is not
// active, so callers never report health from an uninitialized core.
bool snapshotRealtimeHealth(RealtimeHealthView& output);

}  // namespace realtime
}  // namespace sentinel
