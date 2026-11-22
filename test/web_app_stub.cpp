// Host stand-in for the flash-embedded web bundle (web_app_data.cpp).
#include "realtime/web_app.h"

namespace sentinel {
namespace realtime {

namespace {
const uint8_t kIndex[] = {0x1f, 0x8b, 'i'};
const uint8_t kJs[] = {0x1f, 0x8b, 'j'};
}  // namespace

size_t webAppAssets(const WebAsset** assets) {
  static const WebAsset kAssets[] = {
      {"/", "text/html; charset=utf-8", "no-cache", kIndex, sizeof(kIndex)},
      {"/assets/app.js", "text/javascript; charset=utf-8",
       "public, max-age=31536000, immutable", kJs, sizeof(kJs)},
  };
  *assets = kAssets;
  return 2;
}

}  // namespace realtime
}  // namespace sentinel
