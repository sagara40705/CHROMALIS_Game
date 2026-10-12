# CHROMALIS Game — Phase 0A

Windows x64 / C++20 / DirectX 12 のゲーム本体とRuntimeです。現在は三角形の描画・入力・診断を行う最小hostです。
このリポジトリだけでビルドでき、EditorやDear ImGuiの取得・ビルドは不要です。

## 必要環境

- Visual Studio 2026「C++によるデスクトップ開発」、v145 toolset。
- Windows SDK 10.0.26100.0（DXCを含む）、CMake 4.2以上。
- 実行: Windows、D3D12 FL11_0 + Shader Model 6.0対応GPU・ドライバー。
- DebugおよびCTest: Windows Graphics Tools / D3D12 Debug Layer。

## 単独ビルド・実行

PowerShellでリポジトリ直下から実行します。

```powershell
git clone https://github.com/sagara40705/CHROMALIS_Game.git
cd CHROMALIS_Game
cmake --preset vs2026
cmake --build --preset debug --parallel
ctest --preset debug
& .\out\build\vs2026\Debug\ChromalisGame.exe
```

Releaseは `cmake --build --preset release --parallel`、検証は `ctest --preset release`。
CTestは対話型WindowsデスクトップでWARP + Debug Layerを使用し、GPU画素読み戻しとリサイズ・最小化・復帰・終了を検証します。
WARPの結果はhardware性能の測定ではありません。

## 操作・診断

- WASD / 矢印: 三角形を移動。Space: 位置をリセット。Esc: 終了。
- ログは実行ファイルの隣の `logs/` に保存します。`--log file.log` で変更できます。
- `--warp` はソフトウェア描画を明示選択します。
- `--smoke-test --gpu-validation` は有限フレームでGPU validation付き検証を行います。
- `--frames N --capture file.bmp` は有限フレーム実行後にGPU読み戻し画像を保存します。

## ファイル構成

- `Main.cpp`: Game host。
- `src/runtime/`: Win32、DirectX 12、入力、ログ、診断。
- `shaders/Triangle.hlsl`: DXCでビルド時にコンパイルする診断用シェーダー。
- `app.manifest`: DPI設定。
- `CMakeLists.txt`, `CMakePresets.json`: 単独ビルド設定。

公開するコード・シェーダー・公開可能なアセットだけを管理します。内部文書、認証情報、個人の環境設定、生成物、ログ、公開権限のない素材は追加しないでください。
Windows SDK・MSVCは別途インストールする前提で、配布物を同梱していません。
