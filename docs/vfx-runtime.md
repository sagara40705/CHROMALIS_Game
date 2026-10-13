# VFXのSocket追従と配置共有

## 制作と追従

InspectorのVFX PlaybackでFollow targetとSocket jointを選び、Follow rotation and scaleをオンにすると、位置・回転・スケールを追従する。オフは従来の位置だけの追従。RootのVFXオブジェクトに設定する。

- 完全追従: `VFX Local × Joint Model × Target World`。TransformのPositionはSocket座標系のオフセットになる。
- 位置のみ: Transformの回転・スケールを維持し、PositionにSocketのWorld位置を加える。
- AnimationのCPU姿勢と参照先Worldを先に確定する。GPU読み戻しは行わない。
- 行列のままEffekseerへ渡す。親の非一様スケールも含むが、SpriteのBillboardや親への追従設定など、素材側の設定によって実際の見え方は変わる。すでに放出した粒子を追従させるかも素材側の設定に従う。
- 追従先削除時は現在のWorld TRSを保持して切り離す。せん断を含みTransformに保存できない場合は、Sceneを変えずに削除を拒否する。先にInspectorで追従を解除して配置を調整できる。
- `.cscene` v8に`effect.followTransform`を保存する。v1〜7は従来の位置のみとして読み込む。

## 所有とフレーム順序

SceneのLoadedAssetが、Asset IDごとのEffekseer Effect・Manager・DX12 Renderer・テクスチャ・2フレーム分の描画領域を所有する。各EntityのVfxPlayerはこれらを共有し、自身の再生ハンドルとWorldだけを保持する。別Asset IDや別SceneRuntime間では共有しない。

1. 各EntityのWorldを計算し、`SetWorld`で自身のハンドルへ反映する。同じ行列の再送信は省略する。
2. 全Entityの反映後、共有元に`Update`を一度呼ぶ。配置数分、時刻が進むことはない。
3. Rendererが該当フレームのFenceを待機した後、共有元に`Draw`を一度呼ぶ。同じフレーム領域を配置ごとにリセットしない。

Scene外で単独利用する場合の`UpdateMatrix`は従来通り利用できる。共有した配置に対して個別にUpdate/Drawを呼ばない。ゲーム側はSceneRuntimeの更新・描画経路を使用する。

Stop・Entity削除は自身のハンドルだけを停止する。停止した内部粒子の回収はManagerの次の更新で行う。Playは候補ハンドルを確保してから旧再生を停止し、確保失敗時は候補だけを停止して旧再生を残す。

再読み込みはSceneRuntime経由で別の共有元を生成し、全参照の候補検証後に置換する。失敗時は旧リソースと再生を維持する。GPU待機後の置換・解放は従来通り。共有中のVfxPlayerへの直接Reloadは拒否する。操作はメインスレッドのフレーム境界で行う。

## 負荷と制限

同一アセットの配置ごとに行っていたロード、Manager更新、Begin/EndRendering、フレーム領域作成を集約した。Inspectorに選択Entityの再生数・粒子数、共有アセット全体の粒子数・描画呼び出し数・残り粒子予算を表示する。粒子数にはEffekseerのRootも含む。

共有Managerの初期粒子予算は4096。以前の「各配置に4096」から「Asset IDごとに4096」になるため、複雑な素材の多重再生では予算に達することがある。残量0の警告を表示するが、Effekseerが省略した粒子を復元するものではない。恒久的なゲーム性能目標ではなく、代表素材の測定で調整する内部予算である。Countの1〜64は既存の個数プレビュー設定で、Scene全体の配置上限ではない。Countの配置間隔は従来通りWorld X方向の0.35m。

透明合成・色空間・深度契約は変更しない。共有グループはSceneのAsset定義順に描画する。異なるAsset IDをまたぐ透明物の完全な奥行きソート、距離LOD、空間カリング、GPU粒子化は未実装。異なる半透明素材が交差する演出は個別検証が必要。Soft Particle・背景歪み・独自Effekseer材質の未対応も継続する。

依存の追加・更新はない。固定済みEffekseer 1.80.7のManager APIと[公式統合ガイド](https://github.com/effekseer/Effekseer/blob/master/docs/Help_Cpp/18x/Guide_Cpp_Ja.md)を参照し、実装時にはローカルの固定版ソースでStopEffect、GetInstanceCount、SetMatrixの動作を確認した。
