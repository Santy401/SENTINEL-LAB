#pragma once
// Copy to include/rf/connected.h only after configuring connectivity is authorized.
// Never commit connected.h. Empty values disable connection and notifications.
#define SENTINEL_RF_STA_SSID ""
#define SENTINEL_RF_STA_PASSWORD ""
#define SENTINEL_RF_WEBHOOK_URL ""
#define SENTINEL_RF_WEBHOOK_TOKEN ""
// PEM trust anchor for HTTPS receiver. No insecure TLS fallback.
#define SENTINEL_RF_WEBHOOK_CA ""

// Optional local NTP server on the authorized LAN (never a public default).
#define SENTINEL_RF_NTP_SERVER ""
