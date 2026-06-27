#include "ConfigPortal.h"

namespace {

// HTMLの特殊文字（<>&"）をエンティティにエスケープする。
// ユーザ入力をHTMLに埋め込む際のXSS対策として使用する。
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

// TTSエンジン種別の文字列を正規化する。
// 不正な値が来た場合はデフォルトの "voicevox_compatible" を返す。
String sanitizedTtsEngineType(const String& value) {
  return value == "simple_wav" ? "simple_wav" : "voicevox_compatible";
}

// <option> タグの selected 属性を返す（現在の値と一致する場合のみ付与）
String selectedAttribute(const String& value, const char* option) {
  if (value == option) {
    return " selected";
  }
  return "";
}

}  // namespace

bool ConfigPortal::begin() {
  load();          // NVSから設定を読み込む
  registerRoutes(); // Webサーバのルートを登録する

  // 保存済みSSIDがあればWiFi接続を試みる
  if (!config_.wifiSsid.isEmpty() && connectWifi()) {
    server_.begin(); // 接続成功: STAモードでWebサーバ起動
    return true;
  }

  // 接続失敗: セットアップ用APを起動する
  startPortal();
  return false;
}

// WebサーバのHTTPリクエストを処理する（毎フレーム呼ぶ必要がある）
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

// APモード時はsoftAPのIPを、STA接続時はDHCPで取得したIPを返す
IPAddress ConfigPortal::localIp() const {
  return portalActive_ ? WiFi.softAPIP() : WiFi.localIP();
}

String ConfigPortal::accessPointName() const {
  return accessPointName_;
}

// SETTINGSメニューを開いたときに呼ぶ。
// 既存のWiFi接続（STA）を切断せず、AP_STAモードで追加APを起動する。
// これにより、同一WiFiのデバイスからも、APに直接接続したデバイスからも
// 設定画面にアクセスできるようになる。
void ConfigPortal::startSettingsAp() {
  // セットアップAPが起動中の場合はそちらを優先（重複起動しない）
  if (settingsApActive_ || portalActive_) return;
  ensureAccessPointName();
  WiFi.mode(WIFI_AP_STA);              // STA+APの同時動作モード
  WiFi.softAP(accessPointName_.c_str(), "stackchan"); // パスワード: stackchan
  settingsApActive_ = true;
}

// SETTINGSメニューを閉じたときに呼ぶ。APを停止してSTAモードに戻す。
void ConfigPortal::stopSettingsAp() {
  if (!settingsApActive_) return;
  WiFi.softAPdisconnect(true); // APを停止し、接続中の端末を切断
  WiFi.mode(WIFI_STA);         // STAモードに戻す
  settingsApActive_ = false;
}

bool ConfigPortal::isSettingsApActive() const {
  return settingsApActive_;
}

IPAddress ConfigPortal::settingsApIp() const {
  return WiFi.softAPIP(); // 通常 192.168.4.1
}

// NVSのnamespace "stackchan" から設定を読み込む。
// 古いキー名（vv_host, vv_port 等）との後方互換性も維持する。
void ConfigPortal::load() {
  preferences_.begin("stackchan", true); // 読み取り専用で開く
  config_.wifiSsid = preferences_.getString("ssid", "");
  config_.wifiPassword = preferences_.getString("password", "");
  // 古いキー名 "vv_host" からのマイグレーションに対応
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

// 現在の設定をNVSに保存する（保存後は再起動が必要）
void ConfigPortal::save() {
  preferences_.begin("stackchan", false); // 書き込みモードで開く
  preferences_.putString("ssid", config_.wifiSsid);
  preferences_.putString("password", config_.wifiPassword);
  preferences_.putString("tts_host", config_.ttsHost);
  preferences_.putUShort("tts_port", config_.ttsPort);
  preferences_.putString("tts_speaker", config_.ttsSpeaker);
  preferences_.putString("tts_engine", config_.ttsEngineType);
  preferences_.putString("speech", config_.speechText);
  preferences_.end();
}

// 保存済みSSID/パスワードでWiFiに接続する。
// 最大15秒間接続を待ち、タイムアウトした場合はfalseを返す。
bool ConfigPortal::connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // 省電力モード無効（レイテンシ改善）
  WiFi.begin(config_.wifiSsid.c_str(), config_.wifiPassword.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

// APのSSID名を生成・保存する。MACアドレスの末尾3バイトを16進数で使用し、
// デバイスごとにユニークなSSID（例: "StackChan-Setup-A1B2C3"）を作る。
void ConfigPortal::ensureAccessPointName() {
  if (!accessPointName_.isEmpty()) return;
  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06llX",
           static_cast<unsigned long long>(chipId & 0xFFFFFF));
  accessPointName_ = "StackChan-Setup-" + String(suffix);
}

// WiFi接続失敗時のセットアップ用APを起動する。
// 既存WiFiを切断してAP_STAモードに切り替え、Webサーバを起動する。
void ConfigPortal::startPortal() {
  portalActive_ = true;
  WiFi.disconnect(true);   // 既存WiFi接続を切断
  WiFi.mode(WIFI_AP_STA);  // AP+STAモード（後でSTA接続できるよう）
  ensureAccessPointName();
  WiFi.softAP(accessPointName_.c_str(), "stackchan");
  server_.begin();
}

// WebサーバのURLルートを登録する。
// GET /  → 設定ページを返す
// POST /save → 設定を保存して再起動する
void ConfigPortal::registerRoutes() {
  server_.on("/", HTTP_GET, [this]() {
    server_.send(200, "text/html; charset=utf-8", pageHtml());
  });

  server_.on("/save", HTTP_POST, [this]() {
    // 手入力SSIDが空でなければ、ドロップダウンより優先する（非公開SSID対応）
    const String manualSsid = server_.arg("manual_ssid");
    config_.wifiSsid =
        manualSsid.isEmpty() ? server_.arg("ssid") : manualSsid;

    // パスワードは空の場合は既存値を維持する（セキュリティ上、フォームに表示しないため）
    const String submittedPassword = server_.arg("password");
    if (!submittedPassword.isEmpty()) {
      config_.wifiPassword = submittedPassword;
    }

    config_.ttsHost = server_.arg("tts_host");
    config_.ttsPort =
        constrain(server_.arg("tts_port").toInt(), 1, 65535); // 有効ポート範囲に制限
    config_.ttsSpeaker = server_.arg("speaker");
    config_.ttsSpeaker.trim(); // 前後の空白を除去
    if (config_.ttsSpeaker.isEmpty()) {
      config_.ttsSpeaker = "3"; // 空の場合はデフォルト話者IDに戻す
    }
    config_.ttsEngineType =
        sanitizedTtsEngineType(server_.arg("tts_engine"));
    config_.speechText = server_.arg("speech");
    save();

    // 保存完了メッセージを表示してから800ms後に再起動
    server_.send(
        200, "text/html; charset=utf-8",
        pageHtml("Saved. Restarting StackChan..."));
    delay(800);
    ESP.restart();
  });
}

// 設定WebページのHTML文字列を生成する。
// モバイル対応のダークテーマUIで、WiFiスキャン結果をドロップダウンに表示する。
String ConfigPortal::pageHtml(const String& message) {
  String html;
  html.reserve(5000); // 余裕を持ったバッファ確保でメモリ断片化を防ぐ
  html += F(
      "<!doctype html><html lang='en'><head>"
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

  // 保存成功などのメッセージがあれば表示する
  if (!message.isEmpty()) {
    html += "<p>" + htmlEscape(message) + "</p>";
  }

  html += F(
      "<form method='post' action='/save'><label>Wi-Fi SSID"
      "<select name='ssid' style='box-sizing:border-box;width:100%;"
      "padding:10px;margin-top:5px;border-radius:8px;border:1px solid #555;"
      "background:#222;color:#fff'>");
  html += wifiOptionsHtml(); // WiFiスキャン結果を埋め込む
  html += F(
      "</select></label><p><a href='/' style='color:#8fd3ff'>"
      "Rescan nearby Wi-Fi</a></p>"
      "<label>Enter SSID manually (for hidden networks)"
      "<input name='manual_ssid' placeholder='Takes priority when filled'>");
  html += F("</label><label>Wi-Fi Password");
  html += F(
      "<input type='password' name='password' "
      "placeholder='Leave blank to keep current'>");
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
  html += F("</label><label>Speaker / Style ID (Zundamon Normal: 3)");
  html += "<input name='speaker' value='" +
          htmlEscape(config_.ttsSpeaker) + "'>";
  html += F("</label><label>Text to speak (A button)");
  html += "<textarea name='speech' rows='4'>" +
          htmlEscape(config_.speechText) + "</textarea>";
  html += F("</label><button type='submit'>Save &amp; Restart</button></form>"
            "<p>Configure your TTS server to accept HTTP connections "
            "from the same LAN.</p></body></html>");
  return html;
}

// 周辺のWiFiをスキャンして<option>タグのリストを生成する。
// 重複SSIDを除外し、電波強度・セキュリティ情報を付記する。
String ConfigPortal::wifiOptionsHtml() {
  String options;
  options.reserve(2000);

  // false=非同期スキャン無効（同期スキャン）, true=隠しSSIDを含む
  const int count = WiFi.scanNetworks(false, true);
  if (count <= 0) {
    options += F("<option value=''>No Wi-Fi networks found</option>");
    WiFi.scanDelete();
    return options;
  }

  bool currentSsidFound = false;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) {
      continue; // 隠しSSIDはスキップ（SSIDが空で返ってくる場合）
    }

    // 同じSSIDが複数のAPで見つかった場合は最初の1件のみ表示する
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

    // 電波強度をRSSI(dBm)で分類: -55以上=strong, -70以上=medium, それ以下=weak
    const int32_t rssi = WiFi.RSSI(i);
    const char* strength =
        rssi >= -55 ? "strong" : (rssi >= -70 ? "medium" : "weak");
    const bool secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    const bool selected = ssid == config_.wifiSsid; // 現在の接続先をデフォルト選択
    currentSsidFound |= selected;

    options += "<option value='" + htmlEscape(ssid) + "'";
    if (selected) {
      options += F(" selected");
    }
    options += ">" + htmlEscape(ssid) + " (" + strength + ", " +
               String(rssi) + " dBm";
    if (secured) {
      options += F(", secured");
    }
    options += F(")</option>");
  }

  // 保存済みSSIDがスキャン結果に含まれない場合（圏外など）は末尾に追加する
  if (!config_.wifiSsid.isEmpty() && !currentSsidFound) {
    options += "<option value='" + htmlEscape(config_.wifiSsid) +
               "' selected>" + htmlEscape(config_.wifiSsid) +
               " (saved)</option>";
  }

  WiFi.scanDelete(); // スキャン結果のメモリを解放する
  return options;
}
