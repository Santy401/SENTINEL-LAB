#include "realtime/realtime_health.h"
#include "realtime/auth.h"
#include "realtime/realtime_core.h"
#include "rf/event_engine.h"
#include "rf/rf_monitor.h"
#include "WebServer.h"
#include "WiFi.h"

#include <ArduinoJson.h>

#include <cassert>
#include <cstdio>
#include <string>

extern uint32_t gTestNowMs;
extern uint32_t gTestFreeHeap;

WiFiClass WiFi;

namespace sentinel {
namespace rf {

void RfMonitor::update(uint32_t) {}
void beginNotifier(const char*) {}
void queueNotification(const Event&) {}
bool notifierEnabled() { return true; }

}  // namespace rf
}  // namespace sentinel

int main() {
  gTestNowMs = 1234;
  gTestFreeHeap = 76543;

  sentinel::realtime::AuthGuard auth("");
  sentinel::realtime::RealtimeCore core;
  core.begin(81, "soak-boot-id", auth);
  gTestNowMs = 15000;
  gTestFreeHeap = 70000;
  core.loop(gTestNowMs);

  sentinel::rf::beginRf();
  WebServer server;
  sentinel::rf::registerRoutes(server);

  WebServer::Route* health = nullptr;
  for (auto& route : server.routes) {
    if (route.path == "/api/realtime/status") health = &route;
  }

#ifdef SENTINEL_REALTIME
  assert(health != nullptr);
  assert(health->method == WebServer::HTTP_GET);

  health->handler();
  assert(server.lastCode == 200);
  assert(server.lastType == "application/json");

  JsonDocument doc;
  assert(!deserializeJson(doc, server.lastBody));
  assert(doc.size() == 13);
  assert(std::string(doc["fw"]) == "0.3.1");
  assert(std::string(doc["realtimeVersion"]) == "0.4.0");
  assert(std::string(doc["bootId"]) == "rf-boot-id");
  assert(doc["uptimeMs"] == 15000);
  assert(doc["clockSynced"] == false);
  assert(doc["clientCount"] == 2);
  assert(doc["freeHeapBytes"] == 70000);
  assert(doc["minFreeHeapBytes"] == 70000);
  assert(doc["heapSamples"] == 2);
  assert(doc["droppedFrames"] == 3);
  assert(doc["rejectedMessages"] == 4);
  assert(doc["droppedTelemetry"] == 0);
  assert(doc["skippedSimulatedEvents"] == 0);
  std::puts("test_web_route (SENTINEL_REALTIME): ALL TESTS PASSED");
#else
  assert(health == nullptr);
  std::puts("test_web_route (production guard): ROUTE ABSENT, ALL TESTS PASSED");
#endif
  return 0;
}
