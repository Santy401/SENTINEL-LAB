#pragma once
#include "rf/ring.h"
#include "rf/distance_estimator.h"
#include <stdint.h>
namespace sentinel { namespace rf {
enum class EventType { DeviceConnected, DeviceDisconnected, SignalWeak, SignalRecovered,
  CoverageLimitReached, MovementDetected, MotionAlert, SensorOffline, SensorRecovered, Heartbeat };
inline const char* eventName(EventType t) {
  switch(t) {
    case EventType::DeviceConnected:return "DEVICE_CONNECTED";
    case EventType::DeviceDisconnected:return "DEVICE_DISCONNECTED";
    case EventType::SignalWeak:return "SIGNAL_WEAK";
    case EventType::SignalRecovered:return "SIGNAL_RECOVERED";
    case EventType::CoverageLimitReached:return "COVERAGE_LIMIT_REACHED";
    case EventType::MovementDetected:return "MOVEMENT_DETECTED";
    case EventType::MotionAlert:return "MOTION_ALERT";
    case EventType::SensorOffline:return "SENSOR_OFFLINE";
    case EventType::SensorRecovered:return "SENSOR_RECOVERED";
    default:return "HEARTBEAT";
  }
}
struct Event {
  uint32_t sequence=0,timeMs=0,clientId=0;
  EventType type=EventType::Heartbeat;
  double rssi=NAN,distanceM=NAN;
  bool simulated=false,notify=false;
};
class EventEngine {
 public:
  bool anyWeak() const {for(const auto& s:states_)if(s.connected&&s.weak)return true;return false;}
  const Ring<Event,48>& events() const {return events_;}
  Event emit(EventType type,uint32_t client,uint32_t now,double rssi=NAN,bool simulated=false,bool notify=true) {
    Event e{++sequence_,now,client,type,rssi,NAN,simulated,notify};events_.push(e);return e;
  }
  void observe(size_t slot,uint32_t id,bool connected,double rssi,uint32_t now) {
    if(slot>=8 || !id)return;
    auto& s=states_[slot];
    if(s.id!=id) {
      if(s.connected)emit(EventType::DeviceDisconnected,s.id,now);
      s=SignalState{};s.id=id;
    }
    if(s.connected!=connected) {
      emit(connected?EventType::DeviceConnected:EventType::DeviceDisconnected,id,now,rssi);
      s.connected=connected;s.weak=false;s.streak=0;
    }
    if(!connected || !validRssi(rssi)){s.streak=0;return;}
    const bool candidate=s.weak?rssi>-75:rssi<-80;
    if(!candidate){s.streak=0;return;}
    if(++s.streak<3)return;
    s.streak=0;s.weak=!s.weak;
    const bool eligible=!s.notified || uint32_t(now-s.lastNotify)>=30000;
    emit(s.weak?EventType::SignalWeak:EventType::SignalRecovered,id,now,rssi,false,eligible);
    if(eligible){s.lastNotify=now;s.notified=true;}
  }
 private:
  struct SignalState {uint32_t id=0,lastNotify=0;bool connected=false,weak=false,notified=false;uint8_t streak=0;};
  SignalState states_[8]{};
  Ring<Event,48> events_;
  uint32_t sequence_=0;
};
} }
