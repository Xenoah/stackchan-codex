#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String voiceVoxHost = "192.168.1.2";
  uint16_t voiceVoxPort = 50021;
  int voiceVoxSpeaker = 3;
  String speechText =
      "こんにちは。ぼく、ずんだもんなのだ。スタックチャンでお話しできるようになったのだ。";
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

 private:
  Preferences preferences_;
  WebServer server_{80};
  AppConfig config_;
  bool portalActive_ = false;
  String accessPointName_;

  void load();
  void save();
  bool connectWifi();
  void startPortal();
  void registerRoutes();
  String pageHtml(const String& message = "");
  String wifiOptionsHtml();
};
