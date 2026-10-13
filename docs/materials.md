# マテリアル制作・描画基盤

静的モデルと骨格モデルで同じ材質を使用する。Cooker → cmesh/cskin → Scene共有定義 → Renderer → Editor調整・保存・再生成の経路を提供する。既存のDiffuse / Unlit / Toonを維持し、Metallic/Roughness PBR、Normal/AO/Emissive、Alpha Blend、Mipを追加した。

## 制作手順

Visual StudioからChromalisEditorを起動し、Workspace scenesで`SurfaceMaterials`を選ぶ。6種類の球でSmooth/Rough/Metal/Normal/Toon/Blendを比較できる。別プロジェクトや専用実行ファイルへの切り替えは不要。

1. DCCでglTF/GLBを出力する。PNG/JPEG、TEXCOORD_0、必要ならTANGENTを含める。
2. AssetCatalogへ登録し、Editorの再生成で取り込む。画像ファイルも依存関係に含まれる。
3. Sceneへ配置し、InspectorのMaterialsで対象Slotを開く。
4. `Create / duplicate definition`でScene材質を作る。Definitionから既存材質を選ぶと共有できる。
5. Shading、Base Color、Emission、Metallic/Roughness、Normal scale、AO strength、Alpha mode、Double sided、投影影の設定を調整する。
6. Saveで保存する。Importedへ戻すとCook済みの値に戻る。Play中の変更は従来どおりStopで編集Sceneへ戻る。

モデルの各MapとSamplerはモデル側の所有。EditorではMapの番号を確認できるが、単独画像の割り当てEditorは設けていない。画像の変更・差し替えは制作元glTF/DCCと再Cookで行う。材質Slotの並べ替え後はScene側の割り当てを確認する。

## 入力と色の契約

| 入力 | チャンネルと用途 | 色空間 |
|---|---|---|
| Base Color | RGB × baseColorFactor、A × factor.a | RGBはsRGB、Aは線形 |
| Metallic/Roughness | G=Roughness、B=Metallic。それぞれ係数を乗算 | 線形 |
| Normal | 接線空間RGBを[-1,1]へ変換。XYへNormal scaleを乗算し正規化 | 線形 |
| Occlusion | R、AO strengthで1との間を補間。PBRの環境拡散光に作用 | 線形 |
| Emissive | RGB × Emission | sRGB |

未指定Mapは白相当、未指定Normalは幾何法線。同じ画像を色とDataの両用途で参照できる。画像リソースとMipは用途別に作成し、必要な用途だけGPUへアップロードする。SamplerはWrap U/VとFilterの18組を共有し、1024材質でもSampler Heap上限を超えない。

Base Color、Emission、照明色は線形値。Cutoffは有限かつ0以上で、1超を丸めない。Metallic/Roughness/AO strengthは0..1、Normal scaleは0..16を入力境界で検証する。glTF画像のAlphaはStraight Alphaとする。

## シェーディング

- **PBR**：GGX法線分布、Smith相関Visibility、Schlick Fresnel、金属度に応じた拡散／鏡面反射。非金属のF0は0.04。Roughnessの数値的下限を内部で設け、ゼロ指定による発散を避ける。
- **Diffuse**：従来のLambert風拡散光。旧cmesh v1の外観を維持する。
- **Unlit**：Base Color + Emission。
- **Toon**：Threshold/SoftnessとShadow色で明暗を調整する。Normal Mapと方向光の投影影を併用できる。

PBRの環境光は一定色の拡散項。IBL／環境鏡面反射は未対応なので、金属の非照射部は暗くなる。SDR出力でHDR/Bloomは行わない。方向光の投影影は直接光へ適用する。

glTFのTANGENTはZ反転とノードの鏡映に合わせて変換する。Skinningでは接線をBoneの線形変換、法線を逆転置で変形する。World変換後に接線を法線へ直交化する。TANGENTがないPrimitiveは描画時の位置・UV微分から基底を作る。これはMikkTSpace生成と同一ではないため、DCCでベイクしたNormal MapにはTANGENTを出力する。退化UVでは幾何法線を使用する。

## 透明・描画順

1. 方向光の深度（OPAQUE/MASK）
2. 不透明・MASK Mesh（深度テスト・深度書き込み）
3. BLEND Mesh（Primitive中心のView Zで奥から手前、深度テストのみ）
4. VFX
5. ImGui

BLENDはRGB=`SrcAlpha/InvSrcAlpha`、Alpha=`One/InvSrcAlpha`。線形空間で合成し、sRGB RTVがSDR変換する。Blend Meshは投影影を落とさない。MASKは本描画と影で同じCutoffを使う。

通常のPrimitiveソートには、交差面・同一Mesh内で前後が交差する三角形・大きく変形したSkinの中心近似による限界がある。VFXとの統合ソート、OIT、色付き透過影、屈折／Transmission／Volumeは未対応。これらを透明対応済みとは扱わない。

## 保存と寿命

- cmesh **v2**：従来データにPBR/Blend、4種の追加MapとSampler、Emission/Normal/AO係数、任意の接線配列を追加。v1を読み込める。v2を古いRuntimeへ渡す場合はRuntime更新が必要。
- cskinは既存コンテナ内のcmeshをv2にする。Skeleton/Clip形式は変更しない。
- cscene **v6**：PBR/Blendと係数を保存。v1〜5も読み込める。
- Cook recipeを更新し、以前のCook結果を再生成対象にする。失敗時は従来の正常データを保持する。
- 色空間を考慮したMipは同期GPUアップロードの準備時にCPUで生成する。sRGBのRGBは線形化して平均し、Alpha/Dataは直接平均する。奇数寸法の端も含める。
- RendererがTexture/Descriptor/PSOを所有する。CPUの材質定義はGPU参照を持たない。
- 定数本体480バイト、Primitiveごとに512バイト境界、影と色で別領域。フレームFence完了後だけ再利用する。Bone領域も個体ごとに分離する。
- Reloadと解放は従来の同期GPU idleを維持する。材質値の編集ではMeshを再生成しない。

## 対応範囲の境界

TEXCOORD_1、KHR_texture_transform、Vertex Color、Clearcoat、Sheen、Transmission、Specular拡張、異方性BRDF、IBL、HDR、圧縮Texture、非同期Streaming、独立した材質ファイルは未対応。非対応UV/Texture transform/必須拡張はCook時に診断する。Samplerは各MapでWrapとmagFilter由来のNearest/Linearを保持し、minFilterを独立に再現しない。Normal MapのMipは平均後にShaderで正規化し、Toksvig等の鏡面エイリアシング対策やMASK coverage保存は行わない。

この範囲を現在の制作基盤とし、すべてのglTF拡張やあらゆる透明表現が完成したとは扱わない。

## 参照資料

- [Khronos glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)：材質Map、接線、色空間、Alpha、BRDFの規約。
- [Direct3D Output Merger](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage)：sRGB RTVと線形Blend。

追加ライブラリはない。cgltf 1.15、Windows SDK/DXC、既存依存の固定版を維持する。
