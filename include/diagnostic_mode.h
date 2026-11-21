#pragma once

#include "distance_sensor.h"

namespace sentinel {
namespace diagnostic {

void report(const DistanceSensor& sensor);
void printSummary();

}  // namespace diagnostic
}  // namespace sentinel
