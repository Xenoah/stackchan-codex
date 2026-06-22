#include <M5GFX.h>
#include <M5Unified.h>

#include "ConfigPortal.h"
#include "VoiceVoxClient.h"
#include "hardware_features.h"

LGFX_Sprite face(&M5.Display);
ConfigPortal configPortal;
VoiceVoxClient voiceVox;

enum FaceEmotion {
  FACE_NEUTRAL,
  FACE_HAPPY,
  FACE_ANGRY,
  FACE_SAD,
  FACE_SLEEPY
};

constexpr uint32_t LIP_FRAME_INTERVAL_MS = 30;
constexpr uint32_t LIP_INPUT_TIMEOUT_MS = 220;

FaceEmotion currentEmotion = FACE_NEUTRAL;
int currentLookX = 0;
int currentLookY = 0;
int currentMouthOpen = 0;
int targetMouthOpen = 0;

bool lipSyncActive = false;
bool speaking = false;
uint32_t lipSyncLastInputAt = 0;
uint32_t lipSyncLastFrameAt = 0;

static int mapRange(int v, int inMin, int inMax, int outMin, int outMax) {
  v = constrain(v, inMin, inMax);
  return (v - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

void drawEye(int cx, int cy, int size, int lidWeight, int tilt) {
  const uint16_t FG = 0xFFFF;
  const uint16_t BG = 0x0000;
  const int r = size / 2;

  face.fillCircle(cx, cy, r, FG);

  const int cover = mapRange(100 - lidWeight, 0, 100, 0, size + 4);
  if (cover > 0) {
    face.fillRect(cx - r - 2, cy - r - 2, size + 4, cover, BG);
  }

  if (tilt > 0) {
    face.fillTriangle(cx - r - 2, cy - r - 2, cx + r + 2, cy - r - 2,
                      cx + r + 2, cy - r + tilt, BG);
  } else if (tilt < 0) {
    const int t = -tilt;
    face.fillTriangle(cx - r - 2, cy - r - 2, cx + r + 2, cy - r - 2,
                      cx - r - 2, cy - r + t, BG);
  }
}

void drawMouth(int cx, int cy, int open) {
  const uint16_t FG = 0xFFFF;
  open = constrain(open, 0, 100);

  const int w = mapRange(open, 0, 100, 90, 60);
  const int h = mapRange(open, 0, 100, 6, 50);
  const int r = mapRange(open, 0, 100, 0, 16);

  face.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, FG);
}

void drawStackChanFace(FaceEmotion emotion, int lookX, int lookY,
                       int mouthOpen) {
  const uint16_t BG = 0x0000;
  const int W = M5.Display.width();
  const int H = M5.Display.height();
  const int cx = W / 2;
  const int cy = H / 2;
  const int ox = mapRange(lookX, -100, 100, -16, 16);
  const int oy = mapRange(lookY, -100, 100, -16, 16);

  int eyeSize = 20;
  int lidWeight = 100;
  int tilt = 0;

  switch (emotion) {
    case FACE_HAPPY:
      lidWeight = 72;
      tilt = 8;
      break;
    case FACE_ANGRY:
      lidWeight = 70;
      tilt = -10;
      mouthOpen = 0;
      break;
    case FACE_SAD:
      lidWeight = 70;
      tilt = 10;
      mouthOpen = 5;
      break;
    case FACE_SLEEPY:
      lidWeight = 35;
      mouthOpen = 0;
      break;
    case FACE_NEUTRAL:
    default:
      break;
  }

  face.fillScreen(BG);
  drawEye(cx - 70 + ox, cy - 16 + oy, eyeSize, lidWeight, tilt);
  drawEye(cx + 70 + ox, cy - 16 + oy, eyeSize, lidWeight, -tilt);
  drawMouth(cx + ox, cy + 26 + oy, mouthOpen);
  face.pushSprite(0, 0);
}

void renderCurrentFace() {
  drawStackChanFace(currentEmotion, currentLookX, currentLookY,
                    currentMouthOpen);
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

  renderCurrentFace();

  if (currentMouthOpen == 0 && targetMouthOpen == 0) {
    lipSyncActive = false;
  }
}

void serviceApp() {
  M5StackChan.update();
  configPortal.update();
  updateLipSync();
}

void drawSetupScreen() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(1);
  M5.Display.drawCentreString("StackChan Wi-Fi Setup", 160, 32, 2);
  M5.Display.drawCentreString(configPortal.accessPointName(), 160, 75, 2);
  M5.Display.drawCentreString("Password: stackchan", 160, 105, 2);
  M5.Display.drawCentreString("Open http://192.168.4.1", 160, 145, 2);
  M5.Display.drawCentreString("VOICEVOX host / Wi-Fi", 160, 180, 2);
  M5.Display.drawCentreString("wo settei shite kudasai", 160, 205, 2);
}

void speakConfiguredText() {
  if (speaking || !configPortal.isConnected()) {
    return;
  }

  speaking = true;
  currentEmotion = FACE_HAPPY;
  M5StackChan.showRgbColor(0, 0, 96);
  renderCurrentFace();

  const AppConfig& appConfig = configPortal.config();
  VoiceVoxConfig voiceConfig;
  voiceConfig.host = appConfig.voiceVoxHost;
  voiceConfig.port = appConfig.voiceVoxPort;
  voiceConfig.speaker = appConfig.voiceVoxSpeaker;

  const bool success = voiceVox.speak(voiceConfig, appConfig.speechText);
  stopLipSync();

  if (success) {
    currentEmotion = FACE_NEUTRAL;
    M5StackChan.showRgbColor(0, 48, 0);
  } else {
    currentEmotion = FACE_ANGRY;
    M5StackChan.showRgbColor(96, 0, 0);
    Serial.printf("VOICEVOX error: %s\n", voiceVox.lastError().c_str());
  }

  renderCurrentFace();
  speaking = false;
}

void setup() {
  Serial.begin(115200);

  M5StackChan.begin();

  // BSP initializes every body device. Keep the motor library ready, but
  // release torque and power until an explicit motion feature is requested.
  M5StackChan.Motion.setTorqueEnabled(false);
  M5StackChan.setServoPowerEnabled(false);
  M5StackChan.showRgbColor(0, 0, 0);

  if (M5.Display.width() < M5.Display.height()) {
    M5.Display.setRotation(1);
  }

  face.setColorDepth(16);
  face.createSprite(M5.Display.width(), M5.Display.height());

  M5.Speaker.begin();
  M5.Speaker.setVolume(160);

  voiceVox.setCallbacks(setLipSyncLevel, serviceApp);

  if (!configPortal.begin()) {
    drawSetupScreen();
    return;
  }

  Serial.printf("StackChan IP: %s\n",
                configPortal.localIp().toString().c_str());
  Serial.printf("VOICEVOX: http://%s:%u speaker=%d\n",
                configPortal.config().voiceVoxHost.c_str(),
                configPortal.config().voiceVoxPort,
                configPortal.config().voiceVoxSpeaker);

  M5StackChan.showRgbColor(0, 48, 0);
  renderCurrentFace();
}

void loop() {
  serviceApp();

  if (configPortal.isPortalActive()) {
    delay(5);
    return;
  }

  if (M5.BtnA.wasPressed() ||
      M5StackChan.TouchSensor.wasClicked()) {
    speakConfiguredText();
  }

  if (!speaking && M5.BtnB.wasPressed()) {
    stopLipSync();
    currentEmotion = FACE_SLEEPY;
    renderCurrentFace();
  }

  if (!speaking && M5.BtnC.wasPressed()) {
    stopLipSync();
    currentEmotion = FACE_NEUTRAL;
    renderCurrentFace();
  }

  delay(5);
}
