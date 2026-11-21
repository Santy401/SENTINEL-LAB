#include "rf/distance_estimator.h"
#include "rf/ring.h"
#include "rf/laboratory.h"
#include "rf/event_engine.h"
#include <cassert>
#include <cstdio>
using namespace sentinel::rf;
int main() {
  DistanceEstimator model;
  assert(std::isnan(model.estimate(-60)));
  CalibrationPoint p[]={{1,-40},{10,-60},{100,-80}};
  assert(model.calibrate(p,3));
  assert(std::fabs(model.exponent()-2)<1e-9);
  assert(std::fabs(model.estimate(-60)-10)<1e-9);
  assert(std::fabs(DistanceEstimator::absoluteError(10,8)-2)<1e-9);
  assert(std::isnan(DistanceEstimator::absoluteError(NAN,8)));
  assert(!validRssi(0) && !validRssi(-101) && !validRssi(INFINITY));
  assert(validRssi(-100) && validRssi(-1));
  CalibrationPoint same[]={{1,-40},{1,-50}};
  assert(!model.calibrate(same,2));
  CalibrationPoint rising[]={{1,-60},{10,-40}};
  assert(!model.calibrate(rising,2));
  assert(!model.calibrate(nullptr,2));
  p[0].distanceM=0; assert(!model.calibrate(p,3));
  p[0].distanceM=NAN; assert(!model.calibrate(p,3));
  assert(std::fabs(model.estimate(-60)-10)<1e-9);
  RssiFilter filter; assert(!filter.ready());
  assert(filter.feed(-40)); assert(filter.value()==-40);
  assert(filter.feed(-60)); assert(filter.value()==-45);
  assert(!filter.feed(0)); assert(!filter.ready());
  assert(filter.feed(-80)); assert(filter.value()==-80);
  Ring<int,3> ring; for(int i=0;i<10;++i) ring.push(i);
  assert(ring.size()==3 && ring.at(0)==7 && ring.at(2)==9);
  ring.clear(); assert(ring.size()==0); ring.push(42); assert(ring.at(0)==42);
  Laboratory lab; lab.experimentCount=1; lab.experiments[0].id=1; lab.experiments[0].clientId=7;
  Measurement m; m.experimentId=1;m.clientId=7;m.actualM=1;m.connected=true;m.calibration=true;m.rssi=-40;m.filtered=-40;
  assert(lab.add(m));m.actualM=10;m.rssi=m.filtered=-60;assert(lab.add(m));assert(lab.calibrate(1));
  m.calibration=false;assert(lab.add(m));assert(std::fabs(lab.measurements[2].estimatedM-10)<1e-9);
  m.clientId=8;assert(!lab.add(m));m.clientId=7;m.connected=false;m.rssi=m.filtered=NAN;assert(lab.add(m));
  assert(std::isnan(lab.measurements[3].estimatedM));
  m.calibration=true;assert(!lab.add(m));m.calibration=false;
  while(lab.measurementCount<96) { assert(lab.add(m)); }
  assert(!lab.add(m));
  EventEngine events;
  events.observe(0,1,true,-60,100); assert(events.events().size()==1);
  events.observe(0,1,true,-81,1100);events.observe(0,1,true,-81,2100);
  events.observe(0,1,true,-79,3100); // Break consecutive streak.
  events.observe(0,1,true,-81,4100);events.observe(0,1,true,-81,5100);
  assert(events.events().size()==1);events.observe(0,1,true,-81,6100);
  assert(events.events().at(1).type==EventType::SignalWeak);
  assert(events.events().at(1).notify);
  for(int i=0;i<3;++i)events.observe(0,1,true,-70,7100+i*1000);
  assert(events.events().at(2).type==EventType::SignalRecovered);
  assert(!events.events().at(2).notify);
  events.observe(0,1,false,NAN,10100);events.observe(0,1,false,NAN,11100);
  assert(events.events().size()==4);
  events.observe(0,1,true,-60,12100);assert(events.events().size()==5);
  for(int i=0;i<60;++i)events.emit(EventType::Heartbeat,0,15000+i);
  assert(events.events().size()==48 && events.events().at(47).sequence==65);
  std::puts("test_rf: ALL TESTS PASSED");
}
