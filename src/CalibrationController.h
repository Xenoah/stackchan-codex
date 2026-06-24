#pragma once

#include <Arduino.h>

class AvatarFaceController;

struct CalibrationData {
  bool servoValid = false;
  int yawMin = 0;
  int yawMax = 0;
  int pitchMin = 0;
  int pitchMax = 0;

  bool imuLevelValid = false;
  float levelRollDeg = 0.0f;
  float levelPitchDeg = 0.0f;
};

class CalibrationController {
 public:
  void load();
  bool run(AvatarFaceController& avatarFace);
  const CalibrationData& data() const;

 private:
  CalibrationData data_;

  bool calibrateServos();
  bool calibrateImuLevel();
  bool moveAndVerify(
      const char* title, int yaw, int pitch, int* measuredYaw,
      int* measuredPitch, bool requireTarget);
  void saveServoCalibration();
  void saveImuCalibration();
  void stopServos();
};
