#include "realtime/web_app.h"

#include <WebServer.h>

namespace sentinel {
namespace realtime {
namespace {

WebServer* gServer = nullptr;

void serve(const WebAsset& asset) {
  if (!gServer) return;
  // Every asset is stored gzip-compressed; all supported browsers accept it.
  gServer->sendHeader("Content-Encoding", "gzip");
  gServer->sendHeader("Cache-Control", asset.cacheControl);
  gServer->sendHeader("X-Content-Type-Options", "nosniff");
  gServer->send_P(200, asset.contentType,
                  reinterpret_cast<const char*>(asset.data), asset.length);
}

}  // namespace

void registerWebApp(WebServer& server) {
  gServer = &server;
  const WebAsset* assets = nullptr;
  const size_t count = webAppAssets(&assets);
  for (size_t i = 0; i < count; ++i) {
    const WebAsset* asset = &assets[i];
    server.on(asset->path, HTTP_GET, [asset] { serve(*asset); });
  }
}

}  // namespace realtime
}  // namespace sentinel
