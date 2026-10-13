# Phase 0C: Animation / VFXの接続契約

静的`.cmesh`を維持し、骨格モデル`.cskin`とEffekseer `.efkefc`を追加する。これは制作経路を確認するプレビュー実装であり、2つの静的スロットと1つの骨格スロットをWorldの恒久仕様にはしない。

## 採用判断

| 候補 | 今回の判断・理由 |
|---|---|
| ozz-animation 0.17.0 | CPU SamplingJob / LocalToModelJobを暫定採用。将来のブレンドにも接続でき、姿勢評価全体の独自実装を避ける。MIT。公式gltf2ozzは骨格・クリップの変換に利用できるが、既存のメッシュ・材質・依存追跡との対応付けは別途必要 |
| ozz公式gltf2ozz + ozz archive | 今回は不採用。Cooker側のcgltfと二重解析になり、Joint番号の突き合わせも必要。ozz archiveを外部入力の安全な保存境界として直接使う設計も避けた |
| 限定的な独自TRSサンプラー | 実装は小さいが、今後のブレンドや性能評価の保守対象が増えるため不採用 |
| Effekseer 1.80.7 / DX12 | 代表的なSprite効果の制作・変換・表示について暫定採用。公式ツールとC++配布物を固定し、同梱LLGIを使用。MIT、LLGIはzlib、stb_imageはMIT/Public Domainの選択式 |
| 独自Flipbook | 簡単な1効果には適するが、今回必要な外部制作・複数粒子の調整経路を独自開発しないため延期 |

依存URLとSHA-256は`cmake/Animation.cmake`、`cmake/Vfx.cmake`に固定。ビルド時に取得し、SDKの展開物はGitへ追加しない。実行ファイルの隣の`licenses/`へ配布元の利用条件をコピーする。ツール自体はRuntimeの依存ではない。

参照: [ozz 0.17.0](https://github.com/guillaumeblanc/ozz-animation/tree/0.17.0)、[ozz公式変換ツール](https://guillaumeblanc.github.io/ozz-animation/documentation/toolset/)、[Effekseer 1.80.7](https://github.com/effekseer/Effekseer/releases/tag/1807)、[同版C++ガイド](https://github.com/effekseer/Effekseer/blob/1807/docs/Help_Cpp/18x/Guide_Cpp_En.md)。APIと利用条件は固定配布物のソースでも確認した。

## 骨格アセット

`.cskin`はlittle-endianの`CHRS` magic、version 1、長さ付きの既存cmeshペイロード、Joint階層、Inverse Bind、4影響のJoint/Weight、TRSクリップを保存する。cmeshペイロードの頂点は骨格モデルのローカル座標であり、静的モデルのようにノード変換を焼き込まない。cmesh単独の形式と解釈は変更しない。

CookerでglTF右手系から左手系へ一度だけ変換する。位置のZ、QuaternionのX/Yを反転し、行列は`F * M * F`、三角形の表裏を反転する。行ベクトル・row-major、単位m・秒・radは0Bと共通。中間の非Jointノードも含めた親先行DFS順を維持する。

入力境界でサイズ、参照、階層、TRS、Quaternion、Weight、時刻、形式版を検証する。ファイル上限512 MiB、256ノード/Binding、64クリップ、合計100万Key、1クリップ3600秒は現在の入出力保護上限であり、ゲーム全体の性能目標ではない。保存は候補ファイルを再読込・検証してから置換する。既存Cookerのreport保護と依存ファイルのSHA-256記録を共用する。

初期対応は1 Skin、4影響/頂点、LINEARの位置・回転・正スケール、法線を持つ三角形メッシュ。複数Primitive/材質は既存経路を使用する。負/ゼロスケール、せん断、Morph、複数Skin、STEP/CUBICSPLINE、追加JOINTSセットは診断して拒否する。DCCで対象の補間方式へベイクして取り込む。すべてのglTFキャラクターとの互換性を保証するものではない。

**現在のトレードオフ:** `.cskin`には検証可能なTRSを保存し、読込/同期reload時にozz RawSkeleton/RawAnimationからRuntimeオブジェクトを構築する。このためGameにはozzのoffline builderコードもリンクされるが、glTF、cgltf、WIC、DCCはリンクされない。起動・reloadのCPU/メモリコストは残る。大量クリップや製品配布時には、検証可能なarchive契約を決めたうえで圧縮構築をCookerへ移す。毎フレーム変換はしない。

## 姿勢・World・描画

`AnimationPlayer`が時刻、再生状態、ozz姿勢、CPU Joint Model行列を所有する。RendererはGPU VB/IB/テクスチャとフレーム別Bone Uploadを所有する。World Transformはプレビュー設定側にあり、AnimationPlayerはWorld位置を変更しない。

順序は「再生時刻更新 → ローカル姿勢 → 親子Model姿勢 → CPU Socket → VFX追従/更新 → 描画」。Skin Matrixは`InverseBind * JointModel`、頂点は`local * Skin * World * View * Projection`。法線は各Skinの逆転置をWeight合成し、World変換後に正規化する。プレビューWorldは正の一様スケールのみ。GPUに姿勢を読み戻す必要はない。

Rest PoseとBind Poseは別操作。RestはglTFノードの既定TRS、BindはInverse Bindの逆行列からSkin Jointを復元する。Bind表示ではPaletteがほぼ単位行列になり、元のメッシュBind空間で表示するため、作者のシーンRootを含むRestの向きとは異なる場合がある。Skinに含まれない補助ノードは親とRest TRSで補完するため、そのノードのDCC上のBind配置まで証明しない。Socket取得はCPU Joint Modelと外部Worldの合成。今回のVFX追従は選択Jointの**位置のみ**で、骨の回転・スケールは継承しない。

将来Simulationへ接続する際は、Root Motionデルタを移動・衝突解決前に抽出し、衝突解決後のWorldと当tickのゲーム判定用姿勢を確定してからSocket/命中判定を評価する。見た目の補間姿勢は別段階とし、描画省略によって判定用姿勢まで省略しない。今回は実移動、戦闘判定、IK、ブレンドツリーを実装しない。

各FrameのFence完了後にだけ対応するBone Upload（位置用/法線用各256行列）を書き換える。同期reloadは候補CPU/GPUデータの構築とQueue drainを終えてから置換する。失敗時は旧Player・GPUモデルを保持する。リサイズはGPU idle後にRTV/Depthを再生成する。

## VFXと色

`.efkproj` + PNG → Effekseer公式ツール → `.efkefc` + PNG → Renderer内の`VfxPlayer`。Gameはツールを起動しない。EditorのReloadは変換済みデータを読み直す操作であり、VFXの再出力は外部ツール側で行う。同期reloadで新しいManager/Loaderを候補として作るため、同名PNG更新を古いキャッシュが隠さない。候補の必須テクスチャ/モデルを検査し、失敗時は旧効果と再生状態を維持する。

Graphics Queue/Command Listはホストが所有し、Effekseerはそのリストへ記録する。2つのFrameごとに専用SingleFrameMemoryPoolとCommandListを持ち、ホストFenceを待ってから再利用する。終了時もQueue drain後にManager、Effect、GPUリソースを解放してDebug Layerを回収する。VFXはダメージやゲーム判定を持たない。

- SceneのRTVは`R8G8B8A8_UNORM_SRGB`、Swap Chainは既存の`R8G8B8A8_UNORM`。Scene shaderは線形RGBを出力し、RTVが格納時にsRGB変換、Blend時に宛先を線形化する。メッシュの旧手動出力変換は除去した。
- 色PNGはsRGB SRV、Normal/UV distortion等のData TextureはUNORM。Effekseer LoaderはLinear color-space設定、`MaintainGammaColorInLinearColorSpace=false`。効果の頂点色係数は線形値として扱う。
- 今回のPNGはStraight Alpha。Alpha Blendは`srcLinear * alpha + dstLinear * (1-alpha)`、Additiveは`srcLinear * alpha + dstLinear`。Alpha自体をsRGB変換しない。
- Premultiplied素材の持込は今回未対応。Straight Alphaへ変換するか、別の明示的なBlend契約を追加してから使用する。
- 描画順は表示色Clear → 不透明/MASKメッシュ（深度書込あり）→ VFX（今回の効果は深度テストあり・書込なし）→ UNORM RTVへ戻してImGui。
- SDR 8-bitの線形合成経路であり、HDRの輝度保存、Bloom、Tone Mapはない。加算結果は飽和する。背景Distortion、Soft Particle、Custom Material、音声は未接続としてreloadを拒否する。
- DistortionにはScene Colorコピーと読取状態/解像度の契約、Soft ParticleにはDepth SRVと線形化定数・読取専用深度の契約が必要。HDRには浮動小数点Scene Colorと最終出力変換が必要。今回は追加しない。
- Flipbook/Noise/Ribbon等はライブラリに機能があるが、今回の代表Spriteテストだけで正式対応済みとはしない。GPU Particle機能も未接続。

`.efkefc`は固定ツールが生成した信頼できる制作物を対象とする。magic/サイズ/読込失敗は診断するが、Effekseer内部バイナリ解析を任意の悪意あるファイルに対して強化したとは扱わない。

Effekseer同梱LLGIのDX12 texture uploadには内部の無期限待機がある。ホスト側Fenceの10秒診断が外部ライブラリ内部にも適用されるとは扱わない。今回の正常動作・失敗reload試験では問題は出ていないが、デバイスハング/除去時の終了時間や復旧は未検証で、製品採用時の追加評価対象とする。

## ビルド・制作手順

以下は**Gameリポジトリ直下**で実行する。

```powershell
cmake --preset vs2026
cmake --build --preset debug --parallel 4
ctest --preset debug
& out/build/vs2026/Debug/ChromalisAssetCooker.exe assets/RiggedFigure/RiggedFigure.glb out/cooked/RiggedFigure.cskin --id samples/rigged-figure
& out/build/vs2026/Debug/ChromalisAssetCooker.exe assets/Cube/Cube.gltf out/cooked/Cube.cmesh --id samples/cube
& out/build/vs2026/Debug/ChromalisGame.exe --asset out/cooked/Cube.cmesh --animation out/cooked/RiggedFigure.cskin --vfx assets/Vfx/Pulse.efkefc
```

`--bind-pose`でBind表示、`--frames 120 --capture out/preview.bmp`で有限実行、`--gpu-validation`でGPU-based Validation。Releaseは同名preset。`--no-vsync`は負荷計測用。70フレーム以降のCPU記録時間（Fence待ち込み/除外）とGPU timestamp時間を終了ログへ出す。Present待ちやCook時間をGPU時間とは呼ばない。

オフライン取得は`CHROMALIS_OZZ_ARCHIVE`、`CHROMALIS_EFFEKSEER_ARCHIVE`に同一SHA-256のローカルアーカイブを指定する。Game-onlyは`CHROMALIS_BUILD_COOKER=OFF`と`BUILD_TESTING=OFF`。Dear ImGuiはDevelopment側だけの依存。

EditorではCook欄の出力拡張子を`.cskin`にしてCook + Reload、またはAnimation欄で変換済みファイルを選択する。Clip、Play/Resume、Pause、Stop、速度、時刻、Loop、Rest/Bind、World、Joint位置を操作できる。VFX欄はパス・Reload、Play/Stop、TRS、個数、Joint位置追従、粒子数/エラーを表示する。VFX再変換は[素材手順](../assets/Vfx/README.md)を参照。相対パスはプロセスの作業ディレクトリ基準。

## 検証の限界

外部実モデルは[Cesium RiggedFigure](../assets/RiggedFigure/README.md)の1種類。TRS、複数Clip、Socket、UV付きスキニングは生成fixtureでも検証するが、他のDCCや全キャラクターへの互換性証明ではない。VFXは手続き生成の代替PNGを使用し、AI生成素材そのものの品質・権利・変換は未検証。最終的なアート品質、長時間運用、大量実キャラクター、悪意あるVFXバイナリ、実ゲームの性能予算は別途評価する。
