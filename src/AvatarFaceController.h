#pragma once

#include <Arduino.h>
#include <Avatar.h>
#include <faces/FaceTemplates.hpp>

class AvatarFaceController {
 public:
  enum class EyePattern : uint8_t {
    AutoBlink,
    Open,
    WinkLeft,
    WinkRight,
    Closed
  };

  enum class TransformPattern : uint8_t {
    Normal,
    ZoomIn,
    ZoomOut,
    TiltLeft,
    TiltRight
  };

  void begin();
  void update();

  void setExpression(m5avatar::Expression expression);
  void nextExpression();
  void nextFace();
  void nextPalette(int direction = 1);
  void nextEyePattern();
  void nextTransform();

  void setMouthOpenRatio(float ratio);
  void setGaze(float vertical, float horizontal);
  void showStatus(const char* text, uint32_t durationMs = 1400);

  void toggleShowcase();
  bool isShowcaseEnabled() const;

  const char* expressionName() const;
  const char* faceName() const;
  const char* paletteName() const;
  const char* eyePatternName() const;
  const char* transformName() const;

 private:
  static constexpr size_t kExpressionCount = 6;
  static constexpr size_t kFaceCount = 7;
  static constexpr size_t kPaletteCount = 5;
  static constexpr size_t kEyePatternCount = 5;
  static constexpr size_t kTransformPatternCount = 5;

  m5avatar::Avatar avatar_;
  m5avatar::Face* faces_[kFaceCount] = {};
  m5avatar::ColorPalette palettes_[kPaletteCount];

  size_t expressionIndex_ = 5;
  size_t faceIndex_ = 0;
  size_t paletteIndex_ = 0;
  size_t eyePatternIndex_ = 0;
  size_t transformPatternIndex_ = 0;

  bool started_ = false;
  bool showcaseEnabled_ = false;
  uint32_t nextShowcaseAt_ = 0;
  uint32_t statusClearAt_ = 0;

  void initializeFaces();
  void initializePalettes();
  void applyExpression();
  void applyFace();
  void applyPalette();
  void applyEyePattern();
  void applyTransform();
  void advanceShowcase();
};
