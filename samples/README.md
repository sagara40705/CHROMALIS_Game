# 公開Runtimeのサンプル

Gameリポジトリ単独で取り込み・描画を確認するための最小例。通常の制作SceneはDevelopment側のContent/Scenesで管理する。

- `Assets/Cube/`：CC0の外部静的モデル。インポート・UV・テクスチャ検証にも使用。
- `Assets/RiggedFigure/`：骨格モデルの外部互換性検証。出典とライセンスは同梱READMEを参照。
- `Assets/Vfx/`：Pulse演出とAlphaProbe合成検証、外部ツールの編集元・再出力手順。
- `Scenes/Example.cscene`：静的モデル・骨格モデル・VFXを併用する最小Scene。
- `Scenes/Prepare.ps1`：Exampleが必要とするモデルだけをoutへCookする。

```powershell
# Gameルートから実行する
powershell -NoProfile -ExecutionPolicy Bypass -File samples/Scenes/Prepare.ps1 -Cooker out/build/vs2026/Debug/ChromalisAssetCooker.exe
& out/build/vs2026/Debug/ChromalisGame.exe --scene samples/Scenes/Example.cscene
```

Materials / Shadows / SharedAnimationの重複Sceneは削除し、DevelopmentのContent/Scenesへ一本化した。素材本体と、意味のあるCPU/GPU回帰テストは残している。
