#include "CalibrationController.h"

#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>

#include "AvatarFaceController.h"
#include "hardware_features.h"

namespace {

constexpr int kYawMin = -1280;
constexpr int kYawMax = 1280;
constexpr int kPitchMin = 0;
constexpr int kPitchMax = 900;
constexpr int kSweepSpeed = 120;
constexpr int kPositionTolerance = 80;
constexpr uint32_t kMoveTimeoutMs = 30000;
constexpr uint32_t kPositionSettleMs = 300;
constexpr uint32_t kMinMoveWaitMs = 1200;
constexpr uint32_t kPositionStableMs = 800;
constexpr uint32_t kAssumeTargetAfterStoppedMs = 10000;
constexpr int kStablePositionDelta = 10;
constexpr int kMinimumAcceptedTravel = 80;
constexpr uint32_t kImuCalibrationMs = 8000;
constexpr size_t kImuSampleCount = 300;

enum class PromptButton {
  None,
  Cancel,
  Confirm,
};

M5Canvas& calibrationCanvas() {
  static M5Canvas canvas(&M5.Display);
  static int16_t canvasWidth = 0;
  static int16_t canvasHeight = 0;

  const int16_t width = M5.Display.width();
  const int16_t height = M5.Display.height();
  if (canvasWidth != width || canvasHeight != height) {
    canvas.deleteSprite();
    canvas.setColorDepth(16);
    canvas.createSprite(width, height);
    canvasWidth = width;
    canvasHeight = height;
  }
  return canvas;
}

PromptButton promptButtonAt(int16_t x, int16_t y) {
  if (y < 172 || y > 230) {
    return PromptButton::None;
  }
  return x < M5.Display.width() / 2
             ? PromptButton::Cancel
             : PromptButton::Confirm;
}

void drawPrompt(
    const char* title, const char* line1, const char* line2,
    const char* confirmLabel, PromptButton pressed,
    const char* footer = nullptr) {
  auto& display = calibrationCanvas();
  const int16_t width = display.width();
  constexpr int16_t margin = 12;
  constexpr int16_t gap = 8;
  constexpr int16_t buttonY = 172;
  constexpr int16_t buttonH = 58;
  const int16_t buttonW = (width - margin * 2 - gap) / 2;
  const int16_t confirmX = margin + buttonW + gap;

  const uint16_t cancelColor =
      pressed == PromptButton::Cancel ? 0x7800 : 0x4208;
  const uint16_t confirmColor =
      pressed == PromptButton::Confirm ? 0x03E0 : 0x0260;

  display.fillScreen(TFT_BLACK);
  display.setFont(&fonts::Font2);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(title, width / 2, 27);

  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString(line1, width / 2, 73);
  display.drawString(line2, width / 2, 101);

  if (footer != nullptr) {
    display.setTextColor(TFT_YELLOW, TFT_BLACK);
    display.drawString(footer, width / 2, 137);
  }

  display.fillRoundRect(
      margin, buttonY, buttonW, buttonH, 9, cancelColor);
  display.drawRoundRect(
      margin, buttonY, buttonW, buttonH, 9, TFT_WHITE);
  display.fillRoundRect(
      confirmX, buttonY, buttonW, buttonH, 9, confirmColor);
  display.drawRoundRect(
      confirmX, buttonY, buttonW, buttonH, 9, TFT_WHITE);

  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.drawString("CANCEL", margin + buttonW / 2, buttonY + 29);
  display.drawString(
      confirmLabel, confirmX + buttonW / 2, buttonY + 29);
  display.pushSprite(0, 0);
}

void waitForTouchRelease() {
  int16_t x = 0;
  int16_t y = 0;
  while (M5.Display.getTouch(&x, &y)) {
    M5StackChan.update();
    delay(10);
  }
}

bool waitForConfirmation(
    const char* title, const char* line1, const char* line2,
    const char* confirmLabel, const char* footer = nullptr) {
  waitForTouchRelease();
  PromptButton pressed = PromptButton::None;
  bool wasTouching = false;
  drawPrompt(
      title, line1, line2, confirmLabel, PromptButton::None, footer);

  while (true) {
    M5StackChan.update();
    int16_t x = 0;
    int16_t y = 0;
    const bool touching = M5.Display.getTouch(&x, &y);
    const PromptButton current =
        touching ? promptButtonAt(x, y) : PromptButton::None;

    if (touching && current != pressed) {
      pressed = current;
      drawPrompt(title, line1, line2, confirmLabel, pressed, footer);
    }

    if (!touching && wasTouching) {
      const PromptButton selected = pressed;
      if (selected == PromptButton::Cancel) {
        return false;
      }
      if (selected == PromptButton::Confirm) {
        return true;
      }
      pressed = PromptButton::None;
      drawPrompt(
          title, line1, line2, confirmLabel, pressed, footer);
    }

    wasTouching = touching;
    delay(10);
  }
}

bool waitForHomeCapture() {
  waitForTouchRelease();
  PromptButton pressed = PromptButton::None;
  bool wasTouching = false;
  uint32_t lastDrawAt = 0;

  while (true) {
    M5StackChan.update();
    const uint32_t now = millis();

    int16_t x = 0;
    int16_t y = 0;
    const bool touching = M5.Display.getTouch(&x, &y);
    const PromptButton current =
        touching ? promptButtonAt(x, y) : PromptButton::None;

    if (touching && current != pressed) {
      pressed = current;
    }
    if (now - lastDrawAt >= 120) {
      const auto angles = M5StackChan.Motion.getCurrentAngles();
      char position[64];
      snprintf(
          position, sizeof(position), "LIVE  Y:%+.1f  P:%+.1f",
          angles.x / 10.0f, angles.y / 10.0f);
      drawPrompt(
          "SERVO 1/7",
          "Move by hand: Yaw CENTER / Pitch HOME",
          position, "SET HOME", pressed,
          "Torque OFF - do not force the mechanism");
      lastDrawAt = now;
    }

    if (!touching && wasTouching) {
      if (pressed == PromptButton::Cancel) {
        return false;
      }
      if (pressed == PromptButton::Confirm) {
        return true;
      }
    }

    wasTouching = touching;
    delay(10);
  }
}

void drawMoving(
    const char* title, int targetYaw, int targetPitch,
    int currentYaw, int currentPitch) {
  auto& display = calibrationCanvas();
  char target[64];
  char current[64];
  snprintf(
      target, sizeof(target), "TARGET  Y:%+.1f  P:%+.1f",
      targetYaw / 10.0f, targetPitch / 10.0f);
  snprintf(
      current, sizeof(current), "NOW     Y:%+.1f  P:%+.1f",
      currentYaw / 10.0f, currentPitch / 10.0f);

  display.fillScreen(TFT_BLACK);
  display.setFont(&fonts::Font2);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(title, display.width() / 2, 30);
  display.setTextSize(1);
  display.drawString(target, display.width() / 2, 80);
  display.drawString(current, display.width() / 2, 112);
  display.setTextColor(TFT_RED, TFT_BLACK);
  display.drawString(
      "TOUCH SCREEN TO EMERGENCY STOP",
      display.width() / 2, 185);
  display.pushSprite(0, 0);
}

void drawImuProgress(
    uint32_t elapsedMs, float ax, float ay, float az) {
  auto& display = calibrationCanvas();
  char remaining[48];
  char accel[64];
  const uint32_t secondsLeft =
      (kImuCalibrationMs - min(elapsedMs, kImuCalibrationMs) + 999) /
      1000;
  snprintf(
      remaining, sizeof(remaining), "KEEP STILL: %lu sec",
      static_cast<unsigned long>(secondsLeft));
  snprintf(
      accel, sizeof(accel), "ACC  %.3f  %.3f  %.3f", ax, ay, az);

  display.fillScreen(TFT_BLACK);
  display.setFont(&fonts::Font2);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.drawString("IMU LEVEL", display.width() / 2, 30);
  display.setTextSize(1);
  display.setTextColor(TFT_YELLOW, TFT_BLACK);
  display.drawString(
      "Place the whole robot on a level surface",
      display.width() / 2, 72);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString(remaining, display.width() / 2, 118);
  display.drawString(accel, display.width() / 2, 148);
  display.setTextColor(TFT_RED, TFT_BLACK);
  display.drawString(
      "TOUCH SCREEN TO CANCEL", display.width() / 2, 205);
  display.pushSprite(0, 0);
}

bool screenTouched() {
  int16_t x = 0;
  int16_t y = 0;
  return M5.Display.getTouch(&x, &y);
}

bool nearTarget(int yaw, int pitch, int targetYaw, int targetPitch) {
  return abs(yaw - targetYaw) <= kPositionTolerance &&
         abs(pitch - targetPitch) <= kPositionTolerance;
}

}  // namespace

void CalibrationController::load() {
  Preferences preferences;
  preferences.begin("stack_cal", true);
  data_.servoValid = preferences.getBool("servo_valid", false);
  data_.yawMin = preferences.getInt("yaw_min", 0);
  data_.yawMax = preferences.getInt("yaw_max", 0);
  data_.pitchMin = preferences.getInt("pitch_min", 0);
  data_.pitchMax = preferences.getInt("pitch_max", 0);
  data_.imuLevelValid = preferences.getBool("imu_valid", false);
  data_.levelRollDeg = preferences.getFloat("roll_zero", 0.0f);
  data_.levelPitchDeg = preferences.getFloat("pitch_zero", 0.0f);
  preferences.end();
}

const CalibrationData& CalibrationController::data() const {
  return data_;
}

bool CalibrationController::run(AvatarFaceController& avatarFace) {
  avatarFace.pauseDrawing();
  delay(40);

  const bool servoOk = calibrateServos();
  bool imuOk = false;
  if (servoOk) {
    imuOk = calibrateImuLevel();
  }

  stopServos();
  avatarFace.resumeDrawing();
  avatarFace.resetToDefault();

  if (servoOk && imuOk) {
    avatarFace.showStatus("CALIBRATION OK", 2500);
    M5StackChan.showRgbColor(0, 48, 0);
    return true;
  }

  avatarFace.setExpression(m5avatar::Expression::Angry);
  avatarFace.showStatus("CALIBRATION STOP", 3000);
  avatarFace.returnToDefaultAfter(3000);
  M5StackChan.showRgbColor(96, 0, 0);
  return false;
}

bool CalibrationController::calibrateServos() {
  stopServos();
  M5StackChan.setServoPowerEnabled(true);
  delay(250);
  M5StackChan.Motion.setTorqueEnabled(false);

  if (!waitForHomeCapture()) {
    return false;
  }

  M5StackChan.Motion.setCurrentPostionAsHome();
  delay(150);
  const auto home = M5StackChan.Motion.getCurrentAngles();
  Serial.printf(
      "Servo home saved yaw=%d pitch=%d\n", home.x, home.y);

  if (!waitForConfirmation(
          "SERVO 2/7",
          "Automatic full-range verification",
          "Runs all endpoints, then HOME",
          "START",
          "Clear cables and hands from the head")) {
    return false;
  }

  M5StackChan.Motion.setAutoAngleSyncEnabled(true);
  M5StackChan.Motion.setAutoTorqueReleaseEnabled(false);
  M5StackChan.Motion.move(home.x, home.y, 1000);
  delay(100);
  M5StackChan.Motion.setTorqueEnabled(true);
  delay(100);
  waitForTouchRelease();

  int yawAtMin = 0;
  int yawAtMax = 0;
  int pitchAtMin = 0;
  int pitchAtMax = 0;
  int unused = 0;

  if (!moveAndVerify(
          "SERVO 3/7 YAW LEFT", kYawMin, kPitchMin,
          &yawAtMin, &unused, false)) {
    return false;
  }
  if (!moveAndVerify(
          "SERVO 4/7 YAW RIGHT", kYawMax, kPitchMin,
          &yawAtMax, &unused, false)) {
    return false;
  }
  if (!moveAndVerify(
          "SERVO 5/7 CENTER", 0, kPitchMin,
          &unused, &pitchAtMin, true)) {
    return false;
  }
  if (!moveAndVerify(
          "SERVO 6/7 PITCH UP", 0, kPitchMax,
          &unused, &pitchAtMax, false, true)) {
    return false;
  }
  if (!moveAndVerify(
          "SERVO 7/7 HOME", 0, 0,
          &unused, &pitchAtMin, true)) {
    return false;
  }

  stopServos();
  data_.servoValid = true;
  data_.yawMin = yawAtMin;
  data_.yawMax = yawAtMax;
  data_.pitchMin = pitchAtMin;
  data_.pitchMax = pitchAtMax;
  saveServoCalibration();

  Serial.printf(
      "Servo calibration yaw=[%d,%d] pitch=[%d,%d]\n",
      data_.yawMin, data_.yawMax,
      data_.pitchMin, data_.pitchMax);
  return true;
}

bool CalibrationController::moveAndVerify(
    const char* title, int yaw, int pitch, int* measuredYaw,
    int* measuredPitch, bool requireTarget,
    bool assumeTargetAfterStoppedDelay) {
  M5StackChan.Motion.move(yaw, pitch, kSweepSpeed);
  const uint32_t startedAt = millis();
  uint32_t lastDrawAt = 0;
  uint32_t reachedSince = 0;
  uint32_t stableSince = 0;
  bool assumedTarget = false;
  const auto startAngles = M5StackChan.Motion.getCurrentAngles();
  auto lastStableAngles = startAngles;
  const int plannedTravel =
      max(abs(startAngles.x - yaw), abs(startAngles.y - pitch));

  while (true) {
    M5StackChan.update();
    const uint32_t now = millis();
    const uint32_t elapsed = now - startedAt;
    const auto current = M5StackChan.Motion.getCurrentAngles();

    if (now - lastDrawAt >= 120) {
      drawMoving(title, yaw, pitch, current.x, current.y);
      lastDrawAt = now;
    }

    if (nearTarget(current.x, current.y, yaw, pitch)) {
      if (reachedSince == 0) {
        reachedSince = now;
      }
      if (now - reachedSince >= kPositionSettleMs) {
        break;
      }
    } else {
      reachedSince = 0;
    }

    if (abs(current.x - lastStableAngles.x) > kStablePositionDelta ||
        abs(current.y - lastStableAngles.y) > kStablePositionDelta) {
      lastStableAngles = current;
      stableSince = now;
    } else if (stableSince == 0) {
      stableSince = now;
    }

    const int actualTravel =
        max(abs(current.x - startAngles.x),
            abs(current.y - startAngles.y));
    const bool movedEnough =
        plannedTravel <= kMinimumAcceptedTravel ||
        actualTravel >= kMinimumAcceptedTravel;
    const bool stopped =
        !M5StackChan.Motion.isMoving() ||
        now - stableSince >= kPositionStableMs;

    if (!requireTarget && !assumeTargetAfterStoppedDelay &&
        elapsed >= kMinMoveWaitMs &&
        movedEnough && now - stableSince >= kPositionStableMs) {
      Serial.printf(
          "%s settled before target target=(%d,%d) actual=(%d,%d)\n",
          title, yaw, pitch, current.x, current.y);
      break;
    }

    if (assumeTargetAfterStoppedDelay &&
        elapsed >= kAssumeTargetAfterStoppedMs &&
        movedEnough && stopped) {
      Serial.printf(
          "%s stopped for target assume target=(%d,%d) actual=(%d,%d)\n",
          title, yaw, pitch, current.x, current.y);
      assumedTarget = true;
      break;
    }

    if (elapsed > 300 && screenTouched()) {
      Serial.printf("%s emergency stop\n", title);
      M5StackChan.Motion.stop();
      stopServos();
      waitForTouchRelease();
      return false;
    }

    if (elapsed >= kMoveTimeoutMs) {
      Serial.printf("%s timeout\n", title);
      M5StackChan.Motion.stop();
      stopServos();
      return false;
    }
    delay(20);
  }

  delay(150);
  const auto actual = M5StackChan.Motion.getCurrentAngles();
  *measuredYaw = assumedTarget ? yaw : actual.x;
  *measuredPitch = assumedTarget ? pitch : actual.y;
  Serial.printf(
      "%s target=(%d,%d) actual=(%d,%d) measured=(%d,%d)\n",
      title, yaw, pitch, actual.x, actual.y,
      *measuredYaw, *measuredPitch);

  if (requireTarget && !nearTarget(actual.x, actual.y, yaw, pitch)) {
    stopServos();
    waitForConfirmation(
        "SERVO ERROR",
        "Endpoint was not reached",
        "Check obstruction, wiring and servo",
        "CLOSE");
    return false;
  }
  return true;
}

bool CalibrationController::calibrateImuLevel() {
  if (!M5.Imu.isEnabled()) {
    Serial.println("IMU not found");
    return false;
  }

  if (!waitForConfirmation(
          "IMU LEVEL",
          "Place the whole robot on a level surface",
          "Do not touch it during calibration",
          "START",
          "Servos are powered OFF")) {
    return false;
  }

  waitForTouchRelease();
  M5.Imu.setCalibration(0, 64, 0);
  const uint32_t startedAt = millis();
  uint32_t lastDrawAt = 0;
  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;

  while (millis() - startedAt < kImuCalibrationMs) {
    M5StackChan.update();
    M5.Imu.update();
    M5.Imu.getAccel(&ax, &ay, &az);

    const uint32_t elapsed = millis() - startedAt;
    if (elapsed - lastDrawAt >= 120) {
      drawImuProgress(elapsed, ax, ay, az);
      lastDrawAt = elapsed;
    }

    if (elapsed > 300 && screenTouched()) {
      M5.Imu.setCalibration(0, 0, 0);
      M5.Imu.loadOffsetFromNVS();
      waitForTouchRelease();
      return false;
    }
    delay(5);
  }
  M5.Imu.setCalibration(0, 0, 0);

  double accelSum[3] = {};
  double accelSquareSum[3] = {};
  double gyroSum[3] = {};
  size_t samples = 0;
  const uint32_t sampleStartedAt = millis();

  while (samples < kImuSampleCount &&
         millis() - sampleStartedAt < 10000) {
    M5StackChan.update();
    if (M5.Imu.update() == m5::IMU_Class::sensor_mask_none) {
      delay(2);
      continue;
    }

    const auto& imu = M5.Imu.getImuData();
    for (size_t axis = 0; axis < 3; ++axis) {
      const double accel = imu.accel.value[axis];
      accelSum[axis] += accel;
      accelSquareSum[axis] += accel * accel;
      gyroSum[axis] += imu.gyro.value[axis];
    }
    ++samples;
    delay(5);
  }

  if (samples < kImuSampleCount) {
    M5.Imu.loadOffsetFromNVS();
    Serial.println("IMU sample timeout");
    return false;
  }

  float accelAverage[3];
  float accelStdDev[3];
  float gyroAverage[3];
  for (size_t axis = 0; axis < 3; ++axis) {
    accelAverage[axis] = accelSum[axis] / samples;
    const double variance =
        max(0.0, accelSquareSum[axis] / samples -
                     accelAverage[axis] * accelAverage[axis]);
    accelStdDev[axis] = sqrt(variance);
    gyroAverage[axis] = gyroSum[axis] / samples;
  }

  const float gravity = sqrtf(
      accelAverage[0] * accelAverage[0] +
      accelAverage[1] * accelAverage[1] +
      accelAverage[2] * accelAverage[2]);
  const float motionNoise =
      sqrtf(accelStdDev[0] * accelStdDev[0] +
            accelStdDev[1] * accelStdDev[1] +
            accelStdDev[2] * accelStdDev[2]);
  const float gyroMotion =
      sqrtf(gyroAverage[0] * gyroAverage[0] +
            gyroAverage[1] * gyroAverage[1] +
            gyroAverage[2] * gyroAverage[2]);

  if (gravity < 0.85f || gravity > 1.15f ||
      motionNoise > 0.035f || gyroMotion > 3.0f) {
    M5.Imu.loadOffsetFromNVS();
    Serial.printf(
        "IMU unstable gravity=%.3f noise=%.4f gyro=%.3f\n",
        gravity, motionNoise, gyroMotion);
    waitForConfirmation(
        "IMU ERROR",
        "Robot moved or surface is not stable",
        "Place it level and retry calibration",
        "CLOSE");
    return false;
  }

  data_.levelRollDeg =
      atan2f(accelAverage[1], accelAverage[2]) * 180.0f / PI;
  data_.levelPitchDeg =
      atan2f(
          -accelAverage[0],
          sqrtf(accelAverage[1] * accelAverage[1] +
                accelAverage[2] * accelAverage[2])) *
      180.0f / PI;
  data_.imuLevelValid = true;

  M5.Imu.saveOffsetToNVS();
  saveImuCalibration();
  Serial.printf(
      "IMU level roll=%.3f pitch=%.3f gravity=%.3f\n",
      data_.levelRollDeg, data_.levelPitchDeg, gravity);
  return true;
}

void CalibrationController::saveServoCalibration() {
  Preferences preferences;
  preferences.begin("stack_cal", false);
  preferences.putBool("servo_valid", data_.servoValid);
  preferences.putInt("yaw_min", data_.yawMin);
  preferences.putInt("yaw_max", data_.yawMax);
  preferences.putInt("pitch_min", data_.pitchMin);
  preferences.putInt("pitch_max", data_.pitchMax);
  preferences.end();
}

void CalibrationController::saveImuCalibration() {
  Preferences preferences;
  preferences.begin("stack_cal", false);
  preferences.putBool("imu_valid", data_.imuLevelValid);
  preferences.putFloat("roll_zero", data_.levelRollDeg);
  preferences.putFloat("pitch_zero", data_.levelPitchDeg);
  preferences.end();
}

void CalibrationController::stopServos() {
  M5StackChan.Motion.setTorqueEnabled(false);
  M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);
  M5StackChan.setServoPowerEnabled(false);
}
