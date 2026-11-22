#pragma once

#include "rf/event_engine.h"
#include "rf/laboratory.h"
#include "rf/motion_ingest.h"
#include "rf/rf_monitor.h"

namespace sentinel {
namespace rf {

// P2-only accessors. They expose the existing v0.3.1 state read-only and are
// defined only when SENTINEL_REALTIME is enabled.
const RfMonitor& realtimeMonitor();
const EventEngine& realtimeEvents();
const Laboratory& realtimeLaboratory();
const char* realtimeBootId();
uint32_t realtimeLastQueuedEvent();

}  // namespace rf
}  // namespace sentinel
