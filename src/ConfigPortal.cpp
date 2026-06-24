#include "ConfigPortal.h"

namespace {

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&':
        escaped += F("&amp;");
        break;
      case '<':
        escaped += F("&lt;");
        break;
      case '>':
        escaped += F("&gt;");
        break;
      case '"':
        escaped += F("&quot;");
        break;
      default:
        escaped += value[i];
        break;
    }
  }
  return escaped;
}

String sanitizedTtsEngineType(const String& value) {
  return value == "simple_wav" ? "simple_wav" : "voicevox_compatible";
}

String selectedAttribute(const String& value, const char* option) {
  if (value == option) {
    return " selected";
  }
  return "";
}

}  // namespace

bool ConfigPortal::begin() {
  load();
  registerRoutes();

  if (!config_.wifiSsid.isEmpty() && connectWifi()) {
    server_.begin();
    return true;
  }

  startPortal();
  return false;
}

void ConfigPortal::update() {
  server_.handleClient();
}

bool ConfigPortal::isPortalActive() const {
  return portalActive_;
}

bool ConfigPortal::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

const AppConfig& ConfigPortal::config() const {
  return config_;
}

IPAddress ConfigPortal::localIp() const {
  return portalActive_ ? WiFi.softAPIP() : WiFi.localIP();
}

String ConfigPortal::accessPointName() const {
  return accessPointName_;
}

void ConfigPortal::load() {
  preferences_.begin("stackchan", true);
  config_.wifiSsid = preferences_.getString("ssid", "");
  config_.wifiPassword = preferences_.getString("password", "");
  config_.ttsHost = preferences_.getString(
      "tts_host", preferences_.getString("vv_host", config_.ttsHost));
  config_.ttsPort = preferences_.getUShort(
      "tts_port", preferences_.getUShort("vv_port", config_.ttsPort));
  config_.ttsSpeaker = preferences_.getString(
      "tts_speaker",
      String(preferences_.getInt("vv_speaker", config_.ttsSpeaker.toInt())));
  config_.ttsEngineType = sanitizedTtsEngineType(
      preferences_.getString("tts_engine", config_.ttsEngineType));
  config_.speechText =
      preferences_.getString("speech", config_.speechText);
  preferences_.end();
}

void ConfigPortal::save() {
  preferences_.begin("stackchan", false);
  preferences_.putString("ssid", config_.wifiSsid);
  preferences_.putString("password", config_.wifiPassword);
  preferences_.putString("tts_host", config_.ttsHost);
  preferences_.putUShort("tts_port", config_.ttsPort);
  preferences_.putString("tts_speaker", config_.ttsSpeaker);
  preferences_.putString("tts_engine", config_.ttsEngineType);
  preferences_.putString("speech", config_.speechText);
  preferences_.end();
}

bool ConfigPortal::connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(config_.wifiSsid.c_str(), config_.wifiPassword.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

void ConfigPortal::startPortal() {
  portalActive_ = true;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);

  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06llX",
           static_cast<unsigned long long>(chipId & 0xFFFFFF));
  accessPointName_ = "StackChan-Setup-" + String(suffix);

  WiFi.softAP(accessPointName_.c_str(), "stackchan");
  server_.begin();
}

void ConfigPortal::registerRoutes() {
  server_.on("/", HTTP_GET, [this]() {
    server_.send(200, "text/html; charset=utf-8", pageHtml());
  });

  server_.on("/save", HTTP_POST, [this]() {
    const String manualSsid = server_.arg("manual_ssid");
    config_.wifiSsid =
        manualSsid.isEmpty() ? server_.arg("ssid") : manualSsid;
    const String submittedPassword = server_.arg("password");
    if (!submittedPassword.isEmpty()) {
      config_.wifiPassword = submittedPassword;
    }
    config_.ttsHost = server_.arg("tts_host");
    config_.ttsPort =
        constrain(server_.arg("tts_port").toInt(), 1, 65535);
    config_.ttsSpeaker = server_.arg("speaker");
    config_.ttsSpeaker.trim();
    if (config_.ttsSpeaker.isEmpty()) {
      config_.ttsSpeaker = "3";
    }
    config_.ttsEngineType =
        sanitizedTtsEngineType(server_.arg("tts_engine"));
    config_.speechText = server_.arg("speech");
    save();

    server_.send(
        200, "text/html; charset=utf-8",
        pageHtml("保存しました。StackChanを再起動します。"));
    delay(800);
    ESP.restart();
  });
}

String ConfigPortal::pageHtml(const String& message) {
  String html;
  html.reserve(5000);
  html += F(
      "<!doctype html><html lang='ja'><head>"
      "<meta charset='utf-8'><meta name='viewport' "
      "content='width=device-width,initial-scale=1'>"
      "<title>StackChan Setup</title><style>"
      "body{font-family:sans-serif;max-width:640px;margin:24px auto;"
      "padding:0 16px;background:#111;color:#eee}"
      "label{display:block;margin-top:14px}input,textarea{box-sizing:border-box;"
      "width:100%;padding:10px;margin-top:5px;border-radius:8px;border:1px "
      "solid #555;background:#222;color:#fff}button{margin-top:20px;padding:"
      "12px 20px;border:0;border-radius:9px;background:#76d275;color:#111;"
      "font-weight:bold}</style></head><body><h1>StackChan Setup</h1>");

  if (!message.isEmpty()) {
    html += "<p>" + htmlEscape(message) + "</p>";
  }

  html += F(
      "<form method='post' action='/save'><label>Wi-Fi SSID"
      "<select name='ssid' style='box-sizing:border-box;width:100%;"
      "padding:10px;margin-top:5px;border-radius:8px;border:1px solid #555;"
      "background:#222;color:#fff'>");
  html += wifiOptionsHtml();
  html += F(
      "</select></label><p><a href='/' style='color:#8fd3ff'>"
      "周辺Wi-Fiを再スキャン</a></p>"
      "<label>SSIDを手入力（非公開SSIDなど）"
      "<input name='manual_ssid' placeholder='入力時はこちらを優先'>");
  html += F("</label><label>Wi-Fi password");
  html += F(
      "<input type='password' name='password' "
      "placeholder='変更しない場合は空欄'>");
  html += F("</label><label>TTS Engine Type");
  html += F(
      "<select name='tts_engine' style='box-sizing:border-box;width:100%;"
      "padding:10px;margin-top:5px;border-radius:8px;border:1px solid #555;"
      "background:#222;color:#fff'>");
  html += "<option value='voicevox_compatible'" +
          selectedAttribute(config_.ttsEngineType,
                            "voicevox_compatible") +
          ">voicevox_compatible</option>";
  html += "<option value='simple_wav'" +
          selectedAttribute(config_.ttsEngineType, "simple_wav") +
          ">simple_wav</option>";
  html += F("</select>");
  html += F("</label><label>TTS Host");
  html += "<input name='tts_host' value='" +
          htmlEscape(config_.ttsHost) + "'>";
  html += F("</label><label>TTS Port");
  html += "<input type='number' name='tts_port' value='" +
          String(config_.ttsPort) + "'>";
  html += F("</label><label>Speaker / Style ID（ずんだもん ノーマル: 3）");
  html += "<input name='speaker' value='" +
          htmlEscape(config_.ttsSpeaker) + "'>";
  html += F("</label><label>Aボタンで話す文章");
  html += "<textarea name='speech' rows='4'>" +
          htmlEscape(config_.speechText) + "</textarea>";
  html += F("</label><button type='submit'>保存して再起動</button></form>"
            "<p>TTSサーバーは同じLANからHTTP接続できるように"
            "待ち受ける設定にしてください。</p></body></html>");
  return html;
}

String ConfigPortal::wifiOptionsHtml() {
  String options;
  options.reserve(2000);

  const int count = WiFi.scanNetworks(false, true);
  if (count <= 0) {
    options += F("<option value=''>Wi-Fiが見つかりません</option>");
    WiFi.scanDelete();
    return options;
  }

  bool currentSsidFound = false;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) {
      continue;
    }

    bool duplicate = false;
    for (int previous = 0; previous < i; ++previous) {
      if (WiFi.SSID(previous) == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    const int32_t rssi = WiFi.RSSI(i);
    const char* strength =
        rssi >= -55 ? "強" : (rssi >= -70 ? "中" : "弱");
    const bool secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    const bool selected = ssid == config_.wifiSsid;
    currentSsidFound |= selected;

    options += "<option value='" + htmlEscape(ssid) + "'";
    if (selected) {
      options += F(" selected");
    }
    options += ">" + htmlEscape(ssid) + " (" + strength + ", " +
               String(rssi) + " dBm";
    if (secured) {
      options += F(", 鍵あり");
    }
    options += F(")</option>");
  }

  if (!config_.wifiSsid.isEmpty() && !currentSsidFound) {
    options += "<option value='" + htmlEscape(config_.wifiSsid) +
               "' selected>" + htmlEscape(config_.wifiSsid) +
               " (保存済み)</option>";
  }

  WiFi.scanDelete();
  return options;
}
