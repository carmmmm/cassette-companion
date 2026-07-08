#pragma once

// Copy this file to secrets.h and fill in your own values.
// secrets.h is gitignored — never commit your real credentials.

struct WifiCred {
  const char* ssid;
  const char* pass;
};

static const WifiCred WIFI_LIST[] = {
  {"YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD"}
};

static const int WIFI_COUNT = sizeof(WIFI_LIST) / sizeof(WIFI_LIST[0]);

static const char* SPOTIFY_CLIENT_ID     = "YOUR_CLIENT_ID";
static const char* SPOTIFY_CLIENT_SECRET = "YOUR_CLIENT_SECRET";
static const char* SPOTIFY_REFRESH_TOKEN = "YOUR_REFRESH_TOKEN";
