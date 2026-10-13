# アセット制作・納品フォーマット

現在のCHROMALISへ取り込める制作物の仕様。モデル・Animation・VFXの制作者向けに、制作元とゲーム用変換結果を区別する。2026-10-13時点の実装に基づく。glTF全機能や任意のDCC出力を受け入れる仕様ではない。

## 1. ファイルの種類

| 制作物 | 制作・受け渡し形式 | ゲームで読む形式 | 作成方法 |
|---|---|---|---|
| 静的モデル | glTF 2.0 `.glb` / `.gltf`＋依存ファイル | `.cmesh` v1 | AssetCookerで変換 |
| 骨格付きモデル | glTF 2.0 `.glb` / `.gltf` | `.cskin` v2（旧v1も読込可） | AssetCookerで変換 |
| 共通Skeleton・Clip | glTF 2.0 `.glb` / `.gltf` | `.canim` v1 | AssetCookerで変換 |
| モデルのBase Color | PNG / JPEG、glTFに参照または埋め込み | モデル内のRGBA8画像 | モデルと一緒に変換 |
| VFX | Effekseer 1.80.7 `.efkproj`＋PNG等の依存素材 | `.efkefc`＋依存素材 | 同版のEffekseerで出力 |
| 配置・材質・照明 | CHROMALIS Editor | `.cscene` v4（旧v1〜v3も読込可） | Editorで保存 |

制作元のDCCファイルは編集用に保持する。FBX、OBJ、blendなどの直接インポートは未対応。モデルはGLBを標準の受け渡し候補とし、分離テクスチャを更新したい場合はglTFを使える。Runtime用バイナリを手編集しない。

## 2. 共通ルール

- 単位はメートル。glTF標準の右手系・Y upで出力する。Cookerが左手系・Y up・Z forwardへ変換するため、手動で左右反転やUVの上下反転を加えない。
- Animationの時刻は秒。基準姿勢・単位・Rootの向きをモデルとClipで揃える。DCC固有の軸設定ではなく、出力したglTFの向きを確認する。
- `.gltf`の外部bin・画像は制作元ファイルのディレクトリ内、またはその子ディレクトリへ置き、相対参照する。外部ディレクトリへの脱出、絶対URI、ネットワーク参照は使わない。GLB埋め込み画像とbase64 data URIも取り込める。
- Asset IDは1〜256文字のASCII英数字と `- _ / .`。例：`characters/hero-a`、`animations/player-common`。再生成時も同じIDを維持する。ファイルパスやGPUハンドルとは別の識別子。
- 保存先は制作元と分ける。出力や任意reportに入力・依存素材と同じパスを指定しない。
- 名前・パスのUTF-8保存には対応するが、Editorの日本語フォントは未導入。画面で識別する骨名・Clip名・IDにはASCIIの分かりやすい名前を推奨する。

配置例（フォルダ名は規約案であり、固定の読込条件ではない）：

```text
assets/Characters/HeroA/
  HeroA.glb
  README.md                 # 出典・利用条件・制作方法
assets/Animations/PlayerCommon/
  PlayerCommon.glb
out/cooked/
  HeroA.cskin
  PlayerCommon.canim
```

## 3. 静的モデル

| 項目 | 制作条件 |
|---|---|
| 形状 | 三角形リスト。複数Mesh・Primitive・材質を使用可能。線・点・Morph・Draco圧縮は不可 |
| Scene | glTFのdefault sceneを使用。指定がなければ最初のscene。出力前に対象を整理する |
| 頂点 | POSITION必須。NORMALを出力することを推奨。法線省略時は角を分離してフラット法線を生成する |
| UV | Base Color Textureを使う場合はTEXCOORD_0必須。第2UVセットやglTFのTexture Transformは不可 |
| Index | 8/16/32bit、またはIndexなし。Sparse Indexは不可 |
| Transform | 親子Transformを頂点へ焼き込む。可逆な小スケール・非一様・反転Transformを扱える。ゼロ・非有限・数値的に不安定な変換は拒否 |
| Animation | 静的出力にSkin・Animationを含めない。必要なら骨格モデルとして別出力 |

三角形化と法線をDCC側で確定すると、陰影を予測しやすい。静的インポートの反転Transform対応と、Editorの負スケール非対応は別の条件。

## 4. キャラクター・共通Skeleton

プレイアブルキャラクターは共通SkeletonとAnimationを再利用する。本番Skeletonはまだ未決定。検証用RiggedFigureの骨名や体格を本番仕様として固定しない。

- モデルは1 Skin。JOINTS_0 / WEIGHTS_0を持ち、1頂点あたり最大4影響。追加のJoint/Weightセットは不可。Weightは非負で合計が正であること。Cookerが正規化する。
- 骨格モデルはNORMAL必須。UV・材質は静的モデルと同じ条件。
- Skin Joint、親子階層、Inverse Bindを正しく出力する。Rest PoseとBind Poseは同一とは限らないため、Editorで両方を確認する。
- 骨と祖先・補助ノードを含む入力全体は最大256ノード。無関係なカメラ・ライト・オブジェクトを出力に混ぜない。
- 位置・Quaternion回転・正のScaleで表せるTransformを使用する。負／ゼロScale・Shear・Morph・複数Skinは未対応。
- 共通Skeletonの骨名は一意にする。各モデルで骨名・親子関係・ローカルRest Transformを保持する。Mesh、Weight、Inverse Bindはモデルごとに持てる。
- 兄弟ノードやJoint Indexの格納順は違ってもよい。名前で対応付ける。ただし骨名だけ同じで体格・基準姿勢が異なるSkeletonは互換ではない。

別の骨長・階層へのRetargetingはない。モデリング時に共通Skeletonを変更した場合は、利用する全モデルとClipを再確認する。詳細な互換判定は[共通Animation仕様](shared-animation.md)を参照。

## 5. Animation Clip

- モデル `.cskin` と共通ライブラリ `.canim` を分離できる。モデル単体はClipなしでよい。
- 共通ライブラリは1つのglTF/GLB内に、共通Skeletonと名前付きClipを1〜64個含める。Clip名は一意にする。
- 位置・回転・Scaleの **LINEAR補間** のみ。STEP / CUBICSPLINE、IK、Constraint等はDCCでLINEARキーへベイクする。固定のサンプリングfps指定はないため、動きに必要な精度で出力する。
- SkinなしのAnimation専用出力も可能。この場合は入力全ノードをSkeletonに採用するので、共通Armature全体を含める。Skin付き出力ではJointと祖先以外のモデル固有ノードへのAnimationを入れない。
- クリップ範囲の切り出し、複数ファイルのClip結合はDCCで行う。CHROMALIS内でのClip編集機能はない。
- ループ・速度・開始時刻・Clip選択はEditorで設定する。ループの継ぎ目は制作側で調整する。
- Rootのキーは姿勢へ反映するが、Root MotionによるScene上の実移動には変換しない。現在の移動用素材には、その場で動くClipを推奨する。
- Joint/Socketはワールド位置を取得できる。VFX追従は位置だけで、回転・Scaleの継承は未対応。

## 6. テクスチャ・マテリアル

| 項目 | 現在の契約 |
|---|---|
| 色画像 | PNG / JPEG。Base ColorはsRGBとして読み込む。色プロファイルの変換処理はない |
| 透明度 | PNGのStraight Alpha。OPAQUEまたはMASK。半透明メッシュのBLENDは不可 |
| Alpha Cutoff | 有限かつ0以上。1超は全透明の意味を保持。1へ丸めない |
| 色係数 | glTFのBase Color Factor、Editorの色・照明・Emissionは線形値 |
| UV | TEXCOORD_0。UV/画像の上下はCookerで反転しない |
| Sampler | Repeat / Clamp / Mirror、Nearest / Linear。現状はmagFilter由来で、Mipは生成しない |
| 材質 | Base Color、両面、MASK、KHR_materials_unlit。複数材質可能 |

PBRのMetallic/Roughness値は保持するが描画に使用しない。Normal/AO/Emissive等の追加マップや頂点カラーに依存する画作りはできない。未対応の必須glTF拡張は拒否する。任意の追加情報が読み込みを通っても、表現が再現されたとは判断しない。

Toonの閾値・Softness・暗部色、Emission色、投影影の設定はCHROMALISのSceneへ保存する。DCCのToonシェーダーやノード構成は移植されない。材質テクスチャはモデルに含まれ、Editorから別画像へ直接差し替える機能はない。画像を更新してモデルを再変換する。

材質Slot番号を再出力時に入れ替えるとSceneの割り当て先が変わる可能性がある。Slot順を維持し、再生成後に外観を確認する。影は静的・骨格ともOPAQUE/MASKに対応し、材質ごとにCast/Receiveを設定できる。詳細：[材質](materials.md)、[照明・影](lighting-shadows.md)。

## 7. VFX・AI素材

標準の接続経路は **Effekseer 1.80.7**。`.efkproj`を編集元として残し、同版ツールから`.efkefc`へ出力する。参照PNG等も相対配置を維持して渡す。`.efkefc`だけで全素材が埋め込まれるとは扱わない。

1. AI画像または手描き・手続き生成素材を用意する。
2. 背景・不要な縁色を除去し、Straight AlphaのPNGへ整える。外周に余白を取り、必要ならFlipbookを分割・配置する。
3. 色画像はsRGB、Noise・Normal・UV等の数値画像はLinear Dataとして役割を区別する。Alphaは線形値で、RGBへ事前乗算しない。
4. Effekseerで効果を構成する。最初は検証済みのSprite、Alpha Blend／Additive、深度テストあり・深度書込なしを基準にする。
5. `.efkefc`を再出力し、EditorでReload・Playする。Assetsカタログには依存素材を明示登録する。全依存の自動収集はない。

Premultiplied Alpha、背景Distortion、Soft Particle、Custom Material、音声、GPU Particleは現在の対応経路に含めない。Flipbook・Noise・Ribbon等はライブラリに機能があるが、CHROMALISでの個別検証が必要。SDR出力のため強い加算色は飽和し、HDR/Bloomは使えない。

AI素材そのものの制作工程は未検証。既存の検証素材は手続き生成PNG。制作に使う場合はサービス名・生成日・利用条件・加工履歴を記録する。[同梱VFXの再出力手順](../assets/Vfx/README.md)も参照。

## 8. 変換・更新手順

以下はDevelopmentルートで、Debug版をビルド済みの場合。入力は実際の制作物へ置き換える。

```powershell
& out/build/vs2026/Debug/ChromalisAssetCooker.exe assets/Prop.glb out/cooked/Prop.cmesh --id props/example
& out/build/vs2026/Debug/ChromalisAssetCooker.exe assets/Hero.glb out/cooked/Hero.cskin --id characters/example
& out/build/vs2026/Debug/ChromalisAssetCooker.exe assets/Common.glb out/cooked/Common.canim --id animations/player-common
```

EditorのAssetsで制作元・出力・IDを登録し、Rebuild + reloadで反映する。モデルと共通Animationは別項目として登録する。VFXは外部Effekseerで再出力後にReloadする。

変換失敗時は旧変換ファイルを保持し、Sceneへの更新失敗時は旧表示を保持する。ただしCook成功後にScene互換性検査だけ失敗した場合、ディスクは新版・画面は旧版になる。終了コードとEditor診断を確認する。成功reload後のAnimationは保存された初期設定へ戻る。

## 9. 納品前の確認と上限

- 原寸・向き・法線・UV・両面・透明部分が意図どおりか。
- Bind/RestおよびClipの開始・中間・終了で異常変形がないか。共通ライブラリを複数個体へ割り当てられるか。
- Toon・投影影・VFXを同じSceneで確認したか。DCCの表示だけで完成としない。
- 依存ファイルを含めて再変換できるか。再読み込み後も材質SlotとClip名が合うか。
- 元データ、出典URL、作者、ライセンス、改変内容をREADME等に記録したか。PublicのGameリポジトリには再配布可能な素材だけを含める。

現在の入力保護上限は、ソース＋外部Buffer合計512 MiB、頂点200万、Index600万、入力材質1023・画像255、画像は各辺8192以下かつ16,777,216画素以下、デコード画像合計256 MiB。Animationは256ノード、64 Clip、合計100万キー、1 Clip最大3600秒。上限以内でも高速動作を保証しない。これらは恒久的な制作予算ではなく、まず代表素材を取り込んで品質・メモリ・負荷を測る。

本番の異なるキャラクターや全DCCの互換性は未検証。最初の1体・1効果を実機で確認してから量産する。
