#pragma once

#include <stdint.h>

namespace sentinel {
namespace realtime {

// Additive read-only view for local P2 hardware soak validation. It mirrors
// counters that the firmware already owns; it is not part of Protocol v1 and
// does not alter any HTTP v0.3.1 response.
struct RealtimeHealthView {
  const char* fw = "0.3.1";
  const char* realtimeVersion = "0.4.0";
  const char* bootId = "";
  uint32_t uptimeMs = 0;
  bool clockSynced = false;
  uint32_t clientCount = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t minFreeHeapBytes = 0;
  uint32_t heapSamples = 0;
  uint32_t droppedFrames = 0;
  uint32_t rejectedMessages = 0;
  uint32_t droppedTelemetry = 0;
  uint32_t skippedSimulatedEvents = 0;
};

// Fills the view from the active P2 core. Returns false when P2 is not active,
// so callers never report health from an uninitialized core.
bool snapshotRealtimeHealth(RealtimeHealthView& output);

}  // namespace realtime
}  // namespace sentinel
