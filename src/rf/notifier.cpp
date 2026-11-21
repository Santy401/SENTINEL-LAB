#include "rf/notifier.h"
#include "rf/event_json.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#if __has_include("rf/connected.h")
#include "rf/connected.h"
#else
#include "rf/connected.example.h"
#endif
namespace sentinel { namespace rf {
namespace {
QueueHandle_t queue=nullptr;
char boot[17]{};
bool enabled=false;
void worker(void*) {
  Event e;
  for(;;) {
    if(xQueueReceive(queue,&e,portMAX_DELAY)!=pdTRUE)continue;
    JsonDocument doc;eventJson(doc.to<JsonObject>(),e,boot);
    String payload;serializeJson(doc,payload);
    for(int attempt=0;attempt<3;++attempt) {
      if(WiFi.status()!=WL_CONNECTED || time(nullptr)<1700000000)break;
      NetworkClientSecure tls;tls.setCACert(SENTINEL_RF_WEBHOOK_CA);tls.setHandshakeTimeout(3);
      HTTPClient request;request.setConnectTimeout(1500);request.setTimeout(1500);
      if(!request.begin(tls,SENTINEL_RF_WEBHOOK_URL))break;
      request.addHeader("Content-Type","application/json");
      request.addHeader("X-Sentinel-Token",SENTINEL_RF_WEBHOOK_TOKEN);
      const int status=request.POST(payload);request.end();
      if(status>=200&&status<300)break;
      if(status>=400&&status<500&&status!=429)break;
      vTaskDelay(pdMS_TO_TICKS(500*(attempt+1)));
    }
  }
}
}
void beginNotifier(const char* bootId) {
  strlcpy(boot,bootId,sizeof(boot));
  if(!strlen(SENTINEL_RF_STA_SSID)||strlen(SENTINEL_RF_STA_PASSWORD)<8)return;
  WiFi.mode(WIFI_AP_STA);WiFi.begin(SENTINEL_RF_STA_SSID,SENTINEL_RF_STA_PASSWORD);
  if(strlen(SENTINEL_RF_NTP_SERVER))configTime(0,0,SENTINEL_RF_NTP_SERVER);
  if(!String(SENTINEL_RF_WEBHOOK_URL).startsWith("https://") ||
     strlen(SENTINEL_RF_WEBHOOK_TOKEN)<16 || !strlen(SENTINEL_RF_WEBHOOK_CA))return;
  queue=xQueueCreate(8,sizeof(Event));if(!queue)return;
  if(xTaskCreate(worker,"rf-notifier",8192,nullptr,1,nullptr)!=pdPASS){vQueueDelete(queue);queue=nullptr;return;}
  enabled=true;
}
void queueNotification(const Event& event) {
  if(enabled && event.notify) xQueueSend(queue,&event,0); // Full queue drops newest; loop never waits.
}
bool notifierEnabled(){return enabled;}
} }
