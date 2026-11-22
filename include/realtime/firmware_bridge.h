#pragma once

#include "realtime/protocol_adapter.h"
#include "rf/event_engine.h"
#include "rf/laboratory.h"
#include "rf/motion_ingest.h"
#include "rf/rf_monitor.h"

namespace sentinel {
namespace v02 {
class MotionEngine;
const MotionEngine& realtimeMotionEngine();
}  // namespace v02

namespace realtime {

size_t snapshotRfClients(RfClientView* output, size_t capacity,
                         uint8_t& channel, bool& available);
bool snapshotMotionStatus(uint32_t nowMs, MotionStatusView& output);
const char* firmwareBootId();
const rf::EventEngine& firmwareEvents();
uint32_t firmwareFreeHeapBytes();
uint32_t firmwareLargestFreeBlockBytes();
// Free bytes never used on the calling task's stack (high-water mark).
uint32_t firmwareLoopStackFreeBytes();
// esp_reset_reason() of the current boot, as a fixed uppercase name.
const char* firmwareResetReason();

}  // namespace realtime
}  // namespace sentinel
