# StackChan first face

M5Stack StackChan（CoreS3）の画面に、瞬きする顔を表示する最小構成です。
サーボモーターはまだ制御しません。

## ビルド

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

## 書き込み

1. StackChan のベース側 USB-C ポートを PC に接続します。
2. 必要なら、microSD スロット付近の RST ボタンを約3秒長押しし、
   インジケーターが緑に点灯したら離してダウンロードモードにします。
3. 次のコマンドを実行します。

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload
```
