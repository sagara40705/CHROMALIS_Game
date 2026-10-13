# 描画・材質の制作基盤

インポート済みモデルを使い、Scene上で材質を共有・調整・保存する最小構成。静的モデルと骨格モデルは共通の材質処理を使う。新しい外部ライブラリは導入していない。

## 制作手順

```powershell
cmake --build --preset debug --parallel 2 -- /nodeReuse:false
powershell -NoProfile -ExecutionPolicy Bypass -File Game/assets/Scenes/Prepare.ps1 -Cooker out/build/vs2026/Debug/ChromalisAssetCooker.exe
& out/build/vs2026/Debug/ChromalisEditor.exe --scene Game/assets/Scenes/Materials.cscene
```

Developmentルートから実行する。Game単独の場合は`Game/`の接頭辞を外す。サンプルでは3個のCubeにDiffuse・Unlit・Toonを割り当て、2体のRiggedFigureで同じToon定義を共有する。既存の共通AnimationとVFXも表示する。

1. モデルを選択し、InspectorのMaterialsを開く。実際にPrimitiveが使用する材質Slot、使用数、テクスチャ番号を表示する。glTF由来の未使用デフォルトSlotは表示しない。
2. Create / duplicate definitionでインポート値を基にScene材質を作る。生成される`material/N`はそのScene内の安定ID。
3. Definitionで既存の定義を選べば、別モデルや別Slotでも共有できる。変更は全利用者へ反映する。個別に変えたい場合は同じボタンで複製する。
4. Lit (diffuse) / Unlit / Toon、Base Color、Emission、Double sided、Alpha MASKを調整する。ToonはThreshold・Softness・Shadow色を持つ。
5. Camera / background内で方向光と環境光を調整する。Light directionはWorld空間でサーフェスから光源へ向かう方向。ゼロ方向は診断して拒否する。全体Unlitは照明確認用の上書き。
6. Save / Save AsでSceneへ保存する。Importedへ戻すとそのSlotの割り当てを解除し、元のモデル値へ戻る。定義自体は再利用のためSceneに残る。

色の数値は線形。Base Colorはテクスチャへ乗算する最終係数で、インポート係数へさらに二重乗算しない。Emissionは乗算後に加算する色で、発光テクスチャやBloomではない。Cutoffは0以上の有限値を許可し、1超のMASKは全透明という意味を維持する。UIのドラッグ範囲は0〜10、保存形式に1上限はない。

再インポートは従来のAssets / updatesから行う。材質を割り当てていないSlotは更新後のインポート値を使い、割り当て済みSlotはScene定義を優先する。Base Color Texture・UV・Samplerは常にモデルのものを使う。読み込みやSlot範囲検証に失敗したら旧Sceneを保持する。

## 責務と保存

| 層 | 内容 |
|---|---|
| Cooker / cmesh・cskin | テクスチャ、UV、Sampler、Primitiveと材質Slot、元の係数。今回形式変更なし |
| MaterialSurface | GPU参照を持たない材質値。Diffuse / Unlit / Toon、色、MASK、両面設定 |
| SceneDocument | 共有材質IDと定義、各EntityのSlot→ID、Scene照明 |
| SceneRuntime | 読み込み・編集時に参照/Slotを検証し、各モデル用の材質配列を解決。PlayerやMeshは変更しない |
| Renderer | Mesh/Texture/PSOとフレーム別定数領域。Static/Skinningの後段は同じPixel Shader |

`.cscene`はv4で保存する。v1/v2は材質割り当てなし・従来の照明値で読み込み、v3は材質を維持して投影影を無効にする。GameにImGuiはリンクしない。Edit/Playの材質はSceneコピーに従い分離する。モデルのAsset IDを変更する割り当て操作では、別モデルへの誤適用を避けてSlot割り当てを解除する。同じIDのreloadでは維持する。

Slotは現在インポート時の番号。DCCで材質順序を変えると、同じSlot数でも意味が変わる可能性がある。再生成時は外観を確認する。材質名による安定した再対応付けは未実装。共有定義はScene内に保存し、独立した材質ファイルやScene間ライブラリはまだ持たない。

## 描画・色・GPU寿命

Base Color TextureはsRGB SRVでデコードし、係数・照明・発光は線形空間で評価する。SceneのsRGB RTVが最後のSDR変換を担当する。ImGuiへは従来どおり符号化済みScene画像をUNORM SRVで渡す。既存VFXの線形合成経路は変更しない。

- Litは`base * (ambient + lightColor * saturate(N dot L) * shadowLight) + emission`の拡散反射。投影影は直接光だけに掛ける。
- Unlitは`base + emission`。
- ToonはN dot Lの閾値をSoftness幅のsmoothstepで切り替え、Shadow色と白の間を補間して方向光へ掛ける。照明の段階化と、別途設定する[投影影](lighting-shadows.md)の色・濃さを組み合わせて調整できる。
- OPAQUE/MASKは深度テスト・深度書き込みあり。両面では裏面法線を反転する。半透明メッシュのAlpha Blendとソートは未対応。
- 描画順は影の深度 → 不透明/MASK → VFX → ImGui。SDR 8-bitなので発光や加算結果は飽和する。

従来のRoot Constantsからフレーム別CBVへ移した。定数本体は432バイト、各Primitiveへ512バイトを割り当てる。影パスと色パスは別の領域を使う。対応フレームのFence完了後にだけ領域を拡張・書き換え、静的/骨格・複数個体/材質の定数が混ざらない。GPU参照中のBufferやDescriptorを編集しない。Scene編集は従来の同期ApplyでGPU idleを待つが、材質だけの変更ではMesh/Texture/Playerを再生成しない。

## 今後の境界

PBRのMetallic/Roughness、Normal Map、AO、複数光源の影、IBL、HDR/Bloom、輪郭線、Rim、透過メッシュ、材質テクスチャ差し替え、Mip生成、独立材質アセットは未対応。元データにあるMetallic/Roughnessを描画しているとは扱わない。シェーダーはCMake/DXCによるビルド時コンパイルで、編集後は再ビルド・再起動する。汎用ノードEditorやRenderGraphは追加していない。
