#include <M5Unified.h>
#include <M5GFX.h>

LGFX_Sprite face(&M5.Display);

enum FaceEmotion {
  FACE_NEUTRAL,
  FACE_HAPPY,
  FACE_ANGRY,
  FACE_SAD,
  FACE_SLEEPY
};

static int mapRange(int v, int inMin, int inMax, int outMin, int outMax) {
  v = constrain(v, inMin, inMax);
  return (v - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

void drawEye(int cx, int cy, int size, int lidWeight, int tilt) {
  const uint16_t FG = 0xFFFF;  // white
  const uint16_t BG = 0x0000;  // black

  int r = size / 2;

  // 目玉
  face.fillCircle(cx, cy, r, FG);

  // まぶた。lidWeight=100で全開、低いほど眠い
  int cover = mapRange(100 - lidWeight, 0, 100, 0, size + 4);
  if (cover > 0) {
    face.fillRect(cx - r - 2, cy - r - 2, size + 4, cover, BG);
  }

  // 感情用の斜めまぶた
  if (tilt > 0) {
    face.fillTriangle(cx - r - 2, cy - r - 2,
                      cx + r + 2, cy - r - 2,
                      cx + r + 2, cy - r + tilt, BG);
  } else if (tilt < 0) {
    int t = -tilt;
    face.fillTriangle(cx - r - 2, cy - r - 2,
                      cx + r + 2, cy - r - 2,
                      cx - r - 2, cy - r + t, BG);
  }
}

void drawMouth(int cx, int cy, int open) {
  const uint16_t FG = 0xFFFF;

  open = constrain(open, 0, 100);

  // 公式寄せ: 閉じ口は横長、開くと少し狭く縦に広がる
  int w = mapRange(open, 0, 100, 90, 60);
  int h = mapRange(open, 0, 100, 6, 50);
  int r = mapRange(open, 0, 100, 0, 16);

  face.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, FG);
}

void drawStackChanFace(FaceEmotion emotion, int lookX, int lookY,
                       int mouthOpen) {
  const uint16_t BG = 0x0000;

  int W = M5.Display.width();
  int H = M5.Display.height();
  int cx = W / 2;
  int cy = H / 2;

  // 視線オフセット。-100〜100で指定
  int ox = mapRange(lookX, -100, 100, -16, 16);
  int oy = mapRange(lookY, -100, 100, -16, 16);

  int eyeSize = 20;
  int lidWeight = 100;
  int tilt = 0;

  switch (emotion) {
    case FACE_HAPPY:
      lidWeight = 72;
      tilt = 8;
      mouthOpen = max(mouthOpen, 35);
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
      tilt = 0;
      mouthOpen = 0;
      break;

    case FACE_NEUTRAL:
    default:
      lidWeight = 100;
      tilt = 0;
      break;
  }

  face.fillScreen(BG);

  // 目。中心から左右70、上に16
  drawEye(cx - 70 + ox, cy - 16 + oy, eyeSize, lidWeight, tilt);
  drawEye(cx + 70 + ox, cy - 16 + oy, eyeSize, lidWeight, -tilt);

  // 口。中心から下に26
  drawMouth(cx + ox, cy + 26 + oy, mouthOpen);

  face.pushSprite(0, 0);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // 縦向きになっていたら横向きへ
  if (M5.Display.width() < M5.Display.height()) {
    M5.Display.setRotation(1);
  }

  face.setColorDepth(16);
  face.createSprite(M5.Display.width(), M5.Display.height());

  drawStackChanFace(FACE_NEUTRAL, 0, 0, 0);
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    drawStackChanFace(FACE_HAPPY, 0, 0, 60);
  }

  if (M5.BtnB.wasPressed()) {
    drawStackChanFace(FACE_SLEEPY, 0, 0, 0);
  }

  if (M5.BtnC.wasPressed()) {
    drawStackChanFace(FACE_NEUTRAL, 0, 0, 0);
  }
}
