#include "rf/rf_monitor.h"
#include "rf/laboratory.h"
#include "rf/event_json.h"
#include "rf/notifier.h"
#include "rf/dashboard_html.h"
#include "rf/capture_html.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#ifdef SENTINEL_REALTIME
#include "realtime/realtime_health.h"
#endif
namespace sentinel { namespace rf {
namespace {
RfMonitor monitor;
Laboratory lab;
EventEngine eventEngine;
uint32_t lastEventPoll=0,lastQueued=0,lastHeartbeat=0;
WebServer* http;
char bootId[17]{};
void send(JsonDocument& doc) {
  String body; serializeJson(doc,body);
  http->sendHeader("Cache-Control","no-store"); http->send(200,"application/json",body);
}
void error(int code, const char* text) {
  JsonDocument doc; doc["ok"]=false; doc["error"]=text;
  String body; serializeJson(doc,body); http->send(code,"application/json",body);
}
bool body(JsonDocument& doc) {
  if(http->arg("plain").length()>1024) { error(413,"body too large"); return false; }
  if(deserializeJson(doc,http->arg("plain")) || !doc.is<JsonObject>()) {
    error(400,"invalid json object"); return false;
  }
  return true;
}
bool number(JsonVariantConst v, double low, double high) {
  return v.is<double>() && std::isfinite(v.as<double>()) && v.as<double>()>=low && v.as<double>()<=high;
}
const Client* client(uint32_t id) {
  if(!id) return nullptr; // id 0 means "empty slot", never a real participant.
  for(size_t i=0;i<8;++i) if(monitor.clients()[i].id==id) return monitor.clients()+i;
  return nullptr;
}
void numeric(JsonObject obj,const char* key,double value) {
  if(std::isfinite(value)) obj[key]=value; else obj[key]=nullptr;
}
void measurementJson(JsonObject item,const Measurement& m) {
  item["id"]=m.id; item["experimentId"]=m.experimentId; item["clientId"]=m.clientId;
  item["timeMs"]=m.timeMs; item["actualM"]=m.actualM; item["connected"]=m.connected;
  item["calibration"]=m.calibration; item["notes"]=m.notes;
  numeric(item,"rssiDbm",m.rssi); numeric(item,"filteredDbm",m.filtered);
  numeric(item,"estimatedM",m.estimatedM);
  numeric(item,"errorM",DistanceEstimator::absoluteError(m.estimatedM,m.actualM));
  numeric(item,"x",m.x); numeric(item,"y",m.y);
}
String csvText(const char* text) {
  String s(text); s.replace("\"","\"\"");
  // Prevent spreadsheet formulas, including after leading whitespace.
  String probe=s; probe.trim();
  if(probe.length() && (probe[0]=='=' || probe[0]=='+' || probe[0]=='-' || probe[0]=='@')) s="'"+s;
  return "\""+s+"\"";
}
String csvNumber(double v) { return std::isfinite(v)?String(v,5):String(""); }
}
void beginRf() { snprintf(bootId,sizeof(bootId),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random()); beginNotifier(bootId); }
void updateRf(uint32_t now) {
  monitor.update(now);
  if(uint32_t(now-lastEventPoll)>=1000) {
    lastEventPoll=now;
    for(size_t i=0;i<8;++i) {
      const auto& c=monitor.clients()[i];
      eventEngine.observe(i,c.id,c.connected,monitor.available()&&c.rssiValid?c.rssi:NAN,now);
    }
  }
  if(uint32_t(now-lastHeartbeat)>=15000){lastHeartbeat=now;eventEngine.emit(EventType::Heartbeat,0,now);}
  const auto& events=eventEngine.events();
  for(size_t i=0;i<events.size();++i) {
    const auto& e=events.at(i);if(e.sequence>lastQueued){queueNotification(e);lastQueued=e.sequence;}
  }
}
void observeMotion(int state,bool simulated,uint32_t now) {
  static int previous=3;
  static bool lastSimulated=false;
  static uint32_t lastNotice=0;
  static bool notified=false;
  if(state==previous){if(state!=3)lastSimulated=simulated;return;}
  const EventType type=state==3?EventType::SensorOffline:previous==3?EventType::SensorRecovered:
    state==2?EventType::MotionAlert:EventType::MovementDetected;
  if(state!=0 || previous==3) {
    const bool eligible=!notified || uint32_t(now-lastNotice)>=30000;
    eventEngine.emit(type,0,now,NAN,state==3?lastSimulated:simulated,eligible);
    if(eligible){lastNotice=now;notified=true;}
  }
  previous=state;if(state!=3)lastSimulated=simulated;
}
void registerRoutes(WebServer& server) {
  http=&server;
  server.on("/api/events",HTTP_GET,[]{
    JsonDocument doc;doc["bootId"]=bootId;doc["notificationsEnabled"]=notifierEnabled();
    auto array=doc["events"].to<JsonArray>();
    for(size_t i=0;i<eventEngine.events().size();++i)eventJson(array.add<JsonObject>(),eventEngine.events().at(i),bootId);
    send(doc);
  });
  server.on("/capture",HTTP_GET,[]{http->send_P(200,"text/html; charset=utf-8",kCaptureHtml);});
  server.on("/",HTTP_GET,[]{http->send_P(200,"text/html; charset=utf-8",kRfDashboard);});
  server.on("/api/rf",HTTP_GET,[]{
    JsonDocument doc; doc["bootId"]=bootId; doc["fw"]="0.3.1"; doc["uptimeMs"]=millis();
    doc["mode"]=WiFi.getMode()==WIFI_AP_STA?"AP+STA":"AP";
    doc["staConnected"]=WiFi.status()==WL_CONNECTED;
    doc["notificationsEnabled"]=notifierEnabled();
    doc["signalWeak"]=monitor.available() && eventEngine.anyWeak();
    doc["available"]=monitor.available(); doc["ssid"]=WiFi.softAPSSID();
    doc["ip"]=WiFi.softAPIP().toString(); doc["clients"]=monitor.count();
    doc["channel"]=monitor.channel();
    if(monitor.channel()) {
      const int f=monitor.channel()==14?2484:2407+5*monitor.channel();
      doc["frequencyMHz"]=f; doc["wavelengthM"]=299792458.0/(f*1000000.0);
    } else {doc["frequencyMHz"]=nullptr;doc["wavelengthM"]=nullptr;}
    auto devices=doc["devices"].to<JsonArray>();
    for(size_t i=0;i<8;++i) {
      const auto& c=monitor.clients()[i]; if(!c.id) continue;
      auto item=devices.add<JsonObject>(); item["id"]=c.id; item["connected"]=c.connected;
      item["lastSeenMs"]=c.lastSeenMs; numeric(item,"rssiDbm",c.rssiValid?c.rssi:NAN);
      numeric(item,"filteredDbm",c.rssiValid?c.filter.value():NAN);
      auto history=item["history"].to<JsonArray>();
      for(size_t j=0;j<c.history.size();++j) {
        auto h=history.add<JsonObject>(); h["t"]=c.history.at(j).timeMs; h["rssi"]=c.history.at(j).rssi;
      }
    }
    auto events=doc["events"].to<JsonArray>();
    for(size_t i=0;i<monitor.events().size();++i) {
      const auto& e=monitor.events().at(i); auto item=events.add<JsonObject>();
      item["t"]=e.timeMs; item["clientId"]=e.clientId;
      item["type"]=e.connected?"DEVICE_CONNECTED":"DEVICE_DISCONNECTED";
    }
    send(doc);
  });
  server.on("/api/lab",HTTP_GET,[]{
    JsonDocument doc; doc["bootId"]=bootId; doc["storage"]="RAM; export before restart";
    auto experiments=doc["experiments"].to<JsonArray>();
    for(size_t i=0;i<lab.experimentCount;++i) {
      const auto& e=lab.experiments[i]; auto o=experiments.add<JsonObject>();
      o["id"]=e.id;o["name"]=e.name;o["clientId"]=e.clientId;o["environment"]=e.environment;
      o["calibrated"]=e.model.calibrated();
      numeric(o,"n",e.model.calibrated()?e.model.exponent():NAN);
      numeric(o,"referenceRssi",e.model.calibrated()?e.model.referenceRssi():NAN);
      const auto* c=client(e.clientId);
      numeric(o,"currentEstimateM",c && c->connected && c->rssiValid ? e.model.estimate(c->filter.value()):NAN);
    }
    auto rows=doc["measurements"].to<JsonArray>();
    for(size_t i=0;i<lab.measurementCount;++i) measurementJson(rows.add<JsonObject>(),lab.measurements[i]);
    send(doc);
  });
  server.on("/api/experiments",HTTP_POST,[]{
    JsonDocument doc;if(!body(doc))return;
    const char* name=doc["name"]|"";
    if(!*name || strlen(name)>48 || !doc["clientId"].is<uint32_t>() ||
       !client(doc["clientId"]) || !doc["environment"].is<unsigned>() || doc["environment"].as<unsigned>()>2) {
      error(400,"name, clientId and environment required");return;
    }
    if(lab.experimentCount==8){error(409,"experiment capacity reached; export results");return;}
    auto& e=lab.experiments[lab.experimentCount++];e.id=lab.experimentCount;
    e.clientId=doc["clientId"];e.environment=doc["environment"];strlcpy(e.name,name,sizeof(e.name));
    JsonDocument reply;reply["id"]=e.id;send(reply);
  });
  server.on("/api/measurements",HTTP_POST,[]{
    JsonDocument doc;if(!body(doc))return;
    auto* e=lab.find(doc["experimentId"]|0u);
    if(!e || !number(doc["actualM"],0,10000) || !doc["calibration"].is<bool>()) {error(400,"invalid measurement");return;}
    const auto* c=client(e->clientId);
    Measurement m;m.experimentId=e->id;m.clientId=e->clientId;m.timeMs=millis();
    m.actualM=doc["actualM"];m.calibration=doc["calibration"];
    const char* notes=doc["notes"]|"";if(strlen(notes)>96){error(400,"notes too long");return;}
    strlcpy(m.notes,notes,sizeof(m.notes));
    if(!doc["x"].isNull() || !doc["y"].isNull()) {
      if(!number(doc["x"],-10000,10000)||!number(doc["y"],-10000,10000)){error(400,"invalid coordinates");return;}
      m.x=doc["x"];m.y=doc["y"];
    }
    // API failure is unknown, never evidence of disconnection.
    if(!monitor.available()){error(503,"RF observation unavailable");return;}
    if(!c){error(409,"client session expired; create a new experiment");return;}
    m.connected=c->connected;
    if(m.connected) {
      if(!c->rssiValid || uint32_t(m.timeMs-c->lastSeenMs)>2500){error(409,"RSSI unavailable");return;}
      m.rssi=c->rssi;m.filtered=c->filter.value();
    }
    if(!lab.add(m)){error(409,"measurement rejected or storage full");return;}
    JsonDocument reply;reply["ok"]=true;send(reply);
  });
  server.on("/api/calibration",HTTP_POST,[]{
    JsonDocument doc;if(!body(doc))return;
    if(!lab.calibrate(doc["experimentId"]|0u)){error(400,"need valid calibration points at distinct distances");return;}
    JsonDocument reply;reply["ok"]=true;send(reply);
  });
  server.on("/api/export.csv",HTTP_GET,[]{
    http->sendHeader("Content-Disposition","attachment; filename=sentinel-rf.csv");
    http->setContentLength(CONTENT_LENGTH_UNKNOWN);http->send(200,"text/csv; charset=utf-8","");
    http->sendContent("boot_id,experiment_id,name,environment,client_id,time_ms,actual_m,rssi_dbm,filtered_dbm,connected,calibration,estimated_m,error_m,x_m,y_m,notes\r\n");
    for(size_t i=0;i<lab.measurementCount;++i) {
      const auto& m=lab.measurements[i]; const auto* e=lab.find(m.experimentId);
      String row=String(bootId)+","+m.experimentId+","+csvText(e->name)+","+e->environment+","+m.clientId+","+m.timeMs+",";
      row+=csvNumber(m.actualM)+","+csvNumber(m.rssi)+","+csvNumber(m.filtered)+","+(m.connected?"1":"0")+","+(m.calibration?"1":"0")+",";
      row+=csvNumber(m.estimatedM)+","+csvNumber(DistanceEstimator::absoluteError(m.estimatedM,m.actualM))+","+csvNumber(m.x)+","+csvNumber(m.y)+","+csvText(m.notes)+"\r\n";
      http->sendContent(row);
    }
    http->sendContent("");
  });
#ifdef SENTINEL_REALTIME
  // Additive read-only health route for local P2 soak validation; not part of
  // Protocol v1 and no authentication is required for this local GET.
  server.on("/api/realtime/status",HTTP_GET,[]{
    sentinel::realtime::RealtimeHealthView health;
    if(!sentinel::realtime::snapshotRealtimeHealth(health)) {
      error(503,"realtime core inactive");return;
    }
    JsonDocument doc;
    doc["fw"]=health.fw;
    doc["realtimeVersion"]=health.realtimeVersion;
    doc["bootId"]=health.bootId;
    doc["uptimeMs"]=health.uptimeMs;
    doc["clockSynced"]=health.clockSynced;
    doc["clientCount"]=health.clientCount;
    doc["freeHeapBytes"]=health.freeHeapBytes;
    doc["minFreeHeapBytes"]=health.minFreeHeapBytes;
    doc["heapSamples"]=health.heapSamples;
    doc["droppedFrames"]=health.droppedFrames;
    doc["rejectedMessages"]=health.rejectedMessages;
    doc["droppedTelemetry"]=health.droppedTelemetry;
    doc["skippedSimulatedEvents"]=health.skippedSimulatedEvents;
    send(doc);
  });
#endif
}

#ifdef SENTINEL_REALTIME
const RfMonitor& realtimeMonitor() { return monitor; }
const EventEngine& realtimeEvents() { return eventEngine; }
const Laboratory& realtimeLaboratory() { return lab; }
const char* realtimeBootId() { return bootId; }
uint32_t realtimeLastQueuedEvent() { return lastQueued; }
#endif
} }
