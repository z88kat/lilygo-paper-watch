#pragma once

#include "ESP32Time.h"
#include "GxDEPG0150BN/GxDEPG0150BN.h" // 1.54 inches b/w 200x200
#include "GxEPD.h"
#include "Preferences.h"
#include "WiFi.h"
#include "home.h"
#include "lib/battery.h"
#include "lib/log.h"
#include "os_config.h"
#include "weather.h"

enum class WakeupFlag { WAKEUP_INIT, WAKEUP_LIGHT, WAKEUP_DEEP_SLEEP };
enum class AwakeState { APPS_MENU};

void playAlarm();

void wakeupInit(WakeupFlag *wakeupType, unsigned int *wakeupCount, GxEPD_Class *display, ESP32Time *rtc, Preferences *preferences);
void wakeupLight(WakeupFlag *wakeupType, unsigned int *wakeupCount, GxEPD_Class *display, ESP32Time *rtc, Preferences *preferences);
void wakeupDeepSleep(WakeupFlag *wakeupType, unsigned int *wakeupCount, GxEPD_Class *display, ESP32Time *rtc, Preferences *preferences);

void wakeupInitLoop(WakeupFlag *wakeupType, unsigned int sleepTimer, GxEPD_Class *display, ESP32Time *rtc);
void wakeupLightLoop(WakeupFlag *wakeupType, unsigned int sleepTimer, GxEPD_Class *display, ESP32Time *rtc);
void wakeupDeepSleepLoop(WakeupFlag *wakeupType, unsigned int sleepTimer, GxEPD_Class *display, ESP32Time *rtc, AwakeState awakeState);

void performWiFiActions(GxEPD_Class *display, Preferences *preferences);
