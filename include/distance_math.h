#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace math {

// distance = duration * speedOfSound / 2
inline float distanceCm(uint32_t durationUs, float speedCmPerUs) {
  return static_cast<float>(durationUs) * speedCmPerUs / 2.0f;
}

// In-place insertion sort + median. Safe for small sample buffers.
inline float median(float* values, size_t count) {
  if (count == 0) {
    return 0.0f;
  }
  for (size_t i = 1; i < count; ++i) {
    const float key = values[i];
    int j = static_cast<int>(i) - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
  if ((count % 2) == 1) {
    return values[count / 2];
  }
  return (values[count / 2 - 1] + values[count / 2]) / 2.0f;
}

}  // namespace math
}  // namespace sentinel
