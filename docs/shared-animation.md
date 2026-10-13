# 共通Skeletonとアニメーション制作・再生

プレイアブルキャラクターは共通Skeletonとクリップを再利用する。本番の基準Skeletonは未決定のため、同梱RiggedFigureを検証に使う。RiggedFigureの骨名や体格をゲームの恒久仕様にはしない。既存のozz-animation 0.17.0、cgltf 1.15を継続使用し、新しい依存は追加しない。

## 制作物と所有権

| 制作物・状態 | 所有する内容 |
|---|---|
| `.cskin` モデル | メッシュ、材質、Skin Weight、Joint参照、モデル固有のInverse Bind、基準姿勢。従来の内蔵Clipも読める |
| `.canim` 共通ライブラリ | 安定したAsset ID、共通Skeletonの名前・親・基準TRS、名前付きの複数Clip、制作元・依存・CookレシピのSHA-256 |
| `AnimationRig` | 不変のSkeletonとozz用に構築済みのClip。同じScene内の利用者で共有 |
| `AnimationPlayer` | 個体ごとの時刻、選択Clip、Sampling Context、評価姿勢、モデル用Palette。モデルとRigを共有参照 |
| Scene / Renderer | SceneがWorldとAsset IDを所有。RendererがGPU Meshとフレーム別Bone領域を所有 |

Scene Entityの`asset`はモデルID、`animation.library`は共通ライブラリID。空ならモデル内蔵Clipを使う。Clipを持たないモデルはRest姿勢のプレビューが可能。GPUハンドルを保存形式の識別子に使わない。

`.canim`はCHAN v1の最小バイナリ形式。`.cskin`はクリップなしモデルを認めるCHRS v2へ更新し、旧v1も読み込む。`.cscene`のlibrary参照はv2で導入した。現在は[材質と照明](materials.md)と投影影設定を含むv4で保存し、旧v1〜v3も読み込む。旧アプリへの逆互換はない。`.cmesh`は変更しない。

## DCC側の共通Skeleton契約

1. 基準となるArmatureを一つ管理し、各キャラクターはその骨名・親子関係・ローカルRest TRSを保持してSkinを作る。モデル名・形状・材質・Weight・Inverse Bindは各モデルのものを使う。
2. 骨名は一意、Clip名もライブラリ内で一意にする。モデルとアニメーションのエクスポートで祖先ノードや単位・座標変換を揃える。名前だけ同じ別骨格は互換としない。
3. glTF/GLBへ位置・回転・正スケールのLINEARキーとして出力する。STEP/CUBICSPLINE、制約、IK等はDCCでこの形式にベイクする。モデルだけの出力にはClipは不要。
4. 共通ライブラリは複数Clipを含む一つのglTF/GLBから作る。Skin付き出力ではSkin Jointとその祖先を採用し、モデル固有ノードへのアニメーションは拒否する。Skinなしのアニメーション専用出力では全ノードを採用するため、共通Armature全体を出力し、カメラ等を混ぜない。
5. Skinは従来どおり1つ、4影響/頂点、最大256ノード。共通Clipは1〜64、合計100万キー以内。これは現実装の検証上限で、ゲーム人数や性能目標ではない。

読み込み時に骨名でモデル側の番号へ対応付けるため、兄弟の格納順やSkin Joint Indexの違いは許容する。親の名前とRest行列は照合し、各行列要素の許容差は`1e-4 * max(1, abs(基準値))`。共通SkeletonにないSkin Jointは拒否する。モデル固有の補助ノードはRest TRSと親姿勢で補完する。

骨の長さや親構造が異なるキャラクターへのRetargetingは行わない。体格を変更する場合は、この共通Skeleton契約を維持するか、別のSkeleton/ライブラリとして扱う。本番素材が決まった段階で、DCCからの実出力を再検証する。

## 変換と確認

リポジトリルートで、ビルド済みCookerを使う。

```powershell
& out/build/vs2026/Debug/ChromalisAssetCooker.exe Character.glb out/cooked/Character.cskin --id characters/example
& out/build/vs2026/Debug/ChromalisAssetCooker.exe CommonAnimations.glb out/cooked/Common.canim --id animations/common
powershell -NoProfile -ExecutionPolicy Bypass -File Game/assets/Scenes/Prepare.ps1 -Cooker out/build/vs2026/Debug/ChromalisAssetCooker.exe
& out/build/vs2026/Debug/ChromalisEditor.exe --scene Game/assets/Scenes/SharedAnimation.cscene
```

最初の2行は自分の制作物のパスへ置き換える。後ろの2行は同梱RiggedFigureで再現できる。サンプルの2体は同一モデルと共通ライブラリを使い、個別の再生状態を持つ。本番の異なるキャラクター素材での実績を示すサンプルではない。

EditorではSkeletal modelを選択し、AnimationのLibrary pathへ`.canim`を入力してLoad shared animations。登録済みライブラリはAnimation sourceで選べる。Clip、再生/停止/一時停止、速度、開始時刻、Rest/Bind、Joint位置を確認できる。Preview timeと1/30秒送りは一時プレビューで、Sceneへ保存するStart timeとは別。停止中の最終時刻も保持する。

Assetsパネルの種類にShared animationを追加した。制作元glTF/GLB・出力`.canim`・安定IDを登録し、Rebuild + reloadで再生成と利用中Sceneへの反映を行う。モデルとライブラリは別のカタログ項目なので、それぞれ更新する。Play中のCook操作は従来どおり無効。

共通ライブラリのreloadは全利用者の互換性とClip名を確認してからまとめて差し替える。失敗時は旧データ・姿勢・再生時刻を保持する。成功時は各Entityの保存された初期設定から再生を開始する。モデル差し替え時も共通ライブラリ参照を維持し、非互換なら差し替えない。Clip削除・改名はSceneのClip参照更新が必要。

## 更新順序と制約

時刻更新 → CPUでClipサンプリング → 共通Skeletonの階層評価 → モデルのJoint番号へ対応付け → モデル固有Inverse Bindとの合成 → CPU Socket → VFX追従 → 描画。WorldはSceneが管理し、アニメーションはWorldを動かさない。GPUから骨を読み戻さない。PaletteのGPUアップロードは既存のフレームFence完了後に行う。

ライブラリは読込時にozz形式へ一度構築する。モデルごと・フレームごとの重複構築はしないが、Scene間のキャッシュは導入していない。編集SceneとPlayコピーは引き続き独立。Runtimeのozz offline builder依存も維持する。大量Clip向けの事前圧縮保存は別途判断する。

今回の範囲は制作元の分離・共通データの再利用・再生/更新の基盤。複数ソースのClip結合、Clip範囲の切り出し、クロスフェード、状態機械、Animation Event、Root Motion移動、IK、Retargetingは未実装。Socketは従来の位置取得のみ。これらを対応済みとして扱わない。
