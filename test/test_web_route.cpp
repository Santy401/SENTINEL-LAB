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
  // The embedded Arc dashboard owns "/", gzip-encoded; v0.3.1 page moved.
  WebServer::Route* root = server.find("/");
  assert(root != nullptr);
  size_t rootRoutes = 0;
  for (auto& route : server.routes) rootRoutes += route.path == "/";
  assert(rootRoutes == 1);
  root->handler();
  assert(server.lastCode == 200);
  assert(server.lastType == "text/html; charset=utf-8");
  assert(server.lastBody == std::string("\x1f\x8bi", 3));
  bool gzip = false, noCache = false;
  for (auto& header : server.headers) {
    gzip |= header == "Content-Encoding: gzip";
    noCache |= header == "Cache-Control: no-cache";
  }
  assert(gzip && noCache);
  server.headers.clear();
  server.find("/assets/app.js")->handler();
  assert(server.lastType == "text/javascript; charset=utf-8");
  assert(server.find("/legacy") != nullptr);

  assert(health != nullptr);
  assert(health->method == WebServer::HTTP_GET);

  health->handler();
  assert(server.lastCode == 200);
  assert(server.lastType == "application/json");

  JsonDocument doc;
  assert(!deserializeJson(doc, server.lastBody));
  assert(doc.size() == 25);
  assert(!std::string(doc["rgb"]["mode"].as<const char*>()).empty());
  assert(doc["rgb"]["pin"] == 48);
  assert(doc["phoneMotion"]["active"] == false);
  assert(doc["phoneMotion"]["deviceId"].isNull());
  assert(std::string(doc["fw"]) == "0.3.1");
  assert(std::string(doc["realtimeVersion"]) == "0.5.0");
  assert(std::string(doc["firmwareVersion"]) == "0.5.0");
  assert(std::string(doc["httpApiVersion"]) == "0.3.1");
  assert(std::string(doc["resetReason"]) == "POWERON");
  assert(std::string(doc["clockSource"]) == "none");
  assert(doc["clockSyncAgeMs"].isNull());
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
  // The v0.3.1 image keeps its original dashboard at "/" and has no /legacy.
  assert(server.find("/") != nullptr);
  assert(server.find("/legacy") == nullptr);
  std::puts("test_web_route (production guard): ROUTE ABSENT, ALL TESTS PASSED");
#endif
  return 0;
}
