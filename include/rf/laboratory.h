#pragma once
#include "rf/distance_estimator.h"
#include <stdint.h>
namespace sentinel { namespace rf {
struct Measurement {
  uint32_t id=0, experimentId=0, clientId=0, timeMs=0;
  double actualM=0, rssi=NAN, filtered=NAN, estimatedM=NAN, x=NAN, y=NAN;
  bool connected=false, calibration=false;
  char notes[97]{};
};
struct Experiment {
  uint32_t id=0, clientId=0;
  char name[49]{};
  uint8_t environment=0; // 0 open, 1 indoor, 2 obstacles.
  DistanceEstimator model;
};
class Laboratory {
 public:
  Experiment experiments[8]{};
  Measurement measurements[96]{};
  size_t experimentCount=0, measurementCount=0;
  Experiment* find(uint32_t id) {
    for (size_t i=0;i<experimentCount;++i) if(experiments[i].id==id) return &experiments[i];
    return nullptr;
  }
  bool add(Measurement m) {
    auto* e=find(m.experimentId);
    if(!e || m.clientId!=e->clientId || measurementCount==96 ||
       !std::isfinite(m.actualM) || m.actualM<0 || m.actualM>10000 ||
       (m.connected && !validRssi(m.rssi)) ||
       (m.calibration && (!m.connected || m.actualM<=0))) return false;
    m.id=measurementCount+1;
    m.estimatedM=m.connected ? e->model.estimate(m.filtered) : NAN;
    measurements[measurementCount++]=m; return true;
  }
  bool calibrate(uint32_t id) {
    auto* e=find(id); if(!e) return false;
    CalibrationPoint points[96]; size_t count=0;
    for(size_t i=0;i<measurementCount;++i) {
      const auto& m=measurements[i];
      if(m.experimentId==id && m.calibration && m.connected && validRssi(m.rssi))
        points[count++]={m.actualM,m.rssi};
    }
    return e->model.calibrate(points,count);
  }
};
} }
