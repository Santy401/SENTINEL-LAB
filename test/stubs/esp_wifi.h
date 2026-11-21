#pragma once
#include <stdint.h>
constexpr int ESP_OK=0;
using wifi_second_chan_t=int;
struct wifi_sta_info_t {uint8_t mac[6];int8_t rssi;};
struct wifi_sta_list_t {wifi_sta_info_t sta[15];int num;};
int esp_wifi_ap_get_sta_list(wifi_sta_list_t*);
int esp_wifi_get_channel(uint8_t*,wifi_second_chan_t*);
