#include "v02/web_dashboard.h"

#include "v02/dashboard_html.h"
#include "v02/settings.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#ifdef SENTINEL_RF
#include "rf/motion_ingest.h"
#include "rf/motion_setup_html.h"
#include "rf/rf_monitor.h"
#endif

namespace sentinel {
namespace v02 {

namespace {

WebServer server(settings::kHttpPort);

float sanitizeAccel(float value) {
  if (!isfinite(value)) {
    return 0.0f;
  }
  if (value > 500.0f) return 500.0f;
  if (value < -500.0f) return -500.0f;
  return value;
}

void sendJson(int code, const char* body) {
  server.send(code, "application/json", body);
}

#ifdef SENTINEL_RF
rf::MotionIngest motionIngest;

void sendJsonDoc(int code, JsonDocument& doc) {
  String output;
  serializeJson(doc, output);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", output);
}
#endif

}  // namespace

WebDashboard* WebDashboard::instance_ = nullptr;

void WebDashboard::begin(MotionEngine& engine, LedIndicator& leds,
                         const WiFiApInfo& wifi) {
  engine_ = &engine;
  leds_ = &leds;
  wifi_ = &wifi;
  instance_ = this;

#ifdef SENTINEL_RF
  motionIngest.attach(engine);
  server.on("/motion", HTTP_GET, handleRoot);
  server.on("/motion/native", HTTP_GET, handleMotionSetup);
  server.on("/api/motion/sensorlogger", HTTP_POST, handleMotionPush);
  server.on("/api/motion/status", HTTP_GET, handleMotionStatus);
  server.on("/api/motion/config", HTTP_POST, handleMotionConfig);
  sentinel::rf::registerRoutes(server);
#else
  server.on("/", HTTP_GET, handleRoot);
#endif
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/accel", HTTP_POST, handleAccel);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("[HTTP] server listening on port 80"));
}

void WebDashboard::handleClient() {
  server.handleClient();
}

void WebDashboard::handleRoot() {
#ifdef SENTINEL_RF
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent_P(kDashboardHtml);
  server.sendContent(F("<script>document.getElementById('btnMotion').onclick=function(){location.href='/capture'};</script>"));
  server.sendContent("");
#else
  server.send_P(200, "text/html", kDashboardHtml);
#endif
}

void WebDashboard::handleState() {
  if (instance_ == nullptr || instance_->engine_ == nullptr) {
    sendJson(503, "{\"ok\":false,\"error\":\"not ready\"}");
    return;
  }

  MotionEngine& engine = *instance_->engine_;
  const MotionSnapshot& s = engine.snapshot();
  const uint32_t now = millis();

  // ArduinoJson 7 grows dynamically; no fixed-capacity constructor exists.
  JsonDocument doc;
#ifdef SENTINEL_RF
  doc["fw"] = "0.3.1";
#else
  doc["fw"] = "0.2.0";
#endif
  doc["uptimeMs"] = now;
  doc["state"] = motionStateName(s.state);
  doc["fresh"] = engine.fresh(now);
  doc["source"] = accelSourceName(s.source);
  doc["simulation"] = instance_->simulation_;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["mode"] = "AP";
  wifi["ssid"] = instance_->wifi_->ssid;
  wifi["ip"] = instance_->wifi_->ip;
  wifi["clients"] = instance_->wifi_->clients;

  JsonObject accel = doc["accel"].to<JsonObject>();
  accel["x"] = s.rawX;
  accel["y"] = s.rawY;
  accel["z"] = s.rawZ;
  accel["total"] = s.totalMagnitude;
  accel["linear"] = s.linearMagnitude;
  accel["gx"] = s.gravityX;
  accel["gy"] = s.gravityY;
  accel["gz"] = s.gravityZ;

  doc["sampleCount"] = s.sampleCount;
  doc["lastSampleMs"] = s.lastSampleMs;

  const MotionThresholds& t = engine.thresholds();
  JsonObject thresholds = doc["thresholds"].to<JsonObject>();
  thresholds["movement"] = t.movement;
  thresholds["alert"] = t.alert;
  thresholds["hysteresis"] = t.hysteresis;
  thresholds["confirmations"] = t.confirmations;
  thresholds["timeoutMs"] = t.timeoutMs;

  JsonObject leds = doc["leds"].to<JsonObject>();
  leds["stable"] = instance_->leds_->isOn(LedId::Stable);
  leds["movement"] = instance_->leds_->isOn(LedId::Movement);
  leds["alert"] = instance_->leds_->isOn(LedId::Alert);

  JsonArray events = doc["events"].to<JsonArray>();
  const size_t count = engine.eventCount();
  const size_t shown = (count < settings::kMaxEventsInJson)
                           ? count
                           : settings::kMaxEventsInJson;
  for (size_t i = count - shown; i < count; ++i) {
    const MotionEvent& event = engine.eventAt(i);
    JsonObject item = events.add<JsonObject>();
    item["t"] = event.timestampMs;
    item["type"] = motionEventName(event.type);
    item["from"] = motionStateName(event.from);
    item["to"] = motionStateName(event.to);
    item["mag"] = event.magnitude;
  }

  String output;
  serializeJson(doc, output);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", output);
}

void WebDashboard::handleAccel() {
  if (instance_ == nullptr || instance_->engine_ == nullptr) {
    sendJson(503, "{\"ok\":false,\"error\":\"not ready\"}");
    return;
  }

#ifdef SENTINEL_RF
  if (server.arg("plain").length() > 1024) {
    sendJson(413, "{\"ok\":false,\"error\":\"body too large\"}");
    return;
  }
#endif
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    sendJson(400, "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

#ifdef SENTINEL_RF
  for (const char* key : {"x", "y", "z"}) {
    if (!doc[key].is<float>() || !isfinite(doc[key].as<float>()) || fabsf(doc[key].as<float>()) > 500) {
      sendJson(400, "{\"ok\":false,\"error\":\"finite acceleration required\"}");
      return;
    }
  }
#endif
  AccelSample sample;
  sample.x = sanitizeAccel(doc["x"] | 0.0f);
  sample.y = sanitizeAccel(doc["y"] | 0.0f);
  sample.z = sanitizeAccel(doc["z"] | 0.0f);
  sample.timestampMs = millis();  // server clock, never the client's

  const bool simulated = doc["simulated"] | false;
  const String origin = doc["source"] | "";
#ifdef SENTINEL_RF
  if (origin == "native" || origin == "nativeClient" ||
      origin == "sensorLogger") {
    sendJson(403, "{\"ok\":false,\"error\":\"use /api/motion/sensorlogger\"}");
    return;
  }
#endif
  if (simulated || origin == "simulated") {
    sample.source = AccelSource::Simulated;
  } else if (origin == "native") {
    sample.source = AccelSource::NativeClient;
  } else {
    sample.source = AccelSource::BrowserDeviceMotion;
  }

  instance_->engine_->feed(sample);
  sendJson(200, "{\"ok\":true}");
}

void WebDashboard::handleConfig() {
  if (instance_ == nullptr || instance_->engine_ == nullptr) {
    sendJson(503, "{\"ok\":false,\"error\":\"not ready\"}");
    return;
  }

#ifdef SENTINEL_RF
  if (server.arg("plain").length() > 1024) {
    sendJson(413, "{\"ok\":false,\"error\":\"body too large\"}");
    return;
  }
#endif
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    sendJson(400, "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

  if (!doc["simulation"].isNull()) {
    instance_->simulation_ = doc["simulation"] | false;
  }

  MotionThresholds t = instance_->engine_->thresholds();
  if (!doc["movement"].isNull())      t.movement = doc["movement"];
  if (!doc["alert"].isNull())         t.alert = doc["alert"];
  if (!doc["hysteresis"].isNull())    t.hysteresis = doc["hysteresis"];
  if (!doc["confirmations"].isNull()) t.confirmations = doc["confirmations"];
  if (!doc["timeoutMs"].isNull())     t.timeoutMs = doc["timeoutMs"];
  instance_->engine_->configure(t);

  sendJson(200, "{\"ok\":true}");
}

#ifdef SENTINEL_RF
void WebDashboard::handleMotionPush() {
  const String body = server.arg("plain");
  const rf::MotionIngestResult result =
      motionIngest.ingest(body.c_str(), body.length(), millis());

  JsonDocument doc;
  doc["ok"] = result.status == rf::MotionIngestStatus::Ok;
  doc["accepted"] = result.accepted;
  doc["skipped"] = result.skipped;
  doc["error"] = result.error;
  if (result.retryAfterMs != 0) doc["retryAfterMs"] = result.retryAfterMs;
  sendJsonDoc(result.httpCode, doc);
}

void WebDashboard::handleMotionStatus() {
  const rf::MotionIngestSnapshot s = motionIngest.snapshot(millis());
  JsonDocument doc;
  doc["fw"] = "0.3.1";
  doc["ready"] = s.ready;
  doc["authorized"] = s.authorized;
  doc["allowedDeviceId"] = s.allowedDeviceId;
  doc["pendingDeviceId"] = s.pendingDeviceId;
  doc["lastDeviceId"] = s.lastDeviceId;
  doc["lastSessionId"] = s.lastSessionId;
  doc["lastMessageId"] = s.lastMessageId;
  doc["lastSampleEpochNs"] = s.lastSampleEpochNs;
  doc["lastAcceptedMs"] = s.lastAcceptedMs;
  doc["sampleAgeMs"] = s.sampleAgeMs;
  doc["acceptedSamples"] = s.acceptedSamples;
  doc["skippedSamples"] = s.skippedSamples;
  doc["lastAcceptedBatch"] = s.lastAcceptedBatch;
  doc["lastSkippedBatch"] = s.lastSkippedBatch;
  doc["lastHttpCode"] = s.lastHttpCode;
  doc["lastError"] = s.lastError;
  doc["signalLost"] = s.signalLost;
  doc["minIntervalMs"] = s.minIntervalMs;
  if (instance_ != nullptr && instance_->engine_ != nullptr) {
    const MotionSnapshot& motion = instance_->engine_->snapshot();
    doc["source"] = accelSourceName(motion.source);
    doc["motionState"] = motionStateName(motion.state);
  }
  sendJsonDoc(200, doc);
}

void WebDashboard::handleMotionConfig() {
  if (server.arg("plain").length() > 512) {
    sendJson(413, "{\"ok\":false,\"error\":\"body too large\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
    sendJson(400, "{\"ok\":false,\"error\":\"invalid json object\"}");
    return;
  }
  const char* deviceId = doc["deviceId"] | "";
  const uint32_t interval = doc["minIntervalMs"].is<uint32_t>()
                                ? doc["minIntervalMs"].as<uint32_t>()
                                : motionIngest.config().minIntervalMs;
  if (!motionIngest.configure(deviceId, interval) ||
      (deviceId[0] == '\0' && doc["deviceId"].isNull())) {
    sendJson(400, "{\"ok\":false,\"error\":\"invalid deviceId\"}");
    return;
  }
  if (deviceId[0] == '\0') motionIngest.clearAuthorization();
  JsonDocument reply;
  reply["ok"] = true;
  reply["authorized"] = motionIngest.config().allowedDeviceId[0] != '\0';
  sendJsonDoc(200, reply);
}

void WebDashboard::handleMotionSetup() {
  server.send_P(200, "text/html; charset=utf-8", rf::kMotionSetupHtml);
}
#ifdef SENTINEL_REALTIME
const rf::MotionIngest& realtimeMotionIngest() { return motionIngest; }
#endif
#endif

void WebDashboard::handleNotFound() {
  sendJson(404, "{\"ok\":false,\"error\":\"not found\"}");
}

}  // namespace v02
}  // namespace sentinel
