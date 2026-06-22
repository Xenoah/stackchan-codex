#include <Arduino.h>
#include <M5Unified.h>

namespace {

constexpr uint32_t kBackgroundColor = TFT_BLACK;
constexpr uint32_t kFaceColor = TFT_WHITE;
constexpr uint32_t kBlinkIntervalMs = 3000;
constexpr uint32_t kBlinkDurationMs = 140;

uint32_t nextBlinkAt = 0;
bool blinking = false;

void drawFace(bool eyesClosed) {
  auto& display = M5.Display;
  const int32_t centerX = display.width() / 2;
  const int32_t centerY = display.height() / 2;
  const int32_t eyeY = centerY - 30;
  const int32_t eyeOffsetX = 58;

  display.startWrite();
  display.fillScreen(kBackgroundColor);

  if (eyesClosed) {
    display.fillRoundRect(centerX - eyeOffsetX - 27, eyeY - 4, 54, 8, 4,
                          kFaceColor);
    display.fillRoundRect(centerX + eyeOffsetX - 27, eyeY - 4, 54, 8, 4,
                          kFaceColor);
  } else {
    display.fillEllipse(centerX - eyeOffsetX, eyeY, 19, 30, kFaceColor);
    display.fillEllipse(centerX + eyeOffsetX, eyeY, 19, 30, kFaceColor);
  }

  // A small, friendly smile.
  display.drawEllipseArc(centerX, centerY + 27, 43, 34, 28, 22, 25, 155,
                         kFaceColor);
  display.endWrite();
}

}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);

  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
  drawFace(false);

  nextBlinkAt = millis() + kBlinkIntervalMs;
}

void loop() {
  M5.update();

  const uint32_t now = millis();
  if (!blinking && static_cast<int32_t>(now - nextBlinkAt) >= 0) {
    blinking = true;
    drawFace(true);
    nextBlinkAt = now + kBlinkDurationMs;
  } else if (blinking && static_cast<int32_t>(now - nextBlinkAt) >= 0) {
    blinking = false;
    drawFace(false);
    nextBlinkAt = now + kBlinkIntervalMs;
  }

  delay(10);
}
