#include "AceButton.h"
#include "Adafruit_I2CDevice.h"
#include "ESP32Time.h"
#include "GxDEPG0150BN/GxDEPG0150BN.h" // 1.54" b/w 200x200
#include "GxEPD.h"
#include "GxIO/GxIO.h"
#include "GxIO/GxIO_SPI/GxIO_SPI.h"
#include "HTTPClient.h"
#include "Preferences.h"
#include "WiFi.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "time.h"

#include "apps.h"
#include "home.h"
#include "lib/battery.h"
#include "lib/log.h"
#include "os_config.h"
#include "wakeup.h"

using namespace ace_button;

GxIO_Class io(SPI, /*CS*/ EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RESET);
GxEPD_Class display(io, /*RST=*/EPD_RESET, /*BUSY=*/EPD_BUSY);

// ESP32Time rtc;
// ESP32Time rtc(GMT_OFFSET_SEC); // offset in seconds GMT+1
ESP32Time rtc(0); // offset in seconds GMT+1
// Set boolean to false that time has not yet been initialized
bool timeInitialized = false;

Preferences preferences;

RTC_DATA_ATTR WakeupFlag wakeup = WakeupFlag::WAKEUP_INIT;
RTC_DATA_ATTR uint32_t wakeupCount = 0;

AwakeState awakeState = AwakeState::APPS_MENU;

uint32_t sleepTimer = 0;

AceButton button(PIN_KEY);
void buttonUpdateTask(void *pvParameters);
void handleButtonEvent(AceButton *button, uint8_t eventType, uint8_t buttonState);

hw_timer_t *uiTimer = NULL;
volatile SemaphoreHandle_t timerSemaphore;

void ARDUINO_ISR_ATTR onTimer() { xSemaphoreGiveFromISR(timerSemaphore, NULL); }

// Asynchronous event handler when WiFi is connected
void WiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  log(LogLevel::INFO, "WiFi connected, attempting to sync time with ntp server");
  // configTime(GMT_OFFSET_SEC, DAY_LIGHT_OFFSET_SEC, NTP_SERVER1);
  log(LogLevel::INFO, "Time synchronized from WiFi");
}

void setup() {
  Serial.begin(115200);
  delay(10);
  log(LogLevel::INFO, "Welcome to qPaperOS!");
  log(LogLevel::SUCCESS, "Serial communication initiliazed");

  SPI.begin(SPI_SCK, -1, SPI_DIN, EPD_CS);

  pinMode(PWR_EN, OUTPUT);
  // TODO:  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PWR_EN, HIGH);
  // TODO:  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_KEY, INPUT_PULLUP);
  ButtonConfig *buttonConfig = button.getButtonConfig();
  buttonConfig->setEventHandler(handleButtonEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
  buttonConfig->setClickDelay(200);
  buttonConfig->setDebounceDelay(10);
  buttonConfig->setLongPressDelay(1000);

  pinMode(BAT_ADC, ANALOG);
  adcAttachPin(BAT_ADC);
  analogReadResolution(12);
  analogSetWidth(50);

// Lets turn on the backlight
// Does not work, appears there is no hardware backlight, we need to attach our own
#ifdef BACKLIGHT
  //  pinMode(BACKLIGHT, OUTPUT);
  //  digitalWrite(BACKLIGHT, HIGH);
  log(LogLevel::SUCCESS, "Hardware BACKLIGHT initiliazed");
#endif

  log(LogLevel::SUCCESS, "Hardware pins initiliazed");

  preferences.begin(PREFS_KEY);

  // Check if the WIFI_SSID and WIFI_PASSWD are set and save them to the preferences
  if (strlen(WIFI_SSID) > 0 && strlen(WIFI_PASSWD) > 0) {
    preferences.putString("wifi_ssid", WIFI_SSID);
    preferences.putString("wifi_passwd", WIFI_PASSWD);
    log(LogLevel::SUCCESS, "Wifi Settings Saved");
  }

  // Save the weather api key to the preferences
  if (strlen(WEATHER_API_KEY) > 0) {
    preferences.putString("weather_api_key", WEATHER_API_KEY);
    log(LogLevel::SUCCESS, "Weather API Key Saved");
  }

  // Reset the weather condition and temp parameters
  preferences.putString("weather_c", "Unknown");
  preferences.putString("weather_t", "0.0");

  // Save the current location as defined in the os_config.h
  if (strlen(WEATHER_LOCATION) > 0) {
    preferences.putString("location", WEATHER_LOCATION);
  }

  // When the wifi is connected, pull the time from the ntp server
  WiFi.onEvent(WiFiConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);

  timerSemaphore = xSemaphoreCreateBinary();
  uiTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(uiTimer, &onTimer, true);
  timerAlarmWrite(uiTimer, 1000000, true);
  timerAlarmEnable(uiTimer);
  log(LogLevel::SUCCESS, "Hardware timer initiliazed");

  log(LogLevel::SUCCESS, "Preferences initiliazed");

  //  Make sure we only do this once, otherwise we'll get a time reset every time we wake up
  if (!timeInitialized) {
    log(LogLevel::INFO, "Manual time set");
    // rtc.setTime(00, 40, 21, 13, 1, 2024); // 17th Jan 2021 15:24:30
    timeInitialized = true;
  }
  configTime(GMT_OFFSET_SEC, DAY_LIGHT_OFFSET_SEC, nullptr);
  log(LogLevel::SUCCESS, "Time configured");

  display.init();
  display.setRotation(1);
  log(LogLevel::SUCCESS, "Display initiliazed");

  if (digitalRead(PIN_KEY) == 0)
    wakeup = WakeupFlag::WAKEUP_FULL;
  log(LogLevel::INFO, "Starting wakeup process...");

  switch (wakeup) {
  case WakeupFlag::WAKEUP_INIT:
    wakeupInit(&wakeup, &wakeupCount, &display, &rtc, &preferences);
    break;

  case WakeupFlag::WAKEUP_LIGHT:
    wakeupLight(&wakeup, &wakeupCount, &display, &rtc, &preferences);
    break;

  case WakeupFlag::WAKEUP_FULL:
    xTaskCreate(buttonUpdateTask, "ButtonUpdateTask", 10000, NULL, 1, NULL);
    wakeupFull(&wakeup, &wakeupCount, &display, &rtc, &preferences);
    break;
  }

  log(LogLevel::SUCCESS, "Wakeup process completed");
}

void loop() {
  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE && awakeState != AwakeState::IN_APP)
    sleepTimer++;

  switch (wakeup) {
  case WakeupFlag::WAKEUP_INIT:
    wakeupInitLoop(&wakeup, sleepTimer, &display, &rtc);
    break;

  case WakeupFlag::WAKEUP_LIGHT:
    wakeupLightLoop(&wakeup, sleepTimer, &display, &rtc);
    break;

  case WakeupFlag::WAKEUP_FULL:
    wakeupFullLoop(&wakeup, sleepTimer, &display, &rtc, awakeState);
    break;
  }
}

void buttonUpdateTask(void *pvParameters) {
  while (1) {
    button.check();
    // vTaskDelay(5);
  }
  vTaskDelete(NULL);
}

void handleButtonEvent(AceButton *button, uint8_t eventType, uint8_t buttonState) {
  sleepTimer = 0;

  switch (eventType) {
  case AceButton::kEventClicked:
    if (awakeState == AwakeState::APPS_MENU) {
      currentAppIndex++;
      if (currentAppIndex >= apps.size())
        currentAppIndex = 0;
    } else
      apps[currentAppIndex]->buttonClick();
    break;

  case AceButton::kEventDoubleClicked:
    if (awakeState == AwakeState::IN_APP)
      apps[currentAppIndex]->buttonDoubleClick();
    break;

  case AceButton::kEventLongPressed:
    if (awakeState == AwakeState::APPS_MENU) {
      awakeState = AwakeState::IN_APP;
      apps[currentAppIndex]->setup();
    } else {
      awakeState = AwakeState::APPS_MENU;
      apps[currentAppIndex]->exit();
    }
    break;
  }
}