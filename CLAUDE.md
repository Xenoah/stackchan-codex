# StackChan × Termux Local LLM Gateway

ブラウザ操作型ロボ端末プロジェクト。StackChan を「薄い再生クライアント」にし、
Android Termux 上の Gateway が LLM 推論・音声生成・履歴管理をすべて担う。
音声認識 (STT) は使わない。

- ベース: release tag **β3.5.0**（simple_wav 方式）
- main ブランチの ConversationController/STT/LLM 分離実装は**使わない**

---

## 全体フロー

1. Android Termux で Gateway を起動（llama-server + FastAPI）
2. スマホ/PC ブラウザから Gateway UI を開く
3. パスワードでログイン
4. 質問入力 + 回答言語/長さ/ピッチ/音量/速度 を設定
5. 「送信して喋らせる」
6. Gateway が LLM で回答生成 → 同じテキストから WAV 生成
7. Gateway が StackChan の `POST /api/speak` を呼ぶ
8. StackChan が Gateway の `POST /synthesis` から WAV を取得し発話
9. （A ボタン）StackChan が `__REASK_LAST__` を /synthesis に POST → 最後の質問を再 LLM 処理して発話

### 重要仕様
- 表示回答と発話内容は意味として一致させる
- デフォルト日本語。英語選択時は表示も発話も英語
- 日本語は表示が漢字混じり、音声生成時のみ漢字→かな変換可。**読み上げ用テキストも UI に表示**
- 設定は確実に永続保存。履歴から再発話/声だけ変更/削除が可能
- パスワードログインでセキュリティ確保

---

## Part A: StackChan ファームウェア（最小変更）

β3.5.0 をベースに以下のみ追加・変更する。

| # | 変更 | 状態 |
|---|------|------|
| A1 | `M5.Speaker.setVolume(160)` → `255` | ✅ |
| A2 | `speakConfiguredText()` を `speakText(text)` にリファクタ（A ボタンは従来通り設定テキストを発話） | ✅ |
| A3 | `POST /api/speak` 追加。busy なら `{"ok":false,"error":"busy"}`、受付なら `{"ok":true}` | ✅ |
| A4 | `GET /api/status` 追加。`ok/connected/speaking/ip/tts_host/tts_port/tts_engine` を返す | ✅ |
| A5 | A ボタン運用: 設定の「Text to speak」に `__REASK_LAST__` を入れる（コード変更不要） | ✅ |

### 設計メモ
- `/api/speak` は WebServer ハンドラ内で同期再生せず、`pendingApiSpeak` フラグを立てて即 `{"ok":true}` を返す。実際の TTS 再生は `loop()` が拾って実行（再生はブロッキングのため）。
- `/api/speak` のデフォルト発話テキストは `__CURRENT__`（Gateway は current.wav を返す＝再 LLM しない）。`text` フォーム引数で上書き可。
- A ボタン → `speakConfiguredText()` → 設定テキスト（`__REASK_LAST__`）を /synthesis に POST → Gateway が再 LLM。
- `speaking` 状態は `ConfigPortal` にプローブ用コールバックで渡す。

### StackChan 側 想定設定
- TTS Engine Type: `simple_wav`
- TTS Host: Android スマホの LAN 内 IP
- TTS Port: `50021`
- Speaker / Style ID: `3`
- Text to speak: `__REASK_LAST__`

---

## Part B: Android Termux Gateway（FastAPI）

`gateway/` 配下に実装。

- llama.cpp `llama-server` → `http://127.0.0.1:8080/v1/chat/completions`
- Gateway listen → `0.0.0.0:50021`
- TTS: `espeak-ng`（ja は pykakasi で漢字→かな変換）
- 保存先:
  - `~/stackchan_gateway/settings.json`
  - `~/stackchan_gateway/history/`
  - `~/stackchan_gateway/current.json` / `current.wav`
  - `~/stackchan_gateway/logs/`

### settings.json 項目
`password_hash, password_salt, session_secret, stackchan_host, stackchan_port,
voice_lang, pitch, volume, speed, answer_mode, kanji_to_kana, auto_speak,
max_history, system_prompt`

デフォルト: stackchan_port=80, voice_lang=ja, pitch=80, volume=200, speed=150,
answer_mode=short, kanji_to_kana=true, auto_speak=true, max_history=50。
初期パスワード `stackchan`（salt+sha256 で保存、平文不可）。

### エンドポイント
`GET /`, `POST /login`, `POST /logout`, `GET /status`, `POST /settings`,
`POST /ask`, `POST /voice/rebuild`, `GET /current.wav`,
`POST /synthesis`（StackChan 互換: `__REASK_LAST__` で再 LLM、通常は current.wav）,
`POST /stackchan/speak`, `GET /history`,
`POST /history/{id}/speak`, `POST /history/{id}/revoice`, `POST /history/{id}/delete`

### 音声生成
- ja: `espeak-ng -v ja`（必要に応じ pykakasi で読み変換、読み上げ用テキストを保存・表示）
- en: `espeak-ng -v en-us`
- pitch `-p` 0–99（既定80）, volume `-a` 0–200（既定200）, speed `-s` 80–260（既定150）

### 完了条件
1. β3.5.0 ベースでビルド可 ✅
2. スピーカー音量 255 ✅
3. `/api/speak` 追加 ✅
4. ブラウザ送信で A ボタン不要で発話 ✅（/ask → set_current → /api/speak → /synthesis）
5. A ボタンで最後の質問を再 LLM 発話 ✅（`__REASK_LAST__` → process_question）
6. ピッチ/音量/速度をブラウザ変更可 ✅（設定保存 + revoice）
7. 音声だけ再生成可 ✅（/voice/rebuild, /history/{id}/revoice）
8. settings.json 永続保存 ✅（原子的書き込み）
9. 履歴から再発話/声変更/削除 ✅
10. パスワードログイン ✅（salt+sha256, 署名Cookie）
11. 表示回答と発話一致 ✅（answer=表示, speech_text=読み上げ。両方UI表示）
12. デフォルト日本語 ✅

### 既知の制約
- `/synthesis` の `__REASK_LAST__` は LLM 推論を**同期実行**する。StackChan(simple_wav)
  の HTTP タイムアウトは約 30 秒（`VoiceVoxClient.cpp` `kNetworkTimeoutMs`）なので、
  モデルが遅いと A ボタン再発話がタイムアウトする可能性がある。短い回答長(`hitokoto`/`short`)
  と小型モデル推奨。ブラウザ送信フローは LLM を先に終えてから `/api/speak` を呼ぶため影響なし。

---

## 進捗ログ
- 2026-06-27: β3.5.0 に復帰確認（HEAD == β3.5.0, working tree clean）。本ドキュメント作成。
- 2026-06-27: ファームウェア A1–A5 実装・ビルド成功（RAM 18.1%, Flash 18.3%）。
- 2026-06-27: Gateway 実装完了（app.py / ui.py / run.sh / stop.sh / requirements / README）。
  TestClient で 16/16 のスモークテスト合格（認証・設定・/ask・/synthesis・履歴・revoice・削除）。
