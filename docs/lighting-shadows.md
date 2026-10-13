# 方向光・影とアニメ調ライティング

方向光1灯に、静的/骨格モデル共通の投影影を追加した。既存のDiffuse・Unlit・Toon、環境光、発光色を維持する。新しいライブラリは導入していない。

## 起動・制作手順

Developmentルートから実行する。

```powershell
cmake --build --preset debug --parallel 2 -- /nodeReuse:false
powershell -NoProfile -ExecutionPolicy Bypass -File Game/assets/Scenes/Prepare.ps1 -Cooker out/build/vs2026/Debug/ChromalisAssetCooker.exe
& out/build/vs2026/Debug/ChromalisEditor.exe --scene Game/assets/Scenes/Shadows.cscene
```

1. Camera / backgroundでLight direction、Light、Ambientを調整する。方向はWorld空間でサーフェスから光源へ向かう向き。鉛直の光も扱う。
2. Directional shadowを開き、Enabledを有効にする。Shadow centerを対象の中心へ置き、Coverage width/depthで対象を覆う。幅は正方形の直交投影幅、奥行きは光軸方向の範囲。中心はWorld固定で、カメラには追従しない。
3. Resolutionを選び、Depth biasとNormal biasを調整する。まずPCF radiusを小さくして、不自然な自己影がなくなる最小のBiasを探す。
4. StrengthとShadow tintで投影影の濃さ・色を調整する。PCF radiusを増やすと境界を広げられる。通常のToon境界のSoftnessとは別の設定。
5. モデルのMaterialsで共有定義を作るか選び、Cast shadow / Receive shadowを切り替える。既存のMASK材質はテクスチャの透明部分とBase alpha / Cutoffを影にも適用する。
6. SaveでSceneへ保存する。Playコピーへの編集は従来どおり編集Sceneへ戻さない。アニメーションはPreview in EditまたはPlay sceneで動かして影を確認する。

サンプルは同梱Cube・RiggedFigure・VFXを使用する。2体のキャラクターは共通AnimationとToon定義を共有し、静的モデルと同じ床へ影を落とす。Gameでも同じSceneを読み込める。

## 見た目の調整を分ける

| 設定 | 役割 |
|---|---|
| Light / Ambient | 直接光と環境光。線形色・強度 |
| 材質のToon Threshold / Softness / Shadow | 法線と光の角度による明暗の分かれ方・暗部色 |
| Directional shadowのStrength / Tint | 他の形状に遮られた直接光の濃さ・色 |
| PCF radius | シャドウマップのTexel単位のぼかし幅。0は1回、0超は3×3の比較サンプル |
| Depth bias | 正規化した影深度から引く値。大きすぎると接地影が離れる |
| Normal bias | World法線方向の受影位置補正。実際の補正量は値×(1−N dot L) |
| Coverage / Resolution | 対象範囲と細部の解像度。範囲を広げるほど1Texelが大きくなる |

PCFは比較Samplerの線形フィルタを使うため、radius=0でも境界にサブTexelの補間がある。物理的な面光源や距離に応じたペナンブラではない。低解像度・広い範囲・広いPCF・斜面では自己影が出やすい。範囲を絞るか解像度を上げ、必要ならBiasを増やす。256〜4096の5段階は現在の実装上限で、ゲーム全体の性能目標ではない。

投影影は直接光だけに作用する。環境光とEmissionは残る。Unlitは投影影を受けないが、Cast shadowが有効なら他の物体へ影を落とす。Toonの角度による暗部と投影影を別々に調整できるので、重なって暗すぎる場合はAmbient、Strength、Tintを使う。

## 描画・所有権

RendererがR32 typelessの影深度Texture、D32 DSV、R32_FLOAT SRV、比較SamplerとStatic/Skinningの影PSOを所有する。パス順は「影深度 → 不透明/MASK → VFX → ImGui」。影は色テクスチャではなく線形深度で、sRGB変換しない。通常のScene色合成とSDR出力経路は維持する。

影パスと本描画は同じWorld・Bone Paletteを使う。影パス専用のAnimation評価やGPU読み戻しはない。UV/Sampler/Alpha MASK・両面設定も共有する。Bone領域は同じ姿勢を参照し、432バイトの描画定数は512バイト整列でパスごとに別領域へ書く。フレームのFence完了後にだけUpload領域を拡張・再利用する。

影深度はGraphics Queue上で`PIXEL_SHADER_RESOURCE → DEPTH_WRITE → PIXEL_SHADER_RESOURCE`と遷移する。同じQueueの順序とBarrierで前フレームの読取も保護する。マルチQueueは使用しない。各モデルのSRV Heap末尾に影用Descriptorを予約し、解像度変更は候補Texture/DSVの生成後にGPU idleを待って差し替え、全Descriptorを更新する。ウィンドウのリサイズと影解像度は独立している。

無効時も起動時の最小接続用Textureは保持するが、影パスは実行せず影深度もサンプルしない。空の描画リストでも有効な影パスは深度をClearし、古い影を残さない。

## 保存と互換性

`.cscene` v4へ影設定と材質のCast/Receiveを保存する。v1〜v3は影無効で読む。材質の投影/受影フラグは旧データでtrueとし、影の全体スイッチを有効にすると利用できる。cmesh・cskin・canimの形式は変更していない。

外部Sceneの範囲・解像度・Bias・色・非有限値を検証し、無効な候補は旧Sceneを保持する。Rendererへ直接渡すRenderSettingsの照明値も境界で検証する。材質だけの変更はMesh/Playerを再作成しない。

## 今回の制約

- 1枚の固定範囲の方向光シャドウ。Cascade、カメラへの自動フィット、光源ごとの複数影、Point/Spot Light、IBLは未実装。
- 範囲外は明るく扱う。範囲の境界で影が切れるため、受影面だけでなく必要な投影元も含める。境界フェードや範囲の自動拡張はない。
- VFXの投影/受影、半透明の透過影、色付き透過光は未対応。MASKの切り抜きは対応する。
- 高速な大群向けの影カリング、LOD、影キャッシュは未実装。対象メッシュは影パスで追加描画する。
- HDR、Bloom、Contact Shadow、PCSS、輪郭線、完成したキャラクター専用シェーダーは未実装。アートの最終調整と本番素材での検証は別途必要。
