# 平面移動デモ

`ChromalisPlayerDemo`はGame/Editorと同じWin32・DX12実行基盤を使う独立したデモホスト。Game/Editorの既存起動方法は変えない。ImGuiへの依存はない。

```powershell
cmake --build --preset debug --parallel 2 -- /nodeReuse:false
& out/build/vs2026/Debug/ChromalisPlayerDemo.exe --scene path/to/Player.cscene
```

Sceneとモデルは実行時に指定する。HumanoidDemoの元素材はDevelopment側で管理し、このPublicリポジトリには複製しない。素材の準備手順はDevelopment側の`docs/player-demo.md`に記載する。

## 操作

| 入力 | 動作 |
|---|---|
| WASD / 矢印 | カメラ基準の平面移動 |
| Shift＋移動 | 走行。離すと歩行、移動入力なしで待機 |
| Q / E | カメラを水平回転 |
| Page Up / Page Down | カメラ距離を近づける／離す |
| R | 原点・向き・カメラ・Animation時刻をリセット |
| Esc | 終了 |

斜め入力を正規化し、対向キーは相殺する。入力を離したら移動は即停止し、見た目の向きは短時間で移動方向へ旋回する。フォーカス喪失時は既存ホストがキー状態を解除する。最小化・サイズ変更中は更新を止め、復帰時の巨大な移動を避ける。

## デモの設定と接続

- 床はY=0、24m四方。プレイヤーの中心をXZ各±11.5mへ制限する。半径を持つ衝突・段差・重力は実装しない。
- Sceneには親なしの骨格Entity `Player`と、名前付きClip `Idle` / `Walk` / `Run`が必要。共通`.canim`参照を利用する。
- HumanoidDemoの前方はRuntimeで-Zなので、移動方向へ向く半回転を描画用Quaternionに加える。一般的なモデルの自動前方検出ではない。
- 歩行速度は`0.34 / 0.60 ≒ 0.57 m/s`、走行速度は`0.44 / (0.38 × 0.75) ≒ 1.54 m/s`。素材の接地中の足移動量と時間から設定した等倍再生の値。ゲーム全体の速度仕様ではない。
- 歩行↔走行では正規化した周期を引き継ぎ、待機との切替ではClip先頭から再生する。マップ端で完全に止まった場合もIdleになる。クロスフェードはない。
- カメラは高さ0.9mを注視し、指数補間で追従。俯角0.55rad、距離4〜12m。壁とのカメラ衝突はない。

`PlayerMotion`は位置・向き・移動状態・カメラを所有する。`PlayerDemo`は入力とClip名を対応付ける。`GameLoop`はこの処理を共通ホストのScene更新前へ接続する最小境界で、Editorと併用しない。

更新順序は入力 → 平面移動・方向・Clip選択 → `SetRuntimeTransform` → SceneのCPU姿勢/World評価 → 影・本描画。GPU読み戻しや、毎フレームの`SceneRuntime::Apply`は使わない。RendererやAnimationにプレイヤー移動の判断を持ち込まない。

`SceneRuntime`は保存用Localとは別に実行用Localを所有する。入力検証後の設定だけで更新し、保存文書・Dirty・GPUアセット世代は変えない。親の実行用Transformは子にも伝わる。同じ保存用LocalのApply/reloadは実行値を保持し、Localの編集またはOpenは保存値を採用する。

## 検証と制約

`player.motion`は移動距離、斜め速度、異なる時間刻み、カメラ基準方向、境界、停止、追従、無効時間を検査する。

`player.scene`は生成した小骨格でClip切替・姿勢変化・描画、保存値分離、GPU世代維持、reload成功/失敗、親子Transform、リサイズ・最小化復帰・破棄をWARPで検査する。画素は背景一色でないことを確認し、画作りの正しさを網羅する画像比較ではない。

実素材は`ChromalisPlayerDemoTests output-dir hardware Player.cscene`で同じ経路を実GPU・GBVで検査できる。HumanoidのFoot.Lがあれば、歩行・走行の接地区間の水平ずれも測定する。

`player.input`は実際のApplicationにWin32キーイベントを送り、歩行・走行・カメラ旋回・リセット・フォーカス喪失の入力解除・最小化復帰を確認する。人が物理キーボードを操作する評価とは区別する。

このデモは平面での操作・アニメーション接続まで。Dodge、戦闘、物理、Root Motion、Animation Blend、足IK、Editor内のプレイヤー操作は未実装。旋回・Clip切替・境界沿いでの足滑りを完全には解消しない。
