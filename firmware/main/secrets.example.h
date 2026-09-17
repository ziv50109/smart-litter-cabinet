#pragma once

// Copy to secrets.h locally. Never commit deployment credentials or compiled images.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define DEVICE_TOKEN ""
#define APP_SCRIPT_URL ""
#define DEBUG_WEB_SERVER false

// Independent management credentials. Empty/short passwords disable the web service.
#define WEB_ADMIN_USER "admin"
#define WEB_ADMIN_PASSWORD ""

// Separate from debugging. Offline remains the default; connected mode requires PM SDK support.
#ifndef LITTER_CONNECTED_STANDBY
#define LITTER_CONNECTED_STANDBY 0
#endif

#define CAT_1_CHIP_RAW ""
#define CAT_1_NAME "unknown"
#define CAT_2_CHIP_RAW ""
#define CAT_2_NAME "unknown"
