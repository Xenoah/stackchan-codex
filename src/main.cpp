#include <M5Unified.h>
#include <esp_system.h>
#include <math.h>

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
  Calibrate,
};

enum class ServoPromptButton {
  None,
  No,
  Yes,
};

enum class AppMode {
  LocalLlm,
  LevelHold,
};

enum class ModeMenuButton {
  None,
  LocalLlm,
  LevelHold,
  Settings,
  Close,
};

struct PidController {
  float kp = 0.0f;
  float ki = 0.0f;
  float kd = 0.0f;
  float integral = 0.0f;
  float previousError = 0.0f;
  bool hasPrevious = false;

  void reset() {
    integral = 0.0f;
    previousError = 0.0f;
    hasPrevious = false;
  }

  float update(float error, float dt) {
    integral += error * dt;
    integral = constrain(integral, -80.0f, 80.0f);
    const float derivative =
        hasPrevious && dt > 0.0f ? (error - previousError) / dt : 0.0f;
    previousError = error;
    hasPrevious = true;
    return kp * error + ki * integral + kd * derivative;
  }
};

float applyDeadband(float value, float deadband) {
  if (fabsf(value) <= deadband) {
    return 0.0f;
  }
  return value > 0.0f ? value - deadband : value + deadband;
}

int moveTowardByStep(int current, int target, int maxStep) {
  if (target > current) {
    return min(current + maxStep, target);
  }
  if (target < current) {
    return max(current - maxStep, target);
  }
  return current;
}

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

constexpr uint32_t LEVEL_HOLD_UPDATE_INTERVAL_MS = 80;
constexpr int LEVEL_HOLD_SERVO_SPEED = 160;
constexpr float LEVEL_HOLD_FILTER_ALPHA = 0.35f;
constexpr float LEVEL_HOLD_DEADBAND_DEG = 0.6f;
constexpr float LEVEL_HOLD_MAX_OUTPUT_DEG = 35.0f;
constexpr int LEVEL_HOLD_MAX_STEP_DEG = 5;
constexpr float LEVEL_HOLD_ACCEL_MIN_NORM = 0.05f;

AppMode currentMode = AppMode::LocalLlm;
bool levelHoldActive = false;
uint32_t levelHoldLastUpdateAt = 0;
PidController levelHoldRollPid{1.15f, 0.0f, 0.12f};
PidController levelHoldPitchPid{1.35f, 0.0f, 0.14f};
bool levelHoldFilterReady = false;
float levelHoldFilteredRollDeg = 0.0f;
float levelHoldFilteredPitchDeg = 0.0f;
int levelHoldYawTarget = 0;
int levelHoldPitchTarget = 0;

bool modeMenuOpen = false;
bool settingsInfoOpen = false;
bool displayWasTouching = false;
int16_t displayTouchStartX = 0;
int16_t displayTouchStartY = 0;
int16_t displayTouchLastX = 0;
int16_t displayTouchLastY = 0;
uint32_t displayTouchStartedAt = 0;
ModeMenuButton modeMenuPressed = ModeMenuButton::None;

const char* appModeName(AppMode mode) {
  return mode == AppMode::LevelHold ? "LEVEL HOLD" : "LOCAL LLM";
}

void resetLevelHoldPid() {
  levelHoldRollPid.reset();
  levelHoldPitchPid.reset();
  levelHoldFilterReady = false;
  levelHoldFilteredRollDeg = 0.0f;
  levelHoldFilteredPitchDeg = 0.0f;
  levelHoldYawTarget = 0;
  levelHoldPitchTarget = 0;
  levelHoldLastUpdateAt = millis();
}

bool startLevelHoldMode() {
  const auto& calibration = calibrationController.data();
  if (!calibration.servoValid || !calibration.imuLevelValid ||
      !M5.Imu.isEnabled()) {
    avatarFace.setExpression(m5avatar::Expression::Doubt);
    avatarFace.showStatus("CAL REQUIRED", 2500);
    avatarFace.returnToDefaultAfter(2500);
    M5StackChan.showRgbColor(96, 48, 0);
    return false;
  }

  M5StackChan.setServoPowerEnabled(true);
  delay(150);
  M5StackChan.Motion.setAutoAngleSyncEnabled(true);
  M5StackChan.Motion.setAutoTorqueReleaseEnabled(false);
  M5StackChan.Motion.setTorqueEnabled(true);
  delay(80);
  M5StackChan.Motion.move(0, 0, LEVEL_HOLD_SERVO_SPEED);
  delay(120);
  M5StackChan.Motion.setAutoAngleSyncEnabled(false);
  resetLevelHoldPid();
  levelHoldActive = true;
  currentMode = AppMode::LevelHold;
  avatarFace.resetToDefault();
  avatarFace.showStatus("LEVEL HOLD", 1800);
  M5StackChan.showRgbColor(0, 64, 96);
  Serial.println("Mode changed: LEVEL HOLD");
  return true;
}

void stopLevelHoldMode() {
  if (!levelHoldActive) {
    return;
  }
  M5StackChan.Motion.stop();
  delay(50);
  M5StackChan.Motion.setTorqueEnabled(false);
  M5StackChan.Motion.setAutoAngleSyncEnabled(true);
  M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);
  M5StackChan.setServoPowerEnabled(false);
  levelHoldActive = false;
}

void activateMode(AppMode mode) {
  if (mode == AppMode::LevelHold) {
    if (!startLevelHoldMode()) {
      currentMode = AppMode::LocalLlm;
    }
    return;
  }

  stopLevelHoldMode();
  currentMode = AppMode::LocalLlm;
  avatarFace.resetToDefault();
  avatarFace.showStatus("LOCAL LLM", 1800);
  M5StackChan.showRgbColor(0, 48, 0);
  Serial.println("Mode changed: LOCAL LLM");
}

void updateLevelHoldMode() {
  if (!levelHoldActive) {
    return;
  }

  const uint32_t now = millis();
  if (now - levelHoldLastUpdateAt < LEVEL_HOLD_UPDATE_INTERVAL_MS) {
    return;
  }
  const float dt = (now - levelHoldLastUpdateAt) / 1000.0f;
  levelHoldLastUpdateAt = now;

  if (M5.Imu.update() == m5::IMU_Class::sensor_mask_none) {
    return;
  }

  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  M5.Imu.getAccel(&ax, &ay, &az);

  const float accelNorm = sqrtf(ax * ax + ay * ay + az * az);
  if (accelNorm < LEVEL_HOLD_ACCEL_MIN_NORM) {
    return;
  }

  const auto& calibration = calibrationController.data();
  const float rollDeg =
      atan2f(ay, az) * 180.0f / PI - calibration.levelRollDeg;
  const float pitchDeg =
      atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / PI -
      calibration.levelPitchDeg;

  if (!levelHoldFilterReady) {
    levelHoldFilteredRollDeg = rollDeg;
    levelHoldFilteredPitchDeg = pitchDeg;
    levelHoldFilterReady = true;
  } else {
    levelHoldFilteredRollDeg +=
        (rollDeg - levelHoldFilteredRollDeg) * LEVEL_HOLD_FILTER_ALPHA;
    levelHoldFilteredPitchDeg +=
        (pitchDeg - levelHoldFilteredPitchDeg) * LEVEL_HOLD_FILTER_ALPHA;
  }

  const float rollError =
      applyDeadband(levelHoldFilteredRollDeg, LEVEL_HOLD_DEADBAND_DEG);
  const float pitchError =
      applyDeadband(levelHoldFilteredPitchDeg, LEVEL_HOLD_DEADBAND_DEG);

  const int yawCorrection = static_cast<int>(roundf(constrain(
      levelHoldRollPid.update(rollError, dt),
      -LEVEL_HOLD_MAX_OUTPUT_DEG,
      LEVEL_HOLD_MAX_OUTPUT_DEG)));
  const int pitchCorrection = static_cast<int>(roundf(constrain(
      -levelHoldPitchPid.update(pitchError, dt),
      -LEVEL_HOLD_MAX_OUTPUT_DEG,
      LEVEL_HOLD_MAX_OUTPUT_DEG)));

  const int yawTarget = constrain(
      yawCorrection, calibration.yawMin, calibration.yawMax);
  const int pitchTarget = constrain(
      pitchCorrection, calibration.pitchMin, calibration.pitchMax);

  levelHoldYawTarget = moveTowardByStep(
      levelHoldYawTarget, yawTarget, LEVEL_HOLD_MAX_STEP_DEG);
  levelHoldPitchTarget = moveTowardByStep(
      levelHoldPitchTarget, pitchTarget, LEVEL_HOLD_MAX_STEP_DEG);

  M5StackChan.Motion.move(
      levelHoldYawTarget, levelHoldPitchTarget, LEVEL_HOLD_SERVO_SPEED);
}

void updateActiveMode() {
  updateLevelHoldMode();
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
  updateActiveMode();
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

ModeMenuButton modeMenuButtonAt(int16_t x, int16_t y) {
  const int16_t width = M5.Display.width();
  const int16_t rowX = 18;
  const int16_t rowW = width - rowX * 2;

  if (x < rowX || x > rowX + rowW) {
    return ModeMenuButton::None;
  }
  if (y >= 48 && y <= 88) {
    return ModeMenuButton::LocalLlm;
  }
  if (y >= 96 && y <= 136) {
    return ModeMenuButton::LevelHold;
  }
  if (y >= 144 && y <= 184) {
    return ModeMenuButton::Settings;
  }
  if (y >= 192 && y <= 232) {
    return ModeMenuButton::Close;
  }
  return ModeMenuButton::None;
}

void drawModeMenu(ModeMenuButton pressed) {
  auto& display = startupCanvas();
  const int16_t width = display.width();
  constexpr int16_t rowX = 18;
  const int16_t rowW = width - rowX * 2;
  constexpr int16_t rowH = 40;

  display.fillScreen(TFT_BLACK);
  display.setFont(&fonts::Font2);
  display.setTextDatum(middle_center);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("MODE", width / 2, 18);

  display.setTextSize(1);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString(appModeName(currentMode), width / 2, 36);

  auto drawRow = [&](ModeMenuButton button, const char* label,
                     int16_t y, uint16_t color) {
    const bool selected =
        (button == ModeMenuButton::LocalLlm &&
         currentMode == AppMode::LocalLlm) ||
        (button == ModeMenuButton::LevelHold &&
         currentMode == AppMode::LevelHold);
    const bool isPressed = pressed == button;
    const uint16_t fill =
        isPressed ? color : (selected ? 0x2945 : 0x2104);
    display.fillRoundRect(rowX, y, rowW, rowH, 8, fill);
    display.drawRoundRect(rowX, y, rowW, rowH, 8,
                          selected ? TFT_YELLOW : TFT_DARKGREY);
    display.setTextColor(TFT_WHITE, fill);
    display.setTextSize(2);
    display.drawString(label, width / 2, y + rowH / 2);
  };

  drawRow(ModeMenuButton::LocalLlm, "LOCAL LLM", 48, 0x03E0);
  drawRow(ModeMenuButton::LevelHold, "LEVEL HOLD", 96, 0x035F);
  drawRow(ModeMenuButton::Settings, "SETTINGS", 144, 0x8010);
  drawRow(ModeMenuButton::Close, "CLOSE", 192, 0x4208);
  display.pushSprite(0, 0);
}

void drawSettingsInfo() {
  auto& display = startupCanvas();
  const int16_t width = display.width();
  const int16_t height = display.height();

  display.fillScreen(TFT_BLACK);
  display.setFont(&fonts::Font2);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.drawString("SETTINGS", width / 2, 20);

  display.setTextSize(1);
  int16_t y = 50;

  if (configPortal.isConnected()) {
    const String url = "http://" + configPortal.localIp().toString() + "/";
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.drawString("Open in browser (Wi-Fi):", width / 2, y);
    y += 18;
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.drawString(url, width / 2, y);
    y += 26;
  }

  if (configPortal.isSettingsApActive()) {
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.drawString("Hotspot: " + configPortal.accessPointName(), width / 2, y);
    y += 16;
    display.drawString("Password: stackchan", width / 2, y);
    y += 18;
    const String apUrl = "http://" + configPortal.settingsApIp().toString() + "/";
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.drawString(apUrl, width / 2, y);
  }

  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.drawString("Tap to close", width / 2, height - 14);
  display.pushSprite(0, 0);
}

void openSettingsInfo() {
  settingsInfoOpen = true;
  configPortal.startSettingsAp();
  avatarFace.pauseDrawing();
  delay(20);
  drawSettingsInfo();
}

void closeSettingsInfo() {
  configPortal.stopSettingsAp();
  settingsInfoOpen = false;
  avatarFace.resumeDrawing();
  avatarFace.resetToDefault();
}

void openModeMenu() {
  if (modeMenuOpen) {
    return;
  }
  modeMenuOpen = true;
  modeMenuPressed = ModeMenuButton::None;
  avatarFace.pauseDrawing();
  delay(20);
  drawModeMenu(modeMenuPressed);
}

void closeModeMenu() {
  if (!modeMenuOpen) {
    return;
  }
  modeMenuOpen = false;
  modeMenuPressed = ModeMenuButton::None;
  avatarFace.resumeDrawing();
  avatarFace.resetToDefault();
}

bool handleDisplayTouch() {
  int16_t x = 0;
  int16_t y = 0;
  const bool touching = M5.Display.getTouch(&x, &y);

  if (settingsInfoOpen) {
    if (!touching && displayWasTouching) {
      closeSettingsInfo();
    }
    displayWasTouching = touching;
    return true;
  }

  if (modeMenuOpen) {
    const ModeMenuButton current =
        touching ? modeMenuButtonAt(x, y) : ModeMenuButton::None;
    if (touching && current != modeMenuPressed) {
      modeMenuPressed = current;
      drawModeMenu(modeMenuPressed);
    }
    if (!touching && displayWasTouching) {
      const ModeMenuButton selected = modeMenuPressed;
      closeModeMenu();
      if (selected == ModeMenuButton::LocalLlm) {
        activateMode(AppMode::LocalLlm);
      } else if (selected == ModeMenuButton::LevelHold) {
        activateMode(AppMode::LevelHold);
      } else if (selected == ModeMenuButton::Settings) {
        openSettingsInfo();
      }
    }
    displayWasTouching = touching;
    return true;
  }

  if (touching && !displayWasTouching) {
    displayTouchStartX = x;
    displayTouchStartY = y;
    displayTouchLastX = x;
    displayTouchLastY = y;
    displayTouchStartedAt = millis();
  } else if (touching) {
    displayTouchLastX = x;
    displayTouchLastY = y;
  } else if (!touching && displayWasTouching) {
    const int16_t height = M5.Display.height();
    const int16_t verticalTravel =
        displayTouchStartY - displayTouchLastY;
    const int16_t horizontalTravel =
        abs(displayTouchLastX - displayTouchStartX);
    const uint32_t duration = millis() - displayTouchStartedAt;
    if (displayTouchStartY >= height - 44 &&
        verticalTravel >= 70 &&
        horizontalTravel <= 100 &&
        duration <= 1200) {
      openModeMenu();
      displayWasTouching = false;
      return true;
    }
  }

  displayWasTouching = touching;
  return false;
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
  return x < M5.Display.width() / 2 ? ServoPromptButton::No
                                    : ServoPromptButton::Yes;
}

void drawServoStartupPrompt(ServoPromptButton pressed) {
  auto& display = startupCanvas();
  const int16_t width = display.width();
  constexpr int16_t kMargin = 14;
  constexpr int16_t kGap = 10;
  constexpr int16_t kButtonTop = 160;
  constexpr int16_t kButtonHeight = 66;
  const int16_t buttonWidth =
      (width - kMargin * 2 - kGap) / 2;
  const int16_t noX = kMargin;
  const int16_t yesX = noX + buttonWidth + kGap;

  const uint16_t noColor =
      pressed == ServoPromptButton::No ? 0x7800 : 0x4208;
  const uint16_t yesColor =
      pressed == ServoPromptButton::Yes ? 0x03E0 : 0x0260;

  display.fillScreen(TFT_BLACK);
  display.setFont(&fonts::Font2);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.drawString("SERVO STARTUP", width / 2, 30);

  display.setTextSize(1);
  display.drawString("Run full calibration?", width / 2, 76);
  display.setTextColor(TFT_YELLOW, TFT_BLACK);
  display.drawString("Clear the area before selecting YES", width / 2, 111);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("Touch a button to continue", width / 2, 137);

  display.fillRoundRect(
      noX, kButtonTop, buttonWidth, kButtonHeight, 10, noColor);
  display.drawRoundRect(
      noX, kButtonTop, buttonWidth, kButtonHeight, 10, TFT_WHITE);
  display.fillRoundRect(
      yesX, kButtonTop, buttonWidth, kButtonHeight, 10, yesColor);
  display.drawRoundRect(
      yesX, kButtonTop, buttonWidth, kButtonHeight, 10, TFT_WHITE);

  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.drawString("NO", noX + buttonWidth / 2, kButtonTop + 33);
  display.drawString("YES", yesX + buttonWidth / 2, kButtonTop + 33);
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

      if (selected == ServoPromptButton::No) {
        avatarFace.resumeDrawing();
        avatarFace.resetToDefault();
        return ServoStartupChoice::KeepPosition;
      }
      if (selected == ServoPromptButton::Yes) {
        avatarFace.resumeDrawing();
        avatarFace.resetToDefault();
        return ServoStartupChoice::Calibrate;
      }
    }

    wasTouching = touching;
    delay(10);
  }
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
  if (servoChoice == ServoStartupChoice::Calibrate) {
    Serial.println("Servo startup choice: CALIBRATE");
    calibrationController.run(avatarFace);
  } else {
    Serial.println("Servo startup choice: NO");
    M5StackChan.Motion.setTorqueEnabled(false);
    M5StackChan.setServoPowerEnabled(false);
    avatarFace.showStatus("SERVO: NO", 1800);
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
  avatarFace.showStatus("LOCAL LLM");
  M5StackChan.showRgbColor(0, 48, 0);
  Serial.println("Mode default: LOCAL LLM");
}

void loop() {
  serviceApp();

  if (configPortal.isPortalActive()) {
    delay(5);
    return;
  }

  if (handleDisplayTouch()) {
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
