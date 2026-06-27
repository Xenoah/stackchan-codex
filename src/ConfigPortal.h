#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String ttsHost = "192.168.1.2";
  uint16_t ttsPort = 50021;
  String ttsSpeaker = "3";
  String ttsEngineType = "voicevox_compatible";
  String speechText =
      "Hello! I am Zundamon. I can now talk using StackChan!";
};

class ConfigPortal {
 public:
  bool begin();
  void update();
  bool isPortalActive() const;
  bool isConnected() const;
  const AppConfig& config() const;
  IPAddress localIp() const;
  String accessPointName() const;
  void startSettingsAp();
  void stopSettingsAp();
  bool isSettingsApActive() const;
  IPAddress settingsApIp() const;

 private:
  Preferences preferences_;
  WebServer server_{80};
  AppConfig config_;
  bool portalActive_ = false;
  bool settingsApActive_ = false;
  String accessPointName_;

  void load();
  void save();
  bool connectWifi();
  void startPortal();
  void registerRoutes();
  void ensureAccessPointName();
  String pageHtml(const String& message = "");
  String wifiOptionsHtml();
};
