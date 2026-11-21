#include "rf/rf_monitor.h"
#include <esp_wifi.h>
#include <cstring>
namespace sentinel { namespace rf {
void RfMonitor::update(uint32_t now) {
  if (uint32_t(now - lastPoll_) < 1000) return;
  lastPoll_ = now;
  wifi_sta_list_t list{};
  available_ = esp_wifi_ap_get_sta_list(&list) == ESP_OK;
  if (!available_) {
    count_ = 0; // Unknown, never a stale association count.
    for (auto& c : clients_) { c.rssiValid = false; c.filter.reset(); }
    return;
  }
  wifi_second_chan_t secondary;
  if (esp_wifi_get_channel(&channel_, &secondary) != ESP_OK) channel_ = 0;
  const int total = list.num > 8 ? 8 : list.num; // clients_ has 8 slots.
  count_ = static_cast<uint8_t>(total);
  // Reconcile departures before assigning slots to new associations.
  for (auto& c : clients_) {
    if (!c.connected) continue;
    bool found = false;
    for (int i = 0; i < total; ++i)
      if (!std::memcmp(c.mac, list.sta[i].mac, 6)) found = true;
    if (!found) { c.connected = c.rssiValid = false; c.filter.reset(); events_.push({now, c.id, false}); }
  }
  for (int i = 0; i < total; ++i) {
    auto& sta = list.sta[i];
    Client* target = nullptr;
    for (auto& c : clients_)
      if (c.connected && !std::memcmp(c.mac, sta.mac, 6)) target = &c;
    if (!target) {
      for (auto& c : clients_) if (!c.connected && (!target || c.id < target->id)) target = &c;
      if (!target) continue;
      *target = Client{};
      target->id = nextId_++;
      std::memcpy(target->mac, sta.mac, 6);
      target->connected = true;
      events_.push({now, target->id, true});
    }
    target->lastSeenMs = now;
    target->rssi = sta.rssi;
    target->rssiValid = sta.rssi >= -100 && sta.rssi < 0;
    target->filter.feed(sta.rssi);
    if (target->rssiValid) target->history.push({now, sta.rssi});
  }
}
} }
