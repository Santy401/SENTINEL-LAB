#pragma once

// ---------------------------------------------------------------------------
// SENTINEL Wi-Fi credentials template.
//
// Copy this file to `include/secrets.h` (which is gitignored) and replace the
// placeholders BEFORE flashing. Never commit real credentials to the repo.
//
//   cp include/secrets.example.h include/secrets.h
//   # edit include/secrets.h
//
// WPA2 requires a passphrase of at least 8 characters.
// ---------------------------------------------------------------------------

#define SENTINEL_WIFI_SSID     "SENTINEL-CAMBIAR"
#define SENTINEL_WIFI_PASSWORD "CLAVE-WPA2-CAMBIAR-12345"

// Optional bearer token for privileged P2 WebSocket gateway sessions.
// Empty disables privileged operations; telemetry remains locally readable.
// Use a long random value and never commit the populated secrets.h.
#define SENTINEL_REALTIME_BEARER_TOKEN ""
