#pragma once
// Copy this file to secrets.h and fill in real values.
// secrets.h is git-ignored — NEVER commit your device secret.

// ── WiFi backhaul ────────────────────────────────────────────────
// The ESP32-C3 is WiFi-only. The allotment has no mains WiFi, so this
// is whatever backhaul you provide on a visit or at the plot: a phone
// hotspot, a 4G MiFi, or home WiFi if you bring the hub in. Readings
// are timestamped when measured and buffered until a connection
// appears, so intermittent coverage is fine.
#define WIFI_SSID      "your-ssid"
#define WIFI_PASSWORD  "your-wifi-password"

// ── Yarplot device identity ──────────────────────────────────────
// Printed once by scripts/register-device.mjs when Phil registers the
// device. YARPLOT_HOST has no scheme and no trailing slash.
#define YARPLOT_HOST   "yarplot19.co.uk"
#define DEVICE_ID      "hub-1"   // must match --id used at registration

// The 64-character secret from registration. It is never transmitted —
// only the HMAC of each request is. Keep it off version control.
#define DEVICE_SECRET  "0000000000000000000000000000000000000000000000000000000000000000"
