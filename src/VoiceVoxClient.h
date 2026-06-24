#pragma once

#include <Arduino.h>

enum class TtsEngineType {
  VoiceVoxCompatible,
  SimpleWav,
};

struct TtsConfig {
  String host;
  uint16_t port = 50021;
  String speaker = "3";
  TtsEngineType engineType = TtsEngineType::VoiceVoxCompatible;
};

using LipSyncCallback = void (*)(int level);
using ServiceCallback = void (*)();

class TtsClient {
 public:
  void setCallbacks(LipSyncCallback lipSync, ServiceCallback service);
  bool speak(const TtsConfig& config, const String& text);
  const String& lastError() const;

 private:
  LipSyncCallback lipSync_ = nullptr;
  ServiceCallback service_ = nullptr;
  String lastError_;

  String endpoint(const TtsConfig& config, const char* path) const;
  String urlEncode(const String& value) const;
  bool speakVoiceVoxCompatible(const TtsConfig& config, const String& text);
  bool speakSimpleWav(const TtsConfig& config, const String& text);
  bool playWavStream(class HTTPClient& http);
  bool readExact(class WiFiClient& stream, uint8_t* destination, size_t length,
                 uint32_t timeoutMs);
  bool skipBytes(class WiFiClient& stream, size_t length, uint32_t timeoutMs);
  void service();
};

using VoiceVoxClient = TtsClient;
