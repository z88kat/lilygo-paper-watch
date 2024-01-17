#include "wakeup.h"

// Setup

// When the watch is first powered on, we need to initialize the time
// We use the old existing time which is stored in the preferences
// Then we atttempt to fetch the time from the server, so on the 2nd cycle the time will be updated
//
void wakeupInit(WakeupFlag *wakeupType, unsigned int *wakeupCount, GxEPD_Class *display, ESP32Time *rtc, Preferences *preferences) {
  log(LogLevel::INFO, "WAKEUP_INIT");

  rtc->setTime(preferences->getLong64("prev_time_unix", 0) + 15);

  // display->fillScreen(GxEPD_WHITE);
  //   display->update();
  //   delay(1000);
  // Get the battery status from the preferences
  int batteryStatus = preferences->getInt("battery_level", 0);
  drawHomeUI(display, rtc, batteryStatus);
  // Get the weather from the preferences and display it
  displayWeather(display, preferences->getString("weather_c"), preferences->getString("weather_t"));

  // Re-draw the display
  display->update();

  // Update the time
  performWiFiActions(display, preferences);
}

void wakeupLight(WakeupFlag *wakeupType, unsigned int *wakeupCount, GxEPD_Class *display, ESP32Time *rtc, Preferences *preferences) {
  log(LogLevel::INFO, "WAKEUP_LIGHT");
  setCpuFrequencyMhz(80);

  // Get the battery status from the preferences
  int batteryStatus = preferences->getInt("battery_level", 0);

  // Draw the time and date
  drawHomeUI(display, rtc, batteryStatus);

  // Update the battery status every 10 minutes, its enough, save on battery
  if (*wakeupCount % 10 == 0) {
    calculateBatteryStatus(preferences);
  }

  // Get the weather from the preferences and display it
  displayWeather(display, preferences->getString("weather_c"), preferences->getString("weather_t"));

  // Refresh the display
  display->update();
  // Power it down to save battery
  display->powerDown();

  preferences->putLong64("prev_time_unix", rtc->getEpoch());

  // Increase the wakeup count
  wakeupCount++;

  // Perform the WiFi actions every 4 hours
  if (*wakeupCount % 240 == 0) {
    performWiFiActions(display, preferences);
  }

  // snooze....
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
    drawAppsListUI(display, rtc, -1);
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
 * Perform the WiFi actions such as connecting to the network and getting the time
 * This strange configuration was the only way I managed to get the WiFi working on this device
 * don't mess with it! :-)
 */
void performWiFiActions(GxEPD_Class *display, Preferences *preferences) {

  String wifi_ssid = preferences->getString("wifi_ssid", "");
  String wifi_password = preferences->getString("wifi_passwd", "");

  // Turn on the wifi
  WiFi.mode(WIFI_STA);

  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("no networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found");

    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.printf("%2d", i + 1);
      Serial.print(" | ");
      Serial.printf("%-32.32s", WiFi.SSID(i).c_str());
      Serial.print(" | ");
      Serial.printf("%4d", WiFi.RSSI(i));
      Serial.print(" | ");
      Serial.printf("%2d", WiFi.channel(i));
      Serial.print(" | ");
      switch (WiFi.encryptionType(i)) {
      case WIFI_AUTH_OPEN:
        Serial.print("open");
        break;
      case WIFI_AUTH_WEP:
        Serial.print("WEP");
        break;
      case WIFI_AUTH_WPA_PSK:
        Serial.print("WPA");
        break;
      case WIFI_AUTH_WPA2_PSK:
        Serial.print("WPA2");
        break;
      case WIFI_AUTH_WPA_WPA2_PSK:
        Serial.print("WPA+WPA2");
        break;
      case WIFI_AUTH_WPA2_ENTERPRISE:
        Serial.print("WPA2-EAP");
        break;
      case WIFI_AUTH_WPA3_PSK:
        Serial.print("WPA3");
        break;
      case WIFI_AUTH_WPA2_WPA3_PSK:
        Serial.print("WPA2+WPA3");
        break;
      case WIFI_AUTH_WAPI_PSK:
        Serial.print("WAPI");
        break;
      default:
        Serial.print("unknown");
      }
      Serial.println();
      delay(10);
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setTxPower(WIFI_POWER_2dBm); // REQUIRED otherwise WiFi does not work!
  WiFi.hostname("LilyPaperWatch");
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  // Wait for connection
  /*
  Code Value Meaning
  WL_IDLE_STATUS 0 WiFi is in process of changing between statuses
  WL_NO_SSID_AVAIL 1 SSID cannot be reached
  WL_SCAN_COMPLETED 2
  WL_CONNECTED 3 Successful connection is established
  WL_CONNECT_FAILED 4 Password is incorrect
  WL_CONNECTION_LOST 5
  WL_DISCONNECTED 6 Module is not configured in station mode

  You should normally get a couple of seconds of "6" followed by a single "3".
  */
  uint8_t i = 0;
  // while (WiFi.localIP().toString() == "0.0.0.0" && i++ < 60) {
  while (WiFi.status() != WL_CONNECTED && i++ < 60) { // Wait for the WiFI connection completion
    delay(500);
    Serial.print(".");
    Serial.print(WiFi.status());
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

    Serial.print("# IP address: ");
    Serial.println(WiFi.localIP());

    // Disconnect WiFi as it's no longer needed, saves lots of power
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}