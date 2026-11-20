// Adapted from Idered/xteink-x4 (MIT License, Copyright (c) 2025 CidVonHighwind)
// https://github.com/Idered/xteink-x4
//
// Deviates from upstream here: upstream calibrates analogRead() manually via
// esp_adc_cal, but that's the legacy ADC driver and aborts ("ADC: CONFLICT!
// driver_ng is not allowed to be used with the legacy driver") on this
// Arduino-ESP32 core (3.x / IDF 5.x) because analogRead() elsewhere in this
// firmware already goes through the new driver. analogReadMilliVolts()
// does the same calibration internally through the new driver, so it's a
// drop-in fix with no legacy include needed.
#include "BatteryMonitor.h"

BatteryMonitor::BatteryMonitor(uint8_t adcPin, float dividerMultiplier)
    : _adcPin(adcPin), _dividerMultiplier(dividerMultiplier) {}

uint16_t BatteryMonitor::readPercentage() const {
  return percentageFromMillivolts(readMillivolts());
}

uint16_t BatteryMonitor::readMillivolts() const {
  return static_cast<uint16_t>(readRawMillivolts() * _dividerMultiplier);
}

uint16_t BatteryMonitor::readRawMillivolts() const {
  return analogReadMilliVolts(_adcPin);
}

double BatteryMonitor::readVolts() const {
  return static_cast<double>(readMillivolts()) / 1000.0;
}

uint16_t BatteryMonitor::percentageFromMillivolts(uint16_t millivolts) {
  double volts = millivolts / 1000.0;
  // Polynomial fit derived from LiPo discharge-curve samples (upstream project).
  double y = -144.9390 * volts * volts * volts +
             1655.8629 * volts * volts -
             6158.8520 * volts +
             7501.3202;

  y = max(y, 0.0);
  y = min(y, 100.0);
  y = round(y);
  return static_cast<int>(y);
}
