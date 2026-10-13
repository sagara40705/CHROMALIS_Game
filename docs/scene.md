# 最小Sceneの契約

## 所有権と依存

ApplicationがRendererとSceneRuntimeを所有する。SceneRuntimeはRendererの後に生成し、GPU待機後に先に破棄する。Editorは両者を借用し、Play中の実行用SceneRuntimeだけを追加所有する。ImGui/ImGuizmoはGameへリンクしない。

| 所有者 | 所有する状態 |
|---|---|
| SceneDocument | 安定したEntity ID、名前、親、Local Transform、Asset ID、保存対象の再生初期設定、カメラ |
| SceneRuntime | Document、評価済みWorld、共有ロード済みアセット、個体ごとのAnimationPlayer・VfxPlayer、フレーム用描画リスト |
| Renderer | Device/Queue/Fence、共有MeshHandleに対応するGPU VB/IB/Texture/Descriptor、フレーム別Upload、Swap Chain/Depth、要求時のScene Color/Depth |
| EditorOverlay / EditorSession | 選択ID、編集カメラ、入力欄、ImGui/SRV、Play用Sceneコピー。編集用Sceneの正本は借用する |

SceneDocument/SceneIOはD3D12とImGuiに依存しない。SceneRuntimeがCPUデータを読みRendererにGPU生成を依頼する。MeshHandleはslot+generationであり、永続Asset IDとは別物。同じAsset IDの静的/骨格メッシュは共有し、骨格の再生時刻・姿勢は個体ごとに持つ。VFXは既存のEffekseer接続を維持し、オブジェクトごとにVfxPlayerを持つため、大量配置向けの共有Manager設計ではない。

## Transformと更新順序

Simulationは`SetRuntimeTransform`で保存用Localと別の実行用Localを更新できる。Document/DirtyやGPU世代は変えず、次のUpdateで親子Worldへ反映する。同じ保存用LocalのApply/reloadでは実行値を保持し、Local編集またはOpenでは保存値に戻る。[平面移動デモ](player-demo.md)が最初の利用箇所。

左手系、1単位=1m、秒、行ベクトル・row-major。Localは位置・単位Quaternion・正のXYZ Scale。Local行列はS*R*T、WorldはLocal*ParentWorld。非一様Scaleと回転の合成をTRSへ分解し直さず行列で保持し、描画法線にはWorldの逆転置を使う。負/ゼロScaleは現段階では拒否する。小さな可逆Scaleは許容する。

1. フレーム境界でEditor操作、同期reloadを適用する。
2. AnimationPlayerがCPU姿勢とPaletteを更新する。DocumentのStart timeは初期値で、再生中の時刻とは別。
3. 親とSocket参照先が先になる順序でWorldを確定する。
4. VFXのWorldを確定して演出を更新する。Root VFXのみ他の骨格オブジェクトのJoint名を参照し、位置だけに追従する。Local位置はWorld軸のオフセット。回転とScaleは継承しない。
5. RendererはフレームFence完了後にUploadを更新し、借用した描画リストを消費する。各骨格drawは別の256-byte整列Palette領域を使用する。
6. 不透明モデル→VFX→UNORM RTVでImGui→Present。

ReparentはLocal維持で、Worldは変化する。Deleteは子孫も削除する。削除対象を追従していたRoot VFXは追従を解除し、現在のWorld位置を保持する。Entity IDは削除しても再利用しない。将来のSimulationはWorldの書き手としてこの境界に接続し、AnimationやRendererが勝手にキャラクターの実移動を所有しない。Root Motion、衝突、攻撃判定は未実装。

## 保存・読み込み・更新

`.cscene`はUTF-8 JSON。共通Animation参照・共有材質・照明・投影影設定を含むversion=4で保存し、旧version=1/2/3も読み込む。材質の契約は[materials.md](materials.md)を参照。nlohmann/json 3.12.0（MIT）を公式アーカイブとSHA-256で固定し、ライセンスを配布先へコピーする。オフラインは`CHROMALIS_JSON_ARCHIVE`へ同じアーカイブを指定する。

Entity IDは64bitの十進文字列。Asset表はID・static/skeletal/effect/animation・Sceneファイルからの相対パスを持つ。cmesh/cskin/canim内のIDとの一致も読み込み時に検査する。共通Skeleton・Clipの所有と互換性は[shared-animation.md](shared-animation.md)を参照。絶対パスは保存せず、Save Asで相対パスを再計算する。同一ボリューム上の配置を要求する。Sceneは信頼するローカル制作物であり、相対パスの`..`は許可する。

保存対象は配置・初期再生設定・カメラ。再生中の時刻、選択、GPU handleは保存しない。入力境界で重複ID、参照切れ、親/追従の循環、階層深度、有限値、Quaternion、可逆性、版、件数/ファイルサイズを検査する。現在は最大10,000オブジェクト/アセット、親階層128、JSON 16 MiBの入力保護。性能保証ではない。

保存先は`.cscene`のみ。参照アセットとの同一パス・hardlink衝突を拒否し、検証済みの内容を同じディレクトリの一時ファイルに書き、再読込後に置換する。失敗時は旧ファイルを保持する。Open/Reloadは候補のアセット・個体・Joint参照をすべて準備してから交換する。通常の失敗ではDocument・再生状態・GPU世代を保持する。デバイスロストは復旧せず致命エラーとして扱う。

配置編集では変更のないアセット/再生状態を再利用する。共有AssetのreloadはそのIDを使う全個体へ反映し、対象個体の再生は保存初期値へ戻る。無関係な個体は再生を維持する。Scene全体のOpen/Reloadは全初期状態を復元する。同期処理とGPU idle待機を維持しており、大量のSceneでは編集時に停止時間が発生する。

## 実行例

Game単独のリポジトリ直下で:

```powershell
cmake --preset vs2026
cmake --build --preset debug --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File assets/Scenes/Prepare.ps1 -Cooker out/build/vs2026/Debug/ChromalisAssetCooker.exe
& out/build/vs2026/Debug/ChromalisGame.exe --scene assets/Scenes/Example.cscene
```

サンプルは既存のCube・RiggedFigure・Pulseから3静的モデル、2骨格モデル、Socket追従VFXを作る。素材ライセンスは各素材のREADMEを参照する。`.cscene`は本プロジェクト作成の配置データ。`.cmesh`/`.cskin`は生成物でコミットしない。

従来の`--asset`/`--asset2`/`--animation`/`--vfx`は互換プレビューSceneへ変換する。2スロット制限はこの旧CLI設定だけに残り、SceneやRendererの制約ではない。`--scene`と旧アセット指定を併用しない。

EditorはScene pathのNew/Open/Save/Save As/Reload、Hierarchyの追加・親変更・子孫削除、InspectorのTransform/Asset、Animation/VFXの初期設定とプレビュー操作を提供する。手入力のアセットパスは作業ディレクトリ基準、保存後はScene相対へ変換する。Cookerは選択オブジェクトへ割り当てる。未保存の配置変更を破棄する操作には確認を出す。

制作アセットのカタログはEditor側が所有する。Gameはカタログを読み込まない。同じAsset IDの出力先変更は候補検証後にScene全参照へ反映し、Sceneを未保存状態にする。カタログからのVFX割り当ても指定された安定IDを維持する。変換失敗とScene反映失敗は別段階で扱い、後者では旧描画を保持する。

## 制限

Undo/Redo、Prefab、非同期Streaming、汎用ECSは未実装。Scene APIはメインスレッドのフレーム境界で使い、DrawItemの参照を次の編集/更新まで保持しない。Socketは位置追従だけ。VFX同時数の間隔は従来の検証用World Xオフセットを維持する。日本語コメントを使用するがImGuiの日本語フォントは未導入。Scene JSONの日本語名・パスは保持できるがUI上の日本語名表示は未対応。

Editorは専用Scene Viewとギズモ、CPU三角形選択を提供する。Pickは読み込み済みの静的位置/Index、骨格の現在Paletteを使う。クリック時のみ姿勢を計算し、GPU readbackは不要。負Scale・Local TRSに戻せないShearを伴うギズモ変更は拒否する。Alpha抜きや裏面カリングは選択判定に含まない。

EditとPlayは別のSceneRuntimeを使う。Playは保存対象の初期設定から始め、Stopは実行コピーを破棄する。編集用の未保存状態や一時プレビュー時刻は変更しない。Sceneファイルの保存・読込はEditのみ。GameはこのEditorSessionに依存せず、既存の更新経路を使う。

RendererのScene Surfaceはフレーム記録前に設定する。変更/破棄はGPU idle後、描画時はPS_RESOURCE→RENDER_TARGET→PS_RESOURCEと遷移し、BeginOverlayでSwap Chainに戻す。SRVは呼び出し側の所有で、Surfaceを先に無効化してからDescriptor heapを破棄する。sRGB RTVで符号化しUNORM SRVでImGuiへ渡す。GameではSurfaceを作らず直接描画する。

`scene.cpu`は階層/ID/保存/失敗時保持、`scene.warp`は共有メッシュと3体の独立Paletteの画素、Socket解除、再読み込み、リサイズ/破棄を検証する。既存0A/B/Cテストも継続する。WARPは性能評価ではない。
