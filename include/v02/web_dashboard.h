#pragma once

#include "v02/led_indicator.h"
#include "v02/motion_engine.h"
#include "v02/wifi_ap.h"
#ifdef SENTINEL_REALTIME
#include "rf/motion_ingest.h"
#endif

namespace sentinel {
namespace v02 {

class WebDashboard {
 public:
  void begin(MotionEngine& engine, LedIndicator& leds, const WiFiApInfo& wifi);
  void handleClient();

  bool simulationEnabled() const { return simulation_; }

 private:
  static void handleRoot();
  static void handleState();
  static void handleAccel();
  static void handleConfig();
  static void handleNotFound();
#ifdef SENTINEL_RF
  static void handleMotionPush();
  static void handleMotionStatus();
  static void handleMotionConfig();
  static void handleMotionSetup();
#endif

  static WebDashboard* instance_;

  MotionEngine* engine_ = nullptr;
  LedIndicator* leds_ = nullptr;
  const WiFiApInfo* wifi_ = nullptr;
  bool simulation_ = false;
};

#ifdef SENTINEL_REALTIME
const rf::MotionIngest& realtimeMotionIngest();
#endif

}  // namespace v02
}  // namespace sentinel
