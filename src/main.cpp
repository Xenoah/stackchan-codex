#include <M5Unified.h>
#include <esp_system.h>

#include "AvatarFaceController.h"
#include "CalibrationController.h"
#include "ConfigPortal.h"
#include "VoiceVoxClient.h"
#include "hardware_features.h"

AvatarFaceController avatarFace;
CalibrationController calibrationController;
ConfigPortal configPortal;
TtsClient ttsClient;

constexpr uint32_t LIP_FRAME_INTERVAL_MS = 30;
constexpr uint32_t LIP_INPUT_TIMEOUT_MS = 220;

int currentMouthOpen = 0;
int targetMouthOpen = 0;
bool lipSyncActive = false;
bool speaking = false;
bool ignoreNextTopClick = false;
uint32_t lipSyncLastInputAt = 0;
uint32_t lipSyncLastFrameAt = 0;

enum class ServoStartupChoice {
  KeepPosition,
  GoHome,
  Calibrate,
};

enum class ServoPromptButton {
  None,
  Keep,
  Home,
  Calibrate,
};

TtsEngineType ttsEngineTypeFromString(const String& value) {
  return value == "simple_wav" ? TtsEngineType::SimpleWav
                               : TtsEngineType::VoiceVoxCompatible;
}

M5Canvas& startupCanvas() {
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

void setLipSyncLevel(int level) {
  targetMouthOpen = constrain(level, 0, 100);
  lipSyncLastInputAt = millis();
  lipSyncActive = true;
}

void stopLipSync() {
  targetMouthOpen = 0;
  lipSyncActive = true;
  lipSyncLastInputAt = millis();
}

void updateLipSync() {
  const uint32_t now = millis();

  if (lipSyncActive && now - lipSyncLastInputAt >= LIP_INPUT_TIMEOUT_MS) {
    targetMouthOpen = 0;
  }

  if (!lipSyncActive ||
      now - lipSyncLastFrameAt < LIP_FRAME_INTERVAL_MS) {
    return;
  }
  lipSyncLastFrameAt = now;

  if (currentMouthOpen < targetMouthOpen) {
    currentMouthOpen +=
        max(3, (targetMouthOpen - currentMouthOpen + 1) / 2);
    currentMouthOpen = min(currentMouthOpen, targetMouthOpen);
  } else if (currentMouthOpen > targetMouthOpen) {
    currentMouthOpen -=
        max(2, (currentMouthOpen - targetMouthOpen + 3) / 4);
    currentMouthOpen = max(currentMouthOpen, targetMouthOpen);
  }

  avatarFace.setMouthOpenRatio(currentMouthOpen / 100.0f);

  if (currentMouthOpen == 0 && targetMouthOpen == 0) {
    lipSyncActive = false;
  }
}

void serviceApp() {
  M5StackChan.update();
  configPortal.update();
  avatarFace.update();
  updateLipSync();
}

void speakConfiguredText() {
  if (speaking || !configPortal.isConnected()) {
    return;
  }

  if (avatarFace.isShowcaseEnabled()) {
    avatarFace.toggleShowcase();
  }

  speaking = true;
  avatarFace.setExpression(m5avatar::Expression::Happy);
  avatarFace.showStatus("TTS", 900);
  M5StackChan.showRgbColor(0, 0, 96);

  const AppConfig& appConfig = configPortal.config();
  TtsConfig ttsConfig;
  ttsConfig.host = appConfig.ttsHost;
  ttsConfig.port = appConfig.ttsPort;
  ttsConfig.speaker = appConfig.ttsSpeaker;
  ttsConfig.engineType =
      ttsEngineTypeFromString(appConfig.ttsEngineType);

  const bool success = ttsClient.speak(ttsConfig, appConfig.speechText);
  stopLipSync();

  if (success) {
    avatarFace.resetToDefault();
    M5StackChan.showRgbColor(0, 48, 0);
  } else {
    avatarFace.setExpression(m5avatar::Expression::Angry);
    avatarFace.showStatus("TTS ERROR", 2500);
    avatarFace.returnToDefaultAfter(2500);
    M5StackChan.showRgbColor(96, 0, 0);
    Serial.printf("TTS error: %s\n", ttsClient.lastError().c_str());
  }

  speaking = false;
}

void handleTopTouch() {
  auto& touch = M5StackChan.TouchSensor;

  if (touch.wasSwipedForward()) {
    ignoreNextTopClick = true;
    avatarFace.nextPalette(1);
  } else if (touch.wasSwipedBackward()) {
    ignoreNextTopClick = true;
    avatarFace.nextPalette(-1);
  }

  if (touch.wasDoubleClicked()) {
    ignoreNextTopClick = false;
    avatarFace.nextEyePattern();
  } else if (touch.wasSingleClicked()) {
    if (ignoreNextTopClick) {
      ignoreNextTopClick = false;
    } else {
      speakConfiguredText();
    }
  } else if (touch.wasDecideClickCount() &&
             touch.getClickCount() >= 3) {
    ignoreNextTopClick = false;
    avatarFace.nextTransform();
  }

  if (touch.wasHold()) {
    ignoreNextTopClick = false;
    avatarFace.toggleShowcase();
  }
}

void drawBootFallbackFace() {
  auto cfg = M5.config();
  M5.begin(cfg);

  if (M5.Display.width() < M5.Display.height()) {
    M5.Display.setRotation(1);
  }
  M5.Display.setBrightness(160);
  M5.Display.fillScreen(TFT_BLACK);

  const int cx = M5.Display.width() / 2;
  const int cy = M5.Display.height() / 2;
  M5.Display.fillCircle(cx - 64, cy - 18, 12, TFT_WHITE);
  M5.Display.fillCircle(cx + 64, cy - 18, 12, TFT_WHITE);
  M5.Display.fillRoundRect(cx - 42, cy + 30, 84, 7, 3, TFT_WHITE);
}

ServoPromptButton servoPromptButtonAt(int16_t x, int16_t y) {
  constexpr int16_t kButtonTop = 160;
  constexpr int16_t kButtonBottom = 226;

  if (y < kButtonTop || y > kButtonBottom) {
    return ServoPromptButton::None;
  }
  const int16_t third = M5.Display.width() / 3;
  if (x < third) {
    return ServoPromptButton::Keep;
  }
  if (x < third * 2) {
    return ServoPromptButton::Home;
  }
  return ServoPromptButton::Calibrate;
}

void drawServoStartupPrompt(ServoPromptButton pressed) {
  auto& display = startupCanvas();
  const int16_t width = display.width();
  constexpr int16_t kMargin = 8;
  constexpr int16_t kGap = 6;
  constexpr int16_t kButtonTop = 160;
  constexpr int16_t kButtonHeight = 66;
  const int16_t buttonWidth =
      (width - kMargin * 2 - kGap * 2) / 3;
  const int16_t keepX = kMargin;
  const int16_t homeX = keepX + buttonWidth + kGap;
  const int16_t calibrateX = homeX + buttonWidth + kGap;

  const uint16_t keepColor =
      pressed == ServoPromptButton::Keep ? 0x7800 : 0x4208;
  const uint16_t homeColor =
      pressed == ServoPromptButton::Home ? 0x03E0 : 0x0260;
  const uint16_t calibrateColor =
      pressed == ServoPromptButton::Calibrate ? 0x001F : 0x0010;

  display.fillScreen(TFT_BLACK);
  display.setFont(&fonts::Font2);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.drawString("SERVO STARTUP", width / 2, 30);

  display.setTextSize(1);
  display.drawString("Move head to HOME (0, 0)?", width / 2, 76);
  display.setTextColor(TFT_YELLOW, TFT_BLACK);
  display.drawString("Clear the area before selecting OK", width / 2, 111);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("Touch a button to continue", width / 2, 137);

  display.fillRoundRect(
      keepX, kButtonTop, buttonWidth, kButtonHeight, 10, keepColor);
  display.drawRoundRect(
      keepX, kButtonTop, buttonWidth, kButtonHeight, 10, TFT_WHITE);
  display.fillRoundRect(
      homeX, kButtonTop, buttonWidth, kButtonHeight, 10, homeColor);
  display.drawRoundRect(
      homeX, kButtonTop, buttonWidth, kButtonHeight, 10, TFT_WHITE);
  display.fillRoundRect(
      calibrateX, kButtonTop, buttonWidth, kButtonHeight, 10,
      calibrateColor);
  display.drawRoundRect(
      calibrateX, kButtonTop, buttonWidth, kButtonHeight, 10,
      TFT_WHITE);

  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.drawString("NO", keepX + buttonWidth / 2, kButtonTop + 22);
  display.drawString("OK", homeX + buttonWidth / 2, kButtonTop + 22);
  display.drawString(
      "CAL", calibrateX + buttonWidth / 2, kButtonTop + 22);
  display.setTextSize(1);
  display.drawString(
      "KEEP", keepX + buttonWidth / 2, kButtonTop + 49);
  display.drawString(
      "HOME", homeX + buttonWidth / 2, kButtonTop + 49);
  display.drawString(
      "ALL", calibrateX + buttonWidth / 2, kButtonTop + 49);
  display.pushSprite(0, 0);
}

ServoStartupChoice askServoStartupChoice() {
  avatarFace.pauseDrawing();
  delay(40);
  drawServoStartupPrompt(ServoPromptButton::None);

  int16_t touchX = 0;
  int16_t touchY = 0;

  // Ignore a finger that was already on the display during startup.
  while (M5.Display.getTouch(&touchX, &touchY)) {
    M5StackChan.update();
    delay(10);
  }

  bool wasTouching = false;
  ServoPromptButton pressed = ServoPromptButton::None;

  while (true) {
    M5StackChan.update();
    const bool touching = M5.Display.getTouch(&touchX, &touchY);
    const ServoPromptButton current =
        touching ? servoPromptButtonAt(touchX, touchY)
                 : ServoPromptButton::None;

    if (touching && current != pressed) {
      pressed = current;
      drawServoStartupPrompt(pressed);
    }

    if (!touching && wasTouching) {
      const ServoPromptButton selected = pressed;
      pressed = ServoPromptButton::None;
      drawServoStartupPrompt(pressed);

      if (selected == ServoPromptButton::Keep) {
        avatarFace.resumeDrawing();
        avatarFace.resetToDefault();
        return ServoStartupChoice::KeepPosition;
      }
      if (selected == ServoPromptButton::Home) {
        avatarFace.resumeDrawing();
        avatarFace.resetToDefault();
        return ServoStartupChoice::GoHome;
      }
      if (selected == ServoPromptButton::Calibrate) {
        avatarFace.resumeDrawing();
        avatarFace.resetToDefault();
        return ServoStartupChoice::Calibrate;
      }
    }

    wasTouching = touching;
    delay(10);
  }
}

bool moveServoToHomeSafely() {
  constexpr int kHomeSpeed = 180;
  constexpr uint32_t kPowerStabilizeMs = 250;
  constexpr uint32_t kHomeTimeoutMs = 12000;

  M5StackChan.showRgbColor(48, 24, 0);
  avatarFace.setExpression(m5avatar::Expression::Doubt);
  avatarFace.showStatus("SERVO: HOME", 0);

  M5StackChan.setServoPowerEnabled(true);
  delay(kPowerStabilizeMs);

  // Read the physical position after power-up, then write that same position
  // as the first target while torque is still off. This prevents a jump to a
  // stale target when torque is enabled.
  const auto currentAngles = M5StackChan.Motion.getCurrentAngles();
  Serial.printf("Servo current yaw=%d pitch=%d\n",
                currentAngles.x, currentAngles.y);

  M5StackChan.Motion.setAutoAngleSyncEnabled(true);
  M5StackChan.Motion.setAutoTorqueReleaseEnabled(false);
  M5StackChan.Motion.move(
      currentAngles.x, currentAngles.y, 1000);
  delay(100);

  M5StackChan.Motion.setTorqueEnabled(true);
  delay(80);
  M5StackChan.Motion.goHome(kHomeSpeed);

  const uint32_t startedAt = millis();
  while (M5StackChan.Motion.isMoving() &&
         millis() - startedAt < kHomeTimeoutMs) {
    M5StackChan.update();
    avatarFace.update();
    delay(20);
  }

  const bool completed = !M5StackChan.Motion.isMoving();
  if (!completed) {
    M5StackChan.Motion.stop();
    delay(100);
  }

  M5StackChan.Motion.setTorqueEnabled(false);
  M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);
  M5StackChan.setServoPowerEnabled(false);

  if (completed) {
    Serial.println("Servo home completed");
    avatarFace.resetToDefault();
    avatarFace.showStatus("SERVO: HOME OK", 1800);
    M5StackChan.showRgbColor(0, 48, 0);
  } else {
    Serial.println("Servo home timeout");
    avatarFace.setExpression(m5avatar::Expression::Angry);
    avatarFace.showStatus("SERVO TIMEOUT", 3000);
    avatarFace.returnToDefaultAfter(3000);
    M5StackChan.showRgbColor(96, 0, 0);
  }

  return completed;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\nStackChan boot, reset reason=%d\n",
                static_cast<int>(esp_reset_reason()));

  // Bring up the display first. This fallback remains visible even if a
  // damaged or disconnected body peripheral blocks the later BSP startup.
  drawBootFallbackFace();
  Serial.println("Fallback face ready");

  Serial.println("Initializing StackChan BSP...");
  M5StackChan.begin();
  Serial.println("StackChan BSP ready");

  // Keep the servo APIs ready while preventing unintended movement.
  M5StackChan.Motion.setTorqueEnabled(false);
  M5StackChan.setServoPowerEnabled(false);
  M5StackChan.showRgbColor(0, 0, 0);
  Serial.println("Servo torque and power disabled");
  calibrationController.load();
  const auto& savedCalibration = calibrationController.data();
  Serial.printf(
      "Calibration servo=%d imu=%d yaw=[%d,%d] pitch=[%d,%d] "
      "level=(%.3f,%.3f)\n",
      savedCalibration.servoValid, savedCalibration.imuLevelValid,
      savedCalibration.yawMin, savedCalibration.yawMax,
      savedCalibration.pitchMin, savedCalibration.pitchMax,
      savedCalibration.levelRollDeg,
      savedCalibration.levelPitchDeg);

  if (M5.Display.width() < M5.Display.height()) {
    M5.Display.setRotation(1);
  }
  M5.Display.setBrightness(160);
  M5.Display.fillScreen(TFT_BLACK);

  // Start the face before Wi-Fi. It remains visible during connection waits
  // and setup portal mode.
  Serial.println("Starting avatar...");
  avatarFace.begin();
  avatarFace.resetToDefault();
  avatarFace.showStatus("BOOT", 1200);
  Serial.println("Avatar started");

  const ServoStartupChoice servoChoice = askServoStartupChoice();
  if (servoChoice == ServoStartupChoice::GoHome) {
    Serial.println("Servo startup choice: HOME");
    moveServoToHomeSafely();
  } else if (servoChoice == ServoStartupChoice::Calibrate) {
    Serial.println("Servo startup choice: CALIBRATE");
    calibrationController.run(avatarFace);
  } else {
    Serial.println("Servo startup choice: KEEP");
    M5StackChan.Motion.setTorqueEnabled(false);
    M5StackChan.setServoPowerEnabled(false);
    avatarFace.showStatus("SERVO: KEEP", 1800);
  }

  M5.Speaker.begin();
  M5.Speaker.setVolume(160);
  ttsClient.setCallbacks(setLipSyncLevel, serviceApp);

  Serial.println("Starting network configuration...");
  if (!configPortal.begin()) {
    Serial.printf("Setup AP: %s, http://192.168.4.1\n",
                  configPortal.accessPointName().c_str());
    avatarFace.resetToDefault();
    avatarFace.showStatus("SETUP: 192.168.4.1", 0);
    M5StackChan.showRgbColor(48, 32, 0);
    return;
  }

  Serial.printf("StackChan IP: %s\n",
                configPortal.localIp().toString().c_str());
  Serial.printf("TTS: %s http://%s:%u speaker=%s\n",
                configPortal.config().ttsEngineType.c_str(),
                configPortal.config().ttsHost.c_str(),
                configPortal.config().ttsPort,
                configPortal.config().ttsSpeaker.c_str());

  avatarFace.resetToDefault();
  avatarFace.showStatus("READY");
  M5StackChan.showRgbColor(0, 48, 0);
}

void loop() {
  serviceApp();

  if (configPortal.isPortalActive()) {
    delay(5);
    return;
  }

  if (M5.BtnA.wasPressed()) {
    speakConfiguredText();
  }

  if (!speaking && M5.BtnB.wasPressed()) {
    avatarFace.nextExpression();
  }

  if (!speaking && M5.BtnC.wasPressed()) {
    avatarFace.nextFace();
  }

  if (!speaking) {
    handleTopTouch();
  }

  delay(5);
}
