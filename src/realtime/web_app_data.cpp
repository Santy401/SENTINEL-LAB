// Binary web bundle linked by board_build.embed_files (see platformio.ini).
#include "realtime/web_app.h"

extern const uint8_t index_start[] asm("_binary_web_assets_index_html_gz_start");
extern const uint8_t index_end[] asm("_binary_web_assets_index_html_gz_end");
extern const uint8_t js_start[] asm("_binary_web_assets_app_js_gz_start");
extern const uint8_t js_end[] asm("_binary_web_assets_app_js_gz_end");
extern const uint8_t css_start[] asm("_binary_web_assets_app_css_gz_start");
extern const uint8_t css_end[] asm("_binary_web_assets_app_css_gz_end");
extern const uint8_t webmanifest_start[] asm("_binary_web_assets_manifest_webmanifest_gz_start");
extern const uint8_t webmanifest_end[] asm("_binary_web_assets_manifest_webmanifest_gz_end");
extern const uint8_t touch_start[] asm("_binary_web_assets_apple_touch_icon_png_gz_start");
extern const uint8_t touch_end[] asm("_binary_web_assets_apple_touch_icon_png_gz_end");
extern const uint8_t icon192_start[] asm("_binary_web_assets_icon_192_png_gz_start");
extern const uint8_t icon192_end[] asm("_binary_web_assets_icon_192_png_gz_end");
extern const uint8_t icon512_start[] asm("_binary_web_assets_icon_512_png_gz_start");
extern const uint8_t icon512_end[] asm("_binary_web_assets_icon_512_png_gz_end");

namespace sentinel {
namespace realtime {

size_t webAppAssets(const WebAsset** assets) {
  // index.html is never cached so a new firmware is picked up at once; the
  // bundles carry a content hash in their query string and can be cached.
  static const WebAsset kAssets[] = {
      {"/", "text/html; charset=utf-8", "no-cache", index_start,
       static_cast<size_t>(index_end - index_start)},
      {"/index.html", "text/html; charset=utf-8", "no-cache", index_start,
       static_cast<size_t>(index_end - index_start)},
      {"/assets/app.js", "text/javascript; charset=utf-8",
       "public, max-age=31536000, immutable", js_start,
       static_cast<size_t>(js_end - js_start)},
      {"/assets/app.css", "text/css; charset=utf-8",
       "public, max-age=31536000, immutable", css_start,
       static_cast<size_t>(css_end - css_start)},
      // Home-screen install (Safari "Add to Home Screen").
      {"/manifest.webmanifest", "application/manifest+json", "no-cache",
       webmanifest_start,
       static_cast<size_t>(webmanifest_end - webmanifest_start)},
      {"/apple-touch-icon.png", "image/png", "public, max-age=86400",
       touch_start, static_cast<size_t>(touch_end - touch_start)},
      {"/icon-192.png", "image/png", "public, max-age=86400", icon192_start,
       static_cast<size_t>(icon192_end - icon192_start)},
      {"/icon-512.png", "image/png", "public, max-age=86400", icon512_start,
       static_cast<size_t>(icon512_end - icon512_start)},
  };
  *assets = kAssets;
  return sizeof(kAssets) / sizeof(kAssets[0]);
}

}  // namespace realtime
}  // namespace sentinel
