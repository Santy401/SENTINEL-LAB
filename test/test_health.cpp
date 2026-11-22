#include "realtime/realtime_health.h"
#include "realtime/realtime_health_json.h"

#include "realtime/auth.h"
#include "realtime/realtime_core.h"

#include <ArduinoJson.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

extern uint32_t gTestNowMs;
extern uint32_t gTestFreeHeap;
extern size_t gTestClientCount;

using namespace sentinel::realtime;

namespace {

// Mirrors the serialization performed by /api/realtime/status in web_rf.cpp.
std::string serializeView(const RealtimeHealthView& health) {
  JsonDocument doc;
  writeRealtimeHealth(health, doc);
  std::string body;
  serializeJson(doc, body);
  return body;
}

}  // namespace

int main() {
  RealtimeHealthView view;
  assert(!snapshotRealtimeHealth(view));

  gTestNowMs = 0;
  gTestFreeHeap = 100000;
  AuthGuard auth("");
  RealtimeCore core;
  core.begin(81, "soak-boot-id", auth);

  assert(snapshotRealtimeHealth(view));
  assert(std::strcmp(view.fw, "0.3.1") == 0);
  assert(std::strcmp(view.realtimeVersion, "0.4.3") == 0);
  assert(std::strcmp(view.firmwareVersion, "0.4.3") == 0);
  assert(std::strcmp(view.httpApiVersion, "0.3.1") == 0);
  assert(std::strcmp(view.resetReason, "POWERON") == 0);
  assert(std::strcmp(view.clockSource, "none") == 0);
  assert(!view.hasClockSyncAge);
  assert(view.largestFreeBlockBytes == 50000);
  assert(view.minLoopStackFreeBytes == 4096);
  assert(view.handshakeTimeouts == 0);
  assert(std::strcmp(view.bootId, "rf-boot-id") == 0);
  assert(view.uptimeMs == 0);
  assert(view.clockSynced == false);
  assert(view.clientCount == 2);
  assert(view.freeHeapBytes == 100000);
  assert(view.minFreeHeapBytes == 100000);
  assert(view.heapSamples == 1);
  assert(view.droppedFrames == 0);
  assert(view.rejectedMessages == 0);
  assert(view.droppedTelemetry == 0);
  assert(view.skippedSimulatedEvents == 0);

  // Sampling happens before the clock gate, so it works while unsynced.
  gTestNowMs = 15000;
  gTestFreeHeap = 90000;
  core.loop(gTestNowMs);
  assert(snapshotRealtimeHealth(view));
  assert(view.uptimeMs == 15000);
  assert(view.freeHeapBytes == 90000);
  assert(view.minFreeHeapBytes == 90000);
  assert(view.heapSamples == 2);
  assert(view.droppedFrames == 3);
  assert(view.rejectedMessages == 4);

  gTestNowMs = 30000;
  gTestFreeHeap = 110000;
  core.loop(gTestNowMs);
  assert(snapshotRealtimeHealth(view));
  assert(view.freeHeapBytes == 110000);
  assert(view.minFreeHeapBytes == 90000);
  assert(view.heapSamples == 3);

  // An interval that has not elapsed must not create a sample, even at a
  // lower heap reading.
  gTestNowMs = 31000;
  gTestFreeHeap = 50000;
  core.loop(gTestNowMs);
  assert(snapshotRealtimeHealth(view));
  assert(view.freeHeapBytes == 110000);
  assert(view.minFreeHeapBytes == 90000);
  assert(view.heapSamples == 3);

  gTestNowMs = 45000;
  core.loop(gTestNowMs);
  assert(snapshotRealtimeHealth(view));
  assert(view.freeHeapBytes == 50000);
  assert(view.minFreeHeapBytes == 50000);
  assert(view.heapSamples == 4);

  const std::string body = serializeView(view);
  const std::string expected =
      R"({"fw":"0.3.1","httpApiVersion":"0.3.1","firmwareVersion":"0.4.3",)"
      R"("realtimeVersion":"0.4.3","bootId":"rf-boot-id",)"
      R"("resetReason":"POWERON","uptimeMs":45000,"clockSynced":false,)"
      R"("clockSource":"none","clockSyncAgeMs":null,"clientCount":2,)"
      R"("freeHeapBytes":50000,"minFreeHeapBytes":50000,)"
      R"("largestFreeBlockBytes":25000,"minLoopStackFreeBytes":4096,)"
      R"("heapSamples":4,"droppedFrames":3,"rejectedMessages":4,)"
      R"("droppedTelemetry":0,"skippedSimulatedEvents":0,)"
      R"("handshakeTimeouts":0,"idleTimeouts":0,"remoteCloses":0})";
  if (body != expected) {
    std::printf("health JSON mismatch:\n  actual:   %s\n  expected: %s\n",
                body.c_str(), expected.c_str());
    return 1;
  }

  std::puts("test_health: ALL TESTS PASSED");
  return 0;
}
