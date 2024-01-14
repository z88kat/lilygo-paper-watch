#pragma once

#include "ESP32Time.h"
#include "GxEPD.h"
#include "Preferences.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"

#include "home.h"
#include "lib/log.h"
#include "os_config.h"

void getWeather(GxEPD_Class *display, Preferences *preferences);
