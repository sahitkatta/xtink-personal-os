// Adapted from Idered/xteink-x4 (MIT License, Copyright (c) 2025 CidVonHighwind)
// https://github.com/Idered/xteink-x4
#pragma once

#include <Arduino.h>

class BatteryMonitor {
 public:
  explicit BatteryMonitor(uint8_t adcPin, float dividerMultiplier = 2.0f);

  uint16_t readPercentage() const;
  uint16_t readMillivolts() const;
  uint16_t readRawMillivolts() const;
  double readVolts() const;

  static uint16_t percentageFromMillivolts(uint16_t millivolts);

 private:
  uint8_t _adcPin;
  float _dividerMultiplier;
};
