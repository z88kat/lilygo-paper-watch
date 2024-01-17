#include "app_time_config.h"

void AppTimeConfig::drawUI(GxEPD_Class *display) {
  display->fillScreen(GxEPD_WHITE);
  display->setTextColor(GxEPD_BLACK);
  display->setTextWrap(false);

  display->drawBitmap(50, 35, qpaperos_logo_100, 100, 100, GxEPD_BLACK);
  display->setFont(&Outfit_60011pt7b);
  printCenterString(display, "Time Config", 100, 170);

  display->updateWindow(0, 0, GxEPD_WIDTH, GxEPD_HEIGHT);
}

std::unique_ptr<AppTimeConfig> appTimeConfig(new AppTimeConfig("Time Settings", icon_app_about));