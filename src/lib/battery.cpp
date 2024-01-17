#include "battery.h"

/**
 * Calculate the battery status (power level)
 */
int calculateBatteryStatus(Preferences *preferences) {
  int bat = 0;
  for (uint8_t i = 0; i < 25; i++) {
    bat += analogRead(BAT_ADC);
  }
  bat /= 25;
  float volt = (bat * 3.3 / 4096);

  int level = constrain(map(volt * 1000, 1630, 1850, 0, 100), 0, 100);

  // Save the battery level to the preferences
  preferences->putInt("battery_level", level);

  return level;
}