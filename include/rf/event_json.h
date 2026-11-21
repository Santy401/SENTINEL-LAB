#pragma once
#include "rf/event_engine.h"
#include <ArduinoJson.h>
namespace sentinel { namespace rf {
inline void eventJson(JsonObject o,const Event& e,const char* boot) {
  o["boot_id"]=boot;o["sequence"]=e.sequence;o["timestamp_ms"]=e.timeMs;
  o["event"]=eventName(e.type);o["client_id"]=e.clientId;
  o["simulated"]=e.simulated;o["notify"]=e.notify;
  if(validRssi(e.rssi))o["rssi_dbm"]=e.rssi;else o["rssi_dbm"]=nullptr;
  if(std::isfinite(e.distanceM)&&e.distanceM>0)o["distance_estimated_m"]=e.distanceM;
  else o["distance_estimated_m"]=nullptr;
}
} }
