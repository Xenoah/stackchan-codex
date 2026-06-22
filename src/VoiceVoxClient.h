#pragma once

#include <Arduino.h>

struct VoiceVoxConfig {
  String host;
  uint16_t port = 50021;
  int speaker = 3;
};

using LipSyncCallback = void (*)(int level);
using ServiceCallback = void (*)();

class VoiceVoxClient {
 public:
  void setCallbacks(LipSyncCallback lipSync, ServiceCallback service);
  bool speak(const VoiceVoxConfig& config, const String& text);
  const String& lastError() const;

 private:
  LipSyncCallback lipSync_ = nullptr;
  ServiceCallback service_ = nullptr;
  String lastError_;

  String endpoint(const VoiceVoxConfig& config, const char* path) const;
  String urlEncode(const String& value) const;
  bool playWavStream(class HTTPClient& http);
  bool readExact(class WiFiClient& stream, uint8_t* destination, size_t length,
                 uint32_t timeoutMs);
  bool skipBytes(class WiFiClient& stream, size_t length, uint32_t timeoutMs);
  void service();
};
