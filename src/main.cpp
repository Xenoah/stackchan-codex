#include <M5Unified.h>
#include <esp_system.h>

#include "AvatarFaceController.h"
#include "ConfigPortal.h"
#include "VoiceVoxClient.h"
#include "hardware_features.h"

AvatarFaceController avatarFace;
ConfigPortal configPortal;
VoiceVoxClient voiceVox;

constexpr uint32_t LIP_FRAME_INTERVAL_MS = 30;
constexpr uint32_t LIP_INPUT_TIMEOUT_MS = 220;

int currentMouthOpen = 0;
int targetMouthOpen = 0;
bool lipSyncActive = false;
bool speaking = false;
bool ignoreNextTopClick = false;
uint32_t lipSyncLastInputAt = 0;
uint32_t lipSyncLastFrameAt = 0;

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
  avatarFace.showStatus("VOICEVOX", 900);
  M5StackChan.showRgbColor(0, 0, 96);

  const AppConfig& appConfig = configPortal.config();
  VoiceVoxConfig voiceConfig;
  voiceConfig.host = appConfig.voiceVoxHost;
  voiceConfig.port = appConfig.voiceVoxPort;
  voiceConfig.speaker = appConfig.voiceVoxSpeaker;

  const bool success = voiceVox.speak(voiceConfig, appConfig.speechText);
  stopLipSync();

  if (success) {
    avatarFace.resetToDefault();
    M5StackChan.showRgbColor(0, 48, 0);
  } else {
    avatarFace.setExpression(m5avatar::Expression::Angry);
    avatarFace.showStatus("VOICEVOX ERROR", 2500);
    avatarFace.returnToDefaultAfter(2500);
    M5StackChan.showRgbColor(96, 0, 0);
    Serial.printf("VOICEVOX error: %s\n", voiceVox.lastError().c_str());
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

  M5.Speaker.begin();
  M5.Speaker.setVolume(160);
  voiceVox.setCallbacks(setLipSyncLevel, serviceApp);

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
  Serial.printf("VOICEVOX: http://%s:%u speaker=%d\n",
                configPortal.config().voiceVoxHost.c_str(),
                configPortal.config().voiceVoxPort,
                configPortal.config().voiceVoxSpeaker);

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
