#include "AvatarFaceController.h"

#include <M5Unified.h>
#include <esp_system.h>

namespace {

constexpr m5avatar::Expression kExpressions[] = {
    m5avatar::Expression::Happy,
    m5avatar::Expression::Angry,
    m5avatar::Expression::Sad,
    m5avatar::Expression::Doubt,
    m5avatar::Expression::Sleepy,
    m5avatar::Expression::Neutral,
};

constexpr const char* kExpressionNames[] = {
    "HAPPY", "ANGRY", "SAD", "DOUBT", "SLEEPY", "NEUTRAL",
};

constexpr const char* kFaceNames[] = {
    "DEFAULT", "SIMPLE", "OMEGA", "GIRLY", "GIRLY 2", "PINK DEMON",
    "DOGGY",
};

constexpr const char* kPaletteNames[] = {
    "DEFAULT", "SKIN", "CYBER", "MONO", "DEMON",
};

constexpr const char* kEyePatternNames[] = {
    "AUTO BLINK", "OPEN", "WINK LEFT", "WINK RIGHT", "CLOSED",
};

constexpr const char* kTransformNames[] = {
    "NORMAL", "ZOOM IN", "ZOOM OUT", "TILT LEFT", "TILT RIGHT",
};

constexpr uint32_t kDefaultReturnDelayMs = 5000;
constexpr uint32_t kBlinkIntervalMinMs = 3000;
constexpr uint32_t kBlinkIntervalMaxMs = 10000;
constexpr uint32_t kBlinkClosedMinMs = 100;
constexpr uint32_t kBlinkClosedMaxMs = 180;

size_t wrapIndex(size_t current, int direction, size_t count) {
  const int next = static_cast<int>(current) + direction;
  return static_cast<size_t>((next % static_cast<int>(count) +
                              static_cast<int>(count)) %
                             static_cast<int>(count));
}

}  // namespace

void AvatarFaceController::begin() {
  if (started_) {
    return;
  }

  initializeFaces();
  initializePalettes();
  randomSeed(esp_random());

  // Avatar setters suspend the drawing task internally. Start that task
  // before applying the initial face state; calling them first can suspend
  // the setup task itself because the drawing task handle is still null.
  avatar_.setPosition(0, 0);
  avatar_.init(8);
  started_ = true;

  applyFace();
  applyPalette();
  applyExpression();
  applyEyePattern();
  applyTransform();
  scheduleNextBlink(millis());
}

void AvatarFaceController::update() {
  if (!started_) {
    return;
  }

  const uint32_t now = millis();
  if (statusClearAt_ != 0 &&
      static_cast<int32_t>(now - statusClearAt_) >= 0) {
    avatar_.setSpeechText("");
    statusClearAt_ = 0;
  }

  if (showcaseEnabled_ &&
      static_cast<int32_t>(now - nextShowcaseAt_) >= 0) {
    advanceShowcase();
    nextShowcaseAt_ = now + 1800;
  }

  if (!showcaseEnabled_ && defaultReturnAt_ != 0 &&
      static_cast<int32_t>(now - defaultReturnAt_) >= 0) {
    resetToDefault();
  }

  updateBlink(now);
}

void AvatarFaceController::setExpression(
    m5avatar::Expression expression) {
  for (size_t i = 0; i < kExpressionCount; ++i) {
    if (kExpressions[i] == expression) {
      expressionIndex_ = i;
      applyExpression();
      return;
    }
  }
}

void AvatarFaceController::nextExpression() {
  expressionIndex_ = (expressionIndex_ + 1) % kExpressionCount;
  applyExpression();
  showStatus(expressionName());
  returnToDefaultAfter(kDefaultReturnDelayMs);
}

void AvatarFaceController::nextFace() {
  faceIndex_ = (faceIndex_ + 1) % kFaceCount;
  applyFace();
  showStatus(faceName());
  returnToDefaultAfter(kDefaultReturnDelayMs);
}

void AvatarFaceController::nextPalette(int direction) {
  paletteIndex_ = wrapIndex(paletteIndex_, direction, kPaletteCount);
  applyPalette();
  showStatus(paletteName());
  returnToDefaultAfter(kDefaultReturnDelayMs);
}

void AvatarFaceController::nextEyePattern() {
  eyePatternIndex_ = (eyePatternIndex_ + 1) % kEyePatternCount;
  applyEyePattern();
  showStatus(eyePatternName());
  returnToDefaultAfter(kDefaultReturnDelayMs);
}

void AvatarFaceController::nextTransform() {
  transformPatternIndex_ =
      (transformPatternIndex_ + 1) % kTransformPatternCount;
  applyTransform();
  showStatus(transformName());
  returnToDefaultAfter(kDefaultReturnDelayMs);
}

void AvatarFaceController::setMouthOpenRatio(float ratio) {
  avatar_.setMouthOpenRatio(constrain(ratio, 0.0f, 1.0f));
}

void AvatarFaceController::setGaze(float vertical, float horizontal) {
  vertical = constrain(vertical, -1.0f, 1.0f);
  horizontal = constrain(horizontal, -1.0f, 1.0f);
  avatar_.setRightGaze(vertical, horizontal);
  avatar_.setLeftGaze(vertical, horizontal);
}

void AvatarFaceController::showStatus(const char* text,
                                      uint32_t durationMs) {
  avatar_.setSpeechText(text);
  statusClearAt_ = durationMs == 0 ? 0 : millis() + durationMs;
}

void AvatarFaceController::pauseDrawing() {
  if (!started_ || drawingPaused_) {
    return;
  }
  avatar_.suspend();
  drawingPaused_ = true;
}

void AvatarFaceController::resumeDrawing() {
  if (!started_ || !drawingPaused_) {
    return;
  }
  avatar_.resume();
  drawingPaused_ = false;
}

void AvatarFaceController::resetToDefault() {
  showcaseEnabled_ = false;
  expressionIndex_ = 5;
  faceIndex_ = 0;
  paletteIndex_ = 0;
  eyePatternIndex_ = 0;
  transformPatternIndex_ = 0;

  avatar_.setSpeechText("");
  avatar_.setMouthOpenRatio(0.0f);
  applyFace();
  applyPalette();
  applyExpression();
  applyEyePattern();
  applyTransform();

  statusClearAt_ = 0;
  defaultReturnAt_ = 0;
}

void AvatarFaceController::returnToDefaultAfter(uint32_t delayMs) {
  defaultReturnAt_ = millis() + delayMs;
}

void AvatarFaceController::toggleShowcase() {
  showcaseEnabled_ = !showcaseEnabled_;
  defaultReturnAt_ = 0;

  if (showcaseEnabled_) {
    nextShowcaseAt_ = millis();
    showStatus("SHOWCASE ON");
  } else {
    resetToDefault();
    showStatus("SHOWCASE OFF");
  }
}

bool AvatarFaceController::isShowcaseEnabled() const {
  return showcaseEnabled_;
}

const char* AvatarFaceController::expressionName() const {
  return kExpressionNames[expressionIndex_];
}

const char* AvatarFaceController::faceName() const {
  return kFaceNames[faceIndex_];
}

const char* AvatarFaceController::paletteName() const {
  return kPaletteNames[paletteIndex_];
}

const char* AvatarFaceController::eyePatternName() const {
  return kEyePatternNames[eyePatternIndex_];
}

const char* AvatarFaceController::transformName() const {
  return kTransformNames[transformPatternIndex_];
}

void AvatarFaceController::initializeFaces() {
  faces_[0] = avatar_.getFace();
  faces_[1] = new m5avatar::SimpleFace();
  faces_[2] = new m5avatar::OmegaFace();
  faces_[3] = new m5avatar::GirlyFace();
  faces_[4] = new m5avatar::GirlyFace2();
  faces_[5] = new m5avatar::PinkDemonFace();
  faces_[6] = new m5avatar::DoggyFace();
}

void AvatarFaceController::initializePalettes() {
  palettes_[1].set(COLOR_PRIMARY, M5.Display.color24to16(0x383838));
  palettes_[1].set(COLOR_BACKGROUND, M5.Display.color24to16(0xFAC2A8));
  palettes_[1].set(COLOR_SECONDARY, TFT_PINK);

  palettes_[2].set(COLOR_PRIMARY, TFT_YELLOW);
  palettes_[2].set(COLOR_BACKGROUND, TFT_DARKCYAN);
  palettes_[2].set(COLOR_SECONDARY, TFT_CYAN);

  palettes_[3].set(COLOR_PRIMARY, TFT_DARKGREY);
  palettes_[3].set(COLOR_BACKGROUND, TFT_WHITE);
  palettes_[3].set(COLOR_SECONDARY, TFT_LIGHTGREY);

  palettes_[4].set(COLOR_PRIMARY, TFT_RED);
  palettes_[4].set(COLOR_BACKGROUND, TFT_PINK);
  palettes_[4].set(COLOR_SECONDARY, TFT_MAGENTA);
}

void AvatarFaceController::applyExpression() {
  avatar_.setExpression(kExpressions[expressionIndex_]);
}

void AvatarFaceController::applyFace() {
  avatar_.setFace(faces_[faceIndex_]);
}

void AvatarFaceController::applyPalette() {
  avatar_.setColorPalette(palettes_[paletteIndex_]);
}

void AvatarFaceController::applyEyePattern() {
  const EyePattern pattern =
      static_cast<EyePattern>(eyePatternIndex_);

  switch (pattern) {
    case EyePattern::AutoBlink:
      // Replace the library interval with a 3-10 second random scheduler.
      avatar_.setIsAutoBlink(false);
      avatar_.setEyeOpenRatio(1.0f);
      blinkClosed_ = false;
      scheduleNextBlink(millis());
      break;
    case EyePattern::Open:
      avatar_.setIsAutoBlink(false);
      avatar_.setEyeOpenRatio(1.0f);
      blinkClosed_ = false;
      break;
    case EyePattern::WinkLeft:
      avatar_.setIsAutoBlink(false);
      avatar_.setLeftEyeOpenRatio(0.0f);
      avatar_.setRightEyeOpenRatio(1.0f);
      blinkClosed_ = false;
      break;
    case EyePattern::WinkRight:
      avatar_.setIsAutoBlink(false);
      avatar_.setLeftEyeOpenRatio(1.0f);
      avatar_.setRightEyeOpenRatio(0.0f);
      blinkClosed_ = false;
      break;
    case EyePattern::Closed:
      avatar_.setIsAutoBlink(false);
      avatar_.setEyeOpenRatio(0.0f);
      blinkClosed_ = true;
      break;
  }
}

void AvatarFaceController::applyTransform() {
  const TransformPattern pattern =
      static_cast<TransformPattern>(transformPatternIndex_);

  avatar_.setScale(1.0f);
  avatar_.setRotation(0.0f);

  switch (pattern) {
    case TransformPattern::Normal:
      break;
    case TransformPattern::ZoomIn:
      avatar_.setScale(1.15f);
      break;
    case TransformPattern::ZoomOut:
      avatar_.setScale(0.85f);
      break;
    case TransformPattern::TiltLeft:
      avatar_.setRotation(-0.12f);
      break;
    case TransformPattern::TiltRight:
      avatar_.setRotation(0.12f);
      break;
  }
}

void AvatarFaceController::advanceShowcase() {
  expressionIndex_ = (expressionIndex_ + 1) % kExpressionCount;
  eyePatternIndex_ = (eyePatternIndex_ + 1) % kEyePatternCount;
  transformPatternIndex_ =
      (transformPatternIndex_ + 1) % kTransformPatternCount;

  if (expressionIndex_ == 0) {
    faceIndex_ = (faceIndex_ + 1) % kFaceCount;
    applyFace();

    if (faceIndex_ == 0) {
      paletteIndex_ = (paletteIndex_ + 1) % kPaletteCount;
      applyPalette();
    }
  }

  applyExpression();
  applyEyePattern();
  applyTransform();
  showStatus(expressionName(), 900);
}

void AvatarFaceController::updateBlink(uint32_t now) {
  if (eyePatternIndex_ !=
      static_cast<size_t>(EyePattern::AutoBlink)) {
    return;
  }

  if (!blinkClosed_ &&
      static_cast<int32_t>(now - nextBlinkAt_) >= 0) {
    avatar_.setEyeOpenRatio(0.0f);
    blinkClosed_ = true;
    blinkOpenAt_ =
        now + random(kBlinkClosedMinMs, kBlinkClosedMaxMs + 1);
    return;
  }

  if (blinkClosed_ &&
      static_cast<int32_t>(now - blinkOpenAt_) >= 0) {
    avatar_.setEyeOpenRatio(1.0f);
    blinkClosed_ = false;
    scheduleNextBlink(now);
  }
}

void AvatarFaceController::scheduleNextBlink(uint32_t now) {
  nextBlinkAt_ =
      now + random(kBlinkIntervalMinMs, kBlinkIntervalMaxMs + 1);
}
