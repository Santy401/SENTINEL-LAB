#include "rf/event_json.h"
#include <cassert>
#include <string>
#include <cstdio>
using namespace sentinel::rf;
int main(){
  EventEngine engine;auto e=engine.emit(EventType::MotionAlert,7,123,NAN,true);
  JsonDocument doc;eventJson(doc.to<JsonObject>(),e,"0123456789abcdef");
  std::string wire;serializeJson(doc,wire);JsonDocument parsed;assert(!deserializeJson(parsed,wire));
  assert(parsed["distance_estimated_m"].isNull());assert(parsed["rssi_dbm"].isNull());
  assert(parsed["simulated"].as<bool>());assert(parsed["event"]=="MOTION_ALERT");
  assert(parsed["timestamp_ms"]==123);assert(parsed["sequence"]==1);
  e.rssi=-82;e.distanceM=3.5;e.simulated=false;eventJson(doc.to<JsonObject>(),e,"0123456789abcdef");
  assert(doc["distance_estimated_m"]==3.5);assert(doc["rssi_dbm"]==-82);assert(!doc["simulated"].as<bool>());
  std::puts("test_event_json: ALL TESTS PASSED");
}
