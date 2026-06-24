#include "VoiceVoxClient.h"

#include <HTTPClient.h>
#include <M5Unified.h>
#include <WiFiClient.h>

namespace {

constexpr size_t kAudioBufferCount = 3;
constexpr size_t kAudioBufferSize = 2048;
constexpr uint32_t kNetworkTimeoutMs = 30000;

alignas(4) uint8_t audioBuffers[kAudioBufferCount][kAudioBufferSize];

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

}  // namespace

void TtsClient::setCallbacks(LipSyncCallback lipSync,
                             ServiceCallback serviceCallback) {
  lipSync_ = lipSync;
  service_ = serviceCallback;
}

bool TtsClient::speak(const TtsConfig& config, const String& text) {
  lastError_ = "";
  if (WiFi.status() != WL_CONNECTED) {
    lastError_ = "Wi-Fi is not connected";
    return false;
  }

  if (config.engineType == TtsEngineType::SimpleWav) {
    return speakSimpleWav(config, text);
  }
  return speakVoiceVoxCompatible(config, text);
}

bool TtsClient::speakVoiceVoxCompatible(
    const TtsConfig& config, const String& text) {
  const String speaker = urlEncode(config.speaker);
  const String queryUrl =
      endpoint(config, "/audio_query") + "?speaker=" + speaker +
      "&text=" + urlEncode(text);

  HTTPClient queryHttp;
  queryHttp.setConnectTimeout(kNetworkTimeoutMs);
  queryHttp.setTimeout(kNetworkTimeoutMs);
  if (!queryHttp.begin(queryUrl)) {
    lastError_ = "audio_query begin failed";
    return false;
  }

  const int queryStatus = queryHttp.POST("");
  if (queryStatus != HTTP_CODE_OK) {
    lastError_ = "audio_query HTTP " + String(queryStatus);
    queryHttp.end();
    return false;
  }

  String audioQuery = queryHttp.getString();
  queryHttp.end();
  if (audioQuery.isEmpty()) {
    lastError_ = "audio_query returned empty JSON";
    return false;
  }

  const String synthesisUrl =
      endpoint(config, "/synthesis") + "?speaker=" + speaker;
  HTTPClient synthesisHttp;
  synthesisHttp.setConnectTimeout(kNetworkTimeoutMs);
  synthesisHttp.setTimeout(kNetworkTimeoutMs);
  if (!synthesisHttp.begin(synthesisUrl)) {
    lastError_ = "synthesis begin failed";
    return false;
  }

  synthesisHttp.addHeader("Content-Type", "application/json");
  const int synthesisStatus = synthesisHttp.POST(
      reinterpret_cast<uint8_t*>(
          const_cast<char*>(audioQuery.c_str())),
      audioQuery.length());
  audioQuery.clear();

  if (synthesisStatus != HTTP_CODE_OK) {
    lastError_ = "synthesis HTTP " + String(synthesisStatus);
    synthesisHttp.end();
    return false;
  }

  const bool played = playWavStream(synthesisHttp);
  synthesisHttp.end();
  return played;
}

bool TtsClient::speakSimpleWav(
    const TtsConfig& config, const String& text) {
  HTTPClient synthesisHttp;
  synthesisHttp.setConnectTimeout(kNetworkTimeoutMs);
  synthesisHttp.setTimeout(kNetworkTimeoutMs);
  if (!synthesisHttp.begin(endpoint(config, "/synthesis"))) {
    lastError_ = "simple_wav synthesis begin failed";
    return false;
  }

  synthesisHttp.addHeader("Content-Type", "text/plain; charset=utf-8");
  synthesisHttp.addHeader("Accept", "audio/wav");
  const int synthesisStatus = synthesisHttp.POST(
      reinterpret_cast<uint8_t*>(const_cast<char*>(text.c_str())),
      text.length());

  if (synthesisStatus != HTTP_CODE_OK) {
    lastError_ = "simple_wav synthesis HTTP " + String(synthesisStatus);
    synthesisHttp.end();
    return false;
  }

  const bool played = playWavStream(synthesisHttp);
  synthesisHttp.end();
  return played;
}

const String& TtsClient::lastError() const {
  return lastError_;
}

String TtsClient::endpoint(const TtsConfig& config,
                           const char* path) const {
  return "http://" + config.host + ":" + String(config.port) + path;
}

String TtsClient::urlEncode(const String& value) const {
  static const char hex[] = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(value.length() * 3);

  const uint8_t* bytes =
      reinterpret_cast<const uint8_t*>(value.c_str());
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = bytes[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[c >> 4];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

bool TtsClient::playWavStream(HTTPClient& http) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    lastError_ = "synthesis stream unavailable";
    return false;
  }

  uint8_t riffHeader[12];
  if (!readExact(*stream, riffHeader, sizeof(riffHeader),
                 kNetworkTimeoutMs) ||
      memcmp(riffHeader, "RIFF", 4) != 0 ||
      memcmp(riffHeader + 8, "WAVE", 4) != 0) {
    lastError_ = "invalid WAV RIFF header";
    return false;
  }

  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t sampleRate = 0;
  uint32_t dataLength = 0;

  while (dataLength == 0) {
    uint8_t chunkHeader[8];
    if (!readExact(*stream, chunkHeader, sizeof(chunkHeader),
                   kNetworkTimeoutMs)) {
      lastError_ = "WAV chunk header timeout";
      return false;
    }

    const uint32_t chunkLength = readLe32(chunkHeader + 4);
    if (memcmp(chunkHeader, "fmt ", 4) == 0) {
      if (chunkLength < 16) {
        lastError_ = "invalid WAV fmt chunk";
        return false;
      }

      uint8_t format[16];
      if (!readExact(*stream, format, sizeof(format), kNetworkTimeoutMs)) {
        lastError_ = "WAV fmt timeout";
        return false;
      }

      audioFormat = readLe16(format);
      channels = readLe16(format + 2);
      sampleRate = readLe32(format + 4);
      bitsPerSample = readLe16(format + 14);

      if (chunkLength > sizeof(format) &&
          !skipBytes(*stream, chunkLength - sizeof(format),
                     kNetworkTimeoutMs)) {
        lastError_ = "WAV fmt extension timeout";
        return false;
      }
    } else if (memcmp(chunkHeader, "data", 4) == 0) {
      dataLength = chunkLength;
    } else if (!skipBytes(*stream, chunkLength, kNetworkTimeoutMs)) {
      lastError_ = "WAV chunk skip timeout";
      return false;
    }

    if ((chunkLength & 1) != 0 && dataLength == 0 &&
        !skipBytes(*stream, 1, kNetworkTimeoutMs)) {
      lastError_ = "WAV padding timeout";
      return false;
    }
  }

  if (audioFormat != 1 || bitsPerSample != 16 ||
      (channels != 1 && channels != 2) || sampleRate == 0) {
    lastError_ = "unsupported WAV format";
    return false;
  }

  size_t bufferIndex = 0;
  uint32_t remaining = dataLength;
  while (remaining > 0) {
    size_t readLength = min<size_t>(remaining, kAudioBufferSize);
    readLength &= ~static_cast<size_t>(1);
    if (readLength == 0 ||
        !readExact(*stream, audioBuffers[bufferIndex], readLength,
                   kNetworkTimeoutMs)) {
      lastError_ = "WAV audio stream timeout";
      return false;
    }

    const int16_t* samples =
        reinterpret_cast<const int16_t*>(audioBuffers[bufferIndex]);
    const size_t sampleCount = readLength / sizeof(int16_t);

    if (lipSync_ != nullptr) {
      uint64_t amplitude = 0;
      for (size_t i = 0; i < sampleCount; i += channels) {
        const int32_t sample = samples[i];
        amplitude += sample < 0 ? -sample : sample;
      }
      const size_t frameCount = max<size_t>(1, sampleCount / channels);
      const int average = static_cast<int>(amplitude / frameCount);
      const int level = constrain(
          (average - 180) * 100 / (5200 - 180), 0, 100);
      lipSync_(level);
    }

    M5.Speaker.playRaw(samples, sampleCount, sampleRate, channels == 2, 1,
                       0, false);
    remaining -= readLength;
    bufferIndex = (bufferIndex + 1) % kAudioBufferCount;
    service();
  }

  while (M5.Speaker.isPlaying()) {
    service();
    delay(5);
  }

  if (lipSync_ != nullptr) {
    lipSync_(0);
  }
  return true;
}

bool TtsClient::readExact(WiFiClient& stream, uint8_t* destination,
                          size_t length, uint32_t timeoutMs) {
  size_t offset = 0;
  uint32_t lastProgress = millis();

  while (offset < length) {
    const int available = stream.available();
    if (available > 0) {
      const size_t count =
          min<size_t>(length - offset, static_cast<size_t>(available));
      const int received = stream.read(destination + offset, count);
      if (received > 0) {
        offset += received;
        lastProgress = millis();
      }
    } else if (!stream.connected()) {
      return false;
    } else if (millis() - lastProgress >= timeoutMs) {
      return false;
    }

    service();
    delay(1);
  }
  return true;
}

bool TtsClient::skipBytes(WiFiClient& stream, size_t length,
                          uint32_t timeoutMs) {
  uint8_t scratch[64];
  while (length > 0) {
    const size_t count = min(length, sizeof(scratch));
    if (!readExact(stream, scratch, count, timeoutMs)) {
      return false;
    }
    length -= count;
  }
  return true;
}

void TtsClient::service() {
  if (service_ != nullptr) {
    service_();
  }
}
