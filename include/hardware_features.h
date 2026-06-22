#pragma once

// StackChan body:
// servo, 12 RGB LEDs, three-zone top touch, INA226 battery monitor.
#include <M5StackChan.h>

// CoreS3:
// camera, ambient/proximity sensor, display, touch, speaker, microphones,
// IMU, RTC, power management and internal/external I2C.
#include <M5CoreS3.h>

// StackChan body peripherals.
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <M5UnitUnifiedNFC.h>

// ESP32/CoreS3 built-ins.
#include <BLEDevice.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_camera.h>

namespace stackchan_hardware {

constexpr uint8_t kIrSendPin = 5;
constexpr uint8_t kIrReceivePin = 10;
constexpr uint8_t kBodyI2cSclPin = 11;
constexpr uint8_t kBodyI2cSdaPin = 12;
constexpr uint8_t kRgbLedCount = 12;

// M5StackChan.begin() initializes the body devices. The camera and LTR553
// remain opt-in because each reserves hardware resources when enabled:
//   CoreS3.Camera.begin();
//   CoreS3.Ltr553.begin(...);
//
// NFC and IR are also opt-in to avoid claiming their buses until an
// application feature needs them. Their official libraries are imported
// above and ready to use.

}  // namespace stackchan_hardware
