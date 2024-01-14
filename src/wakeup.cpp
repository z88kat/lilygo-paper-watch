#include "wakeup.h"

// Setup

void wakeupInit(WakeupFlag *wakeupType, unsigned int *wakeupCount, GxEPD_Class *display, ESP32Time *rtc, Preferences *preferences) {
  log(LogLevel::INFO, "WAKEUP_INIT");

  rtc->setTime(preferences->getLong64("prev_time_unix", 0) + 15);

  display->fillScreen(GxEPD_WHITE);
  display->update();
  delay(1000);
  drawHomeUI(display, rtc, calculateBatteryStatus());
  // Get the weather from the preferences and display it
  displayWeather(display, preferences->getString("weather_c"), preferences->getString("weather_t"));

  // Re-draw the display
  display->update();

  // Update the time
  performWiFiActions(display, preferences);

  // Re-draw the display
  display->update();
}

void wakeupLight(WakeupFlag *wakeupType, unsigned int *wakeupCount, GxEPD_Class *display, ESP32Time *rtc, Preferences *preferences) {
  log(LogLevel::INFO, "WAKEUP_LIGHT");
  setCpuFrequencyMhz(80);

  drawHomeUI(display, rtc, calculateBatteryStatus());

  // Get the weather from the preferences and display it
  displayWeather(display, preferences->getString("weather_c"), preferences->getString("weather_t"));

  display->update();

  preferences->putLong64("prev_time_unix", rtc->getEpoch());

  wakeupCount++;

  // Once every 24 hours connect the WiFi and sync the time with the NTP server
  //  if (*wakeupCount % 2880 == 0) {
  //  performWiFiActions(display, preferences);
  // }

  // Check if the time is 8:00, if so, update the weather
  if (rtc->getHour(true) == 8 && rtc->getMinute() == 0) {
    performWiFiActions(display, preferences);
    display->update();
  }

  display->powerDown();

  log(LogLevel::INFO, "Going to sleep...");
  digitalWrite(PWR_EN, LOW);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_KEY, 0);
  esp_sleep_enable_timer_wakeup(UPDATE_WAKEUP_TIMER_US);
  esp_deep_sleep_start();
}

void wakeupFull(WakeupFlag *wakeupType, unsigned int *wakeupCount, GxEPD_Class *display, ESP32Time *rtc, Preferences *preferences) {
  log(LogLevel::INFO, "WAKEUP_FULL");
  setCpuFrequencyMhz(240);

  wakeupCount = 0;

  initApps();
  log(LogLevel::SUCCESS, "Apps initialized");

  display->fillScreen(GxEPD_WHITE);
  display->updateWindow(0, 0, GxEPD_WIDTH, GxEPD_HEIGHT);
}

// Loop
void wakeupInitLoop(WakeupFlag *wakeupType, unsigned int sleepTimer, GxEPD_Class *display, ESP32Time *rtc) {
  if (sleepTimer == 30) {
    *wakeupType = WakeupFlag::WAKEUP_LIGHT;
    esp_sleep_enable_timer_wakeup(1000000);
    esp_deep_sleep_start();
  }
}

void wakeupLightLoop(WakeupFlag *wakeupType, unsigned int sleepTimer, GxEPD_Class *display, ESP32Time *rtc) {
  if (sleepTimer == 15) {
    digitalWrite(PWR_EN, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_KEY, 0);
    esp_sleep_enable_timer_wakeup(UPDATE_WAKEUP_TIMER_US - 15000000);
    esp_deep_sleep_start();
  }
}

void wakeupFullLoop(WakeupFlag *wakeupType, unsigned int sleepTimer, GxEPD_Class *display, ESP32Time *rtc, AwakeState awakeState) {
  if (awakeState == AwakeState::APPS_MENU) {
    drawAppsListUI(display, rtc, calculateBatteryStatus());
    display->updateWindow(0, 0, GxEPD_WIDTH, GxEPD_HEIGHT);
  } else {
    apps[currentAppIndex]->drawUI(display);
  }

  if (sleepTimer == 15) {
    *wakeupType = WakeupFlag::WAKEUP_LIGHT;
    esp_sleep_enable_timer_wakeup(1000000);
    esp_deep_sleep_start();
  }
}

/**
 * @brief Perform the WiFi actions such as connecting to the network and getting the time
 */
void performWiFiActions(GxEPD_Class *display, Preferences *preferences) {

  // Turn on the wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(preferences->getString("wifi_ssid"), preferences->getString("wifi_passwd"));

  // Wait for connection
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 60) { // Wait for the WiFI connection completion
    delay(500);
    Serial.print(".");
  }

  // The WiFi on this device fails all the time, it's completely random when it does or does not connect
  // I have a connection success rate of 1:20
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi");
    // disconnect WiFi as it's no longer needed
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    disableWifiDisplay(display);
    log(LogLevel::ERROR, "WiFi failed to connect");
  } else {
    log(LogLevel::SUCCESS, "WiFi initiliazed");
    // Indicate we are connected to WiFi
    enableWifiDisplay(display);
    // Get the time from the NTP server
    configTime(GMT_OFFSET_SEC, DAY_LIGHT_OFFSET_SEC, NTP_SERVER1);
    // Get the current weather
    getWeather(display, preferences);

    // Disconnect WiFi as it's no longer needed, saves lots of power
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  // Read the WIFI_SSID and WIFI_PASSWD from the preferences into string variables
  // String wifi_ssid = preferences->getString("wifi_ssid", "");
  // String wifi_passwd = preferences->getString("wifi_passwd", "");
  // log(LogLevel::INFO, wifi_passwd.c_str());

  // Print out the device id
  // Serial.println("Device ID: " + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX) + String((uint32_t)ESP.getEfuseMac(), HEX));

  // configTime(GMT_OFFSET_SEC, DAY_LIGHT_OFFSET_SEC, NTP_SERVER1);
}