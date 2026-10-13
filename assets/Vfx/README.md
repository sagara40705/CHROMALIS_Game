# Phase 0C のVFX素材

`Pulse`は発光リング、`AlphaProbe`は色空間・透明合成の画素検証用。PNGとefkprojは本プロジェクト用に新規作成した検証素材で、第三者画像・AI生成画像は使用していない。このディレクトリのPNG、efkproj、efkefc検証素材は[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)で提供する。Effekseer本体の利用条件は別途適用される。

PNGはStraight Alpha。Pulseは64×64、十分な外周余白を持つ手続き生成リング。AlphaProbeはRGBA=(128,96,160,128)の2×2画像で、効果側のRGB係数=(128,128,128)/255と組み合わせる。最終SDR値を独立計算し、誤ったsRGBデコード・係数・透明合成を検出する。

## 再生成

1. [公式1.80.7 Windowsツール](https://github.com/effekseer/Effekseer/releases/tag/1807)を取得・展開する。`Effekseer1.80.7Win.zip`のSHA-256は`6f059b9cce3a79c5bfe3df9c970cfe62fd72acf130083c7b81832d90f8d1335f`。
2. Game直下から以下を実行する。スクリプトはPNGを再生成するため、手編集したPNGは別名で保管する。

```powershell
& assets/Vfx/Build.ps1 -Effekseer C:/tools/Effekseer1.80.7Win/Tool/bin/Effekseer.exe
```

3. 公式CUI `-cui -in source.efkproj -o output.efkefc`によって実際のゲーム形式へ出力する。新しい候補ファイルのmagicを確認してから既存出力を置換する。
4. EditorのVFXパスへ`assets/Vfx/Pulse.efkefc`（Development直下では`Game/assets/...`）を指定し、Reload、Playで確認する。変更後は再出力→Reload→Play。

今回、efkprojの構成と公式ツールによるCUI変換・Runtime表示を検証した。Effekseer GUI上の編集操作は未検証。GUIで色・寿命・サイズ等を編集する場合も、同じefkefc/PNG出力を読み込む。

AI素材を利用する場合は、利用条件と出典を別途記録し、背景除去・不要な縁色の除去・Straight Alpha化・色PNGと線形Data Textureの区別・余白/Flipbook分割を済ませてツールへ渡す。この前処理をAI素材で実測したとは扱わない。今回のSprite、Alpha Blend、Additive、深度テスト以外は個別の接続検証が必要。
