#pragma once

#include <stddef.h>
#include <stdint.h>

class WebServer;

namespace sentinel {
namespace realtime {

// One gzip-compressed file of the embedded web dashboard (apps/web), produced
// by tools/embed_web.py and linked into flash with board_build.embed_files.
struct WebAsset {
  const char* path;
  const char* contentType;
  const char* cacheControl;
  const uint8_t* data;
  size_t length;
};

// Provided by web_app_data.cpp on the firmware and by a stub in host tests.
size_t webAppAssets(const WebAsset** assets);

// Registers "/", "/index.html" and the asset routes. Must run before any other
// handler for "/" is registered. Only built into the _realtime image.
void registerWebApp(WebServer& server);

}  // namespace realtime
}  // namespace sentinel
