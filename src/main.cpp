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
#include "home.h"
#include "lib/battery.h"
#include "lib/log.h"
#include "os_config.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "time.h"
#include "wakeup.h"

using namespace ace_button;

GxIO_Class io(SPI, /*CS*/ EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RESET);
GxEPD_Class display(io, /*RST=*/EPD_RESET, /*BUSY=*/EPD_BUSY);

// ESP32Time rtc;
// ESP32Time rtc(GMT_OFFSET_SEC); // offset in seconds GMT+1
ESP32Time rtc(0); // offset in seconds GMT+1

Preferences preferences;

RTC_DATA_ATTR WakeupFlag wakeup = WakeupFlag::WAKEUP_INIT;
RTC_DATA_ATTR uint32_t wakeupCount = 0;

// We can use this to switch states via the button
// This is used to switch between the apps menu and the home screen for example
AwakeState awakeState = AwakeState::APPS_MENU;

uint32_t sleepTimer = 0;

AceButton button(PIN_KEY);
void buttonUpdateTask(void *pvParameters);
void focusTimerTask(void *pvParameters);
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
  log(LogLevel::INFO, "Welcome to Lilygo Paper Watch!");
  log(LogLevel::SUCCESS, "Serial communication initiliazed");

  SPI.begin(SPI_SCK, -1, SPI_DIN, EPD_CS);

  pinMode(PWR_EN, OUTPUT);
  digitalWrite(PWR_EN, HIGH);
  pinMode(PIN_KEY, INPUT_PULLUP);

  ButtonConfig *buttonConfig = button.getButtonConfig();
  buttonConfig->setEventHandler(handleButtonEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
  //  buttonConfig->setClickDelay(200);
  // buttonConfig->setDebounceDelay(10);
  buttonConfig->setLongPressDelay(1000);

  pinMode(BAT_ADC, ANALOG);
  adcAttachPin(BAT_ADC);
  analogReadResolution(12);
  analogSetWidth(50);

  // The button has been pressed to wake up the device
  if (digitalRead(PIN_KEY) == 0) {
    // Set the status - wake me up from deep sleep and get the buttons working again
    wakeup = WakeupFlag::WAKEUP_DEEP_SLEEP;
    log(LogLevel::INFO, "Button pressed, starting wakeup process...");
  }
  log(LogLevel::INFO, "Starting wakeup process...");

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

  //  We can manually set the time, but not a good idea gets called with every cycle
  // rtc.setTime(00, 40, 21, 13, 1, 2024); // 17th Jan 2021 15:24:30

  configTime(GMT_OFFSET_SEC, DAY_LIGHT_OFFSET_SEC, nullptr);
  log(LogLevel::SUCCESS, "Time configured");

  display.init();
  display.setRotation(1);
  log(LogLevel::SUCCESS, "Display initiliazed");

  switch (wakeup) {
    // Power on the device and initialize the display, pull time via WiFi
  case WakeupFlag::WAKEUP_INIT:
    wakeupInit(&wakeup, &wakeupCount, &display, &rtc, &preferences);
    break;

    // Refresh the time and sleep
  case WakeupFlag::WAKEUP_LIGHT:
    wakeupLight(&wakeup, &wakeupCount, &display, &rtc, &preferences);
    // xTaskCreate(buttonUpdateTask, "ButtonUpdateTask", 10000, NULL, 1, NULL);
    // Listen for when the focus timer ends and set off the alarm
    break;

    // Wake the device from deep sleep when the button is pressed
  case WakeupFlag::WAKEUP_DEEP_SLEEP:
    wakeupDeepSleep(&wakeup, &wakeupCount, &display, &rtc, &preferences);
    break;
  }

  // Listen for action on the user button (pin 35)
  xTaskCreate(buttonUpdateTask, "ButtonUpdateTask", 10000, NULL, 1, NULL);
  // Listen for when the focus timer ends and set off the alarm
  //  xTaskCreate(focusTimerTask, "FocusTimerTask", 20000, NULL, 10, NULL);

  log(LogLevel::SUCCESS, "Wakeup process completed");
}

// Event loop
void loop() {
  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE)
    sleepTimer++;

  switch (wakeup) {

    // Power on the device and initialize the display, pull time via WiFi
  case WakeupFlag::WAKEUP_INIT:
    wakeupInitLoop(&wakeup, sleepTimer, &display, &rtc);
    break;

    // Update the time and sleep
  case WakeupFlag::WAKEUP_LIGHT:
    wakeupLightLoop(&wakeup, sleepTimer, &display, &rtc);
    break;

    // The ESP is asleep, perform the wakeup process and do not sleep again
  case WakeupFlag::WAKEUP_DEEP_SLEEP:
    wakeupDeepSleepLoop(&wakeup, sleepTimer, &display, &rtc, awakeState);
    break;
  }
}

void buttonUpdateTask(void *pvParameters) {
  while (1) {
    button.check();
    vTaskDelay(10);
  }
  Serial.println("Ending task 1");
  vTaskDelete(NULL);
}

void focusTimerTask(void *pvParameters) {
  while (1) {
    Serial.println("Focus Timer Task");
    vTaskDelay(1000);
  }
}

/**
 * Start, Reset or Stop the focus timer
 * This is actually little tricky because when the device is in deep sleep we need the button
 * to wake up the device.
 * This means that these events will not be triggered after the device is in deep sleep.
 * The first click will wake up the device and the second click will trigger the event
 */
void handleButtonEvent(AceButton *button, uint8_t eventType, uint8_t buttonState) {
  // sleepTimer = 0;

  // Print out a message for all events.
  Serial.print(F("handleEvent(): eventType: "));
  Serial.print(eventType);
  Serial.print(F("; buttonState: "));
  Serial.println(buttonState);

  switch (eventType) {
  case AceButton::kEventClicked: {
    Serial.println("Clicked");
    // Let us start focus time if not already running. Get the focus time from the preferences
    int time = preferences.getInt("focus_time", 0);
    if (time <= 0) {
      // Start the focus time to 25 minutes
      preferences.putInt("focus_time", 25);
    }
    break;
  }

    // Double click to reset the focus timer and start again from 25 minutes
  case AceButton::kEventDoubleClicked:
    Serial.println("Double Clicked");
    // Reset the focus timer
    preferences.putInt("focus_time", 25);
    wakeupDeepSleep(&wakeup, &wakeupCount, &display, &rtc, &preferences);
    break;

    // Long press to turn off the focus timer
  case AceButton::kEventLongPressed:
    Serial.println("Long Pressed");
    // Turn off the focus timer
    preferences.putInt("focus_time", -1);
    wakeupDeepSleep(&wakeup, &wakeupCount, &display, &rtc, &preferences);
    break;
  }
}