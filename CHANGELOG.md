# Changelog

このプロジェクトの主な変更を記録します。

## [Unreleased]

### Added

- 起動ごとにサーボ原点復帰を確認するタッチUI
- `NO / KEEP` 選択時はサーボを動かさず、電源・トルクOFFのまま起動を続行
- `OK / HOME` 選択時は現在角度を同期後、低速で原点 `(0, 0)` へ復帰
- 原点復帰完了後と12秒タイムアウト後にサーボを安全停止
- `CAL / ALL` によるYaw/Pitch原点設定と全可動域の段階的な低速検証
- サーボ移動中の画面タッチ緊急停止、到達誤差・タイムアウト検出
- BMI270ジャイロ校正と、300サンプル平均による水平Roll/Pitchゼロ点保存

## [β0.2.5] - 2026-06-23

M5Stack-Avatarによる顔機能の全面実装と、起動時に顔が表示されない問題を修正した
βリリースです。

### Changed

- 独自の顔描画をM5Stack-Avatar v0.10.0へ置き換え
- VOICEVOXリップシンクを `Avatar::setMouthOpenRatio()` へ統合
- Bボタンで全6表情を順番に切り替え
- Cボタンで全7顔テンプレートを順番に切り替え

### Added

- `Happy`、`Angry`、`Sad`、`Doubt`、`Sleepy`、`Neutral`
- Default、Simple、Omega、Girly、Girly2、PinkDemon、Doggy
- 自動瞬き、両目開き、左右ウィンク、両目閉じ
- Default、Skin、Cyber、Monochrome、Demon配色
- Normal、Zoom In、Zoom Out、Tilt Left、Tilt Right変形
- ランダム視線、呼吸、吹き出し
- 頭頂タップ、スワイプ、長押しによる全顔操作
- 全パターンを自動表示するショーケース
- 無操作時に標準顔・通常表情へ戻る待機状態
- 3〜10秒のランダム間隔と100〜180 msの閉眼時間による瞬き
- Wi-Fi接続待ち・設定モード中も標準顔を先に表示する起動順序
- 画面輝度の明示設定と起動段階のシリアル診断ログ
- `THIRD_PARTY_NOTICES.md`
- StackChan BSPより先にCoreS3液晶へ簡易顔を描く安全起動。周辺機器初期化で停止しても液晶状態を確認可能
- Avatar描画タスク開始前の表情設定によってsetupタスクが停止し、顔が表示されなかった初期化順不具合を修正

## [β0.1.0] - 2026-06-22

初回βリリース。

### Added

- CoreS3液晶へStackChanの顔を描画
- 通常、喜び、怒り、悲しみ、眠気の表情
- LGFX Spriteによるちらつきの少ない画面更新
- VOICEVOX Engineの `/audio_query` と `/synthesis` API連携
- VOICEVOX style ID `3` を初期値とした、ずんだもん音声
- 16-bit PCM WAVのストリーミング解析・再生
- PCM平均振幅に基づくリップシンク
- Aボタンと頭頂タッチによる発話
- RGB LEDによる準備・発話・エラー状態表示
- NVSへ保存するブラウザ設定画面
- 周辺Wi-Fiのスキャンと選択
- SSID、RSSI、電波強度、暗号化有無の表示
- 非公開SSIDの手入力
- VOICEVOX host、port、style ID、発話文章の設定
- StackChan-BSP 1.1.0
- M5CoreS3 1.0.1
- カメラ、近接・環境光、マイク、IMU、RTC、microSD用ドライバ
- サーボ、RGB、頭頂タッチ、INA226、NFC、IR用ドライバ
- Wi-Fi、BLE、USB、GPIO、UART、I2C用ライブラリ

### Safety

- 起動直後にサーボのトルクと電源を無効化
- 設定ページで保存済みWi-Fiパスワードを非表示
- パスワード欄が空の場合は保存済みパスワードを維持

### Known limitations

- 音声合成にはLAN上のVOICEVOX Engineが必要
- 音声認識、会話AI、OTAは未実装
- サーボ、カメラ、NFC、IR等はライブラリ準備段階
- β版のためAPIや設定項目は今後変更される可能性あり

[Unreleased]: https://github.com/Xenoah/stackchan-codex/compare/v0.2.5-beta.1...HEAD
[β0.2.5]: https://github.com/Xenoah/stackchan-codex/releases/tag/v0.2.5-beta.1
[β0.1.0]: https://github.com/Xenoah/stackchan-codex/releases/tag/v0.1.0-beta.1
