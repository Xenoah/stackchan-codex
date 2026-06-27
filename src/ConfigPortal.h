#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

// アプリ全体で共有する設定値。NVSから読み込み・保存される。
struct AppConfig {
  String wifiSsid;                              // 接続先WiFiのSSID
  String wifiPassword;                          // WiFiパスワード
  String ttsHost = "192.168.1.2";              // TTSサーバのIPアドレス
  uint16_t ttsPort = 50021;                    // TTSサーバのポート番号
  String ttsSpeaker = "3";                     // 話者ID（VoiceVox: ずんだもんノーマル=3）
  String ttsEngineType = "voicevox_compatible"; // TTSエンジン種別
  String speechText =
      "Hello! I am Zundamon. I can now talk using StackChan!"; // Aボタンで話すデフォルトテキスト
};

// WiFi接続・設定用Webサーバを管理するクラス。
//
// 動作フロー:
//   begin() 呼び出し時、保存済みSSIDで接続を試みる。
//   - 接続成功 → STAモードのままWebサーバ起動（ポート80）
//   - 接続失敗 → APモード（StackChan-Setup-XXXXXX）でWebサーバ起動
//
// 設定画面はブラウザでアクセスし、保存時に再起動する。
class ConfigPortal {
 public:
  // NVSから設定を読み込み、WiFi接続またはAPを起動する。
  // 戻り値: WiFi接続成功=true、APモード=false
  bool begin();

  // Webサーバのリクエストを処理する（メインループから毎フレーム呼ぶ）
  void update();

  // セットアップ用APが起動中かどうか
  bool isPortalActive() const;

  // WiFiに接続中かどうか
  bool isConnected() const;

  // 現在の設定値を返す
  const AppConfig& config() const;

  // デバイスのIPアドレスを返す（AP時は192.168.4.1、STA時はDHCPアドレス）
  IPAddress localIp() const;

  // セットアップ用APのSSID名を返す（例: "StackChan-Setup-AABBCC"）
  String accessPointName() const;

  // SETTINGSメニュー用APを追加起動する（既存WiFi接続を維持したまま）
  // WIFI_AP_STAモードでAPを起動し、192.168.4.1でWebUIにアクセス可能にする
  void startSettingsAp();

  // SETTINGSメニュー用APを停止し、STAモードに戻す
  void stopSettingsAp();

  // SETTINGSメニュー用APが起動中かどうか
  bool isSettingsApActive() const;

  // APのIPアドレスを返す（通常 192.168.4.1）
  IPAddress settingsApIp() const;

 private:
  Preferences preferences_;          // ESP32 NVS（不揮発ストレージ）アクセス
  WebServer server_{80};             // ポート80のHTTPサーバ
  AppConfig config_;                 // 現在の設定値
  bool portalActive_ = false;        // セットアップAPが起動中かどうか
  bool settingsApActive_ = false;    // SETTINGSメニュー用APが起動中かどうか
  String accessPointName_;           // APのSSID名（例: "StackChan-Setup-AABBCC"）

  // NVSから設定を読み込む
  void load();

  // NVSに設定を保存する
  void save();

  // 保存済みSSID/パスワードでWiFi接続を試みる（タイムアウト15秒）
  bool connectWifi();

  // セットアップ用APを起動する（接続失敗時）
  void startPortal();

  // APのSSID名を初期化する（未設定の場合のみ、MACアドレス末尾6桁を使用）
  void ensureAccessPointName();

  // WebサーバのURLルートを登録する（GET /、POST /save）
  void registerRoutes();

  // 設定WebページのHTMLを生成する
  String pageHtml(const String& message = "");

  // WiFiスキャン結果を<option>タグのリストとして返す
  String wifiOptionsHtml();
};
