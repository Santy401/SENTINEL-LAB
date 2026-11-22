// Host-only stubs for firmware globals and the WebSocket transport. They are
// deliberately simple: the test targets the health snapshot and the additive
// HTTP route, not the network stack.
#include "realtime/firmware_bridge.h"
#include "realtime/realtime_core.h"

#include <stdint.h>
#include <string.h>

uint32_t gTestNowMs = 0;
uint32_t gTestFreeHeap = 0;
size_t gTestClientCount = 2;
uint32_t gTestDroppedFrames = 3;
uint32_t gTestRejectedMessages = 4;

namespace sentinel {
namespace realtime {

void RealtimeServer::begin(uint16_t, const char*, UtcClock&, ProtocolAdapter&,
                           const AuthGuard&) {}
void RealtimeServer::loop(uint32_t) {
  dropped_ = gTestDroppedFrames;
  rejected_ = gTestRejectedMessages;
}
void RealtimeServer::broadcast(const char*, size_t) {}
size_t RealtimeServer::clientCount() const { return gTestClientCount; }

size_t snapshotRfClients(RfClientView*, size_t, uint8_t&, bool& available) {
  available = false;
  return 0;
}

bool snapshotMotionStatus(uint32_t, MotionStatusView&) { return false; }

const char* firmwareBootId() { return "rf-boot-id"; }

const rf::EventEngine& firmwareEvents() {
  static rf::EventEngine events;
  return events;
}

uint32_t firmwareFreeHeapBytes() { return gTestFreeHeap; }
uint32_t firmwareLargestFreeBlockBytes() { return gTestFreeHeap / 2; }
uint32_t firmwareLoopStackFreeBytes() { return 4096; }
const char* firmwareResetReason() { return "POWERON"; }

}  // namespace realtime

namespace v02 {

static MotionEngine testEngine;
const MotionEngine& realtimeMotionEngine() { return testEngine; }

}  // namespace v02
}  // namespace sentinel
