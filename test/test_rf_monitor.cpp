#include "rf/rf_monitor.h"
#include <esp_wifi.h>
#include <cassert>
#include <cstdio>
using namespace sentinel::rf;
static wifi_sta_list_t stations{};
static bool fail=false;
int esp_wifi_ap_get_sta_list(wifi_sta_list_t* out){if(fail)return -1;*out=stations;return 0;}
int esp_wifi_get_channel(uint8_t* c,wifi_second_chan_t* s){*c=6;*s=0;return 0;}
int main(){
  RfMonitor monitor;monitor.update(100);assert(!monitor.available());
  stations.num=4;for(int i=0;i<4;++i){stations.sta[i].mac[5]=i+1;stations.sta[i].rssi=-40-i*10;}
  monitor.update(1000);assert(monitor.count()==4 && monitor.events().size()==4);
  for(int i=0;i<4;++i){assert(monitor.clients()[i].connected);assert(monitor.clients()[i].history.size()==1);}
  monitor.update(1200);assert(monitor.clients()[0].history.size()==1);
  fail=true;monitor.update(2000);assert(!monitor.available());
  assert(monitor.clients()[0].connected && !monitor.clients()[0].rssiValid);
  assert(monitor.events().size()==4);
  fail=false;stations.num=3;monitor.update(3000);
  assert(monitor.events().size()==5 && !monitor.events().at(4).connected);
  stations.sta[0].rssi=0;monitor.update(4000);assert(!monitor.clients()[0].rssiValid);
  stations.sta[0].rssi=-60;monitor.update(5000);assert(monitor.clients()[0].filter.value()==-60);
  stations.num=0;monitor.update(6000);assert(monitor.count()==0);
  stations.num=1;stations.sta[0].mac[5]=9;monitor.update(7000);
  assert(monitor.events().at(monitor.events().size()-1).clientId==5);
  for(unsigned i=8;i<80;++i)monitor.update(i*1000);
  size_t max=0;for(int i=0;i<8;++i)if(monitor.clients()[i].connected)max=monitor.clients()[i].history.size();
  assert(max==32);
  std::puts("test_rf_monitor: ALL TESTS PASSED (mock SDK, four clients)");
}
