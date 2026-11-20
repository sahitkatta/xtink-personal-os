#pragma once

// XTEINK X4 (ESP32-C3) pin mapping and button calibration.
// Verified against Idered/xteink-x4 (MIT licensed) - the only reference
// found with a working SD card init and empirically calibrated button
// thresholds for this exact board. Our own first attempt had MISO wrong
// (guessed 9, really 7), which is why the SD card wasn't mounting.

// Shared SPI bus: e-ink panel (4.26" 800x480, SSD1677 / GDEQ0426T82) and the
// microSD card both hang off it, each with their own CS. The display itself
// never reads (write-only panel), but the bus still carries a real MISO line
// for the SD card.
constexpr int8_t SHARED_SCLK = 8;
constexpr int8_t SHARED_MOSI = 10;
constexpr int8_t SHARED_MISO = 7;
constexpr uint32_t SHARED_SPI_HZ = 40000000;

constexpr int8_t EPD_CS   = 21;
constexpr int8_t EPD_DC   = 4;
constexpr int8_t EPD_RST  = 5;
constexpr int8_t EPD_BUSY = 6;

constexpr int8_t SD_CS = 12;

// Buttons. GPIO1/GPIO2 are ADC resistor ladders (multiple buttons sharing
// one analog pin); GPIO3 is a plain digital active-low power button.
constexpr int8_t BTN_LADDER_4 = 1; // Right/Left/Confirm/Back
constexpr int8_t BTN_LADDER_2 = 2; // Volume Down/Volume Up
constexpr int8_t BTN_POWER    = 3; // digital, INPUT_PULLUP, active LOW

// Ladder thresholds (analogRead on a 12-bit ADC) + shared tolerance.
// RIGHT/LEFT/CONFIRM/VOLUME_UP/VOLUME_DOWN match the Idered/xteink-x4
// reference values closely. BACK does not (reference: 3470) - measured on
// this specific unit at ~3580-3592 via the on-device button_log.csv
// logger (see src/main.cpp's logButtonState()); component tolerance
// variance between units, most likely.
constexpr int BTN_THRESHOLD        = 100;
constexpr int BTN_RIGHT_VAL        = 3;
constexpr int BTN_LEFT_VAL         = 1470;
constexpr int BTN_CONFIRM_VAL      = 2655;
constexpr int BTN_BACK_VAL         = 3590;
constexpr int BTN_VOLUME_DOWN_VAL  = 3;
constexpr int BTN_VOLUME_UP_VAL    = 2205;

// Battery (ADC) + USB/charging detection.
constexpr int8_t BAT_ADC_PIN  = 0;  // GPIO0, through a voltage divider
constexpr int8_t USB_SENSE_PIN = 20; // U0RXD; reads HIGH when USB is connected
