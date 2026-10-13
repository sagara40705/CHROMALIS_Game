# 基本再生とClip遷移

AnimationPlayerの姿勢評価にozzのBlendingJobを接続した。ライブラリの追加・版変更、保存形式の変更はない。AnimationRigは引き続き共有し、二つのサンプリング状態・混合姿勢・遷移時間は個体が所有する。

## C++からの操作

- `Play(clipIndex, startTime)`：指定Clipを即時再生する。再開ではなく再スタート。
- `CrossFade(clipIndex, seconds, startTime)`：現在姿勢から指定Clipへ移る。0秒なら即時切替。
- `Pause()` / `Resume()`：Clip時刻と遷移進行を停止／再開。
- `Seek(time)`：有効なClip時刻へ移動。遷移は中止するが再生／停止状態は維持する。
- `Stop()`：遷移を中止し、目標Clipの0秒へ戻して停止。
- `Transitioning()` / `TransitionProgress()`：遷移状態。非遷移時の進行率は1。
- `SocketTransform(joint, world)`：評価済みJointのWorld行列。位置だけなら従来の`Socket`も利用可能。

Clip番号、開始時刻／Seek時刻（0〜Clip長）、遷移時間（有限・0以上）は変更前に検証する。不正な要求で直前の再生状態を置き換えない。速度・Loopなどの設定は既存Playbackで保持する。コードでClipを変更する際は直接代入より上記APIを優先する。

## 遷移の規則

通常の遷移では出発側と到着側の両Clip時刻を進め、Localの移動・回転・スケールを混合してから階層を評価する。出発側は切替時点の速度とLoop設定を保持する。重みは秒単位で線形に変化する。Clip速度0でも再生中なら遷移時間は進む。Pauseでは両方止まる。

途中で再切替した場合はその瞬間の混合Local姿勢を固定した出発姿勢とする。位置・姿勢の連続性を保つが、速度まで連続にするInertializationではない。同じClipへのCrossFadeも再スタートとして扱う。非Loopの到着Clipが終端へ達した場合は、遷移時間の残りによらず目標姿勢で停止する。逆再生も先頭到達時に停止する。

Rest Poseからは混合できる。Bind PoseはInverse Bind由来のモデル空間姿勢であり、通常のLocal TRSとは異なるため、CrossFadeを指定しても即時に通常再生へ戻す。Rest/Bindを明示的に選ぶと進行中の遷移は中止する。

時刻更新 → Localサンプリング／ブレンド → Joint階層 → Skin Palette・法線行列 → Socket → 描画の順序を維持する。SocketTransformは回転・スケールを含む行列を返す（非一様スケールの階層ではShearを含み得る）。Worldは引き続きSimulation/Sceneが所有し、GPUからJointを読み戻さない。

## EditorとHumanoid

InspectorのAnimationでは、保存する`Initial Clip`と、一時的に切り替える`Preview Clip`を分ける。PreviewのTransition秒数、正規化時刻の引継ぎ、進行率、Play/Resume・Pause・Stop・Restart・Seek・コマ送りを使用する。Previewから再生すると`Preview in Edit`も有効にする。Preview Clip・時刻・遷移設定はSceneへ保存しない。

PlayerControllerの`transitionDuration`（初期値0.18秒）で待機・歩行・走行の切替時間を調整できる。既存Sceneには登録時の既定値を補完する。歩行↔走行の正規化時刻引継ぎは維持し、位置リセット操作は即時切替にする。初期設定はBehaviourのScene設定として保存できる。

失敗した再読み込みでは遷移中のPlayer・姿勢・時間を保持する。正常なアセット再読み込みやAnimation初期設定の変更は、従来どおり保存初期値から再構築する。

## 残る制約

- Animation Event、Root Motion、状態機械、レイヤー／加算ブレンド、IK、Retargeting、Clip結合・切り出しは今回の対象外。
- クロスフェード中は移動速度と足の接地を拘束しない。Humanoidの歩行→走行では一時的な足滑りを確認した。足固定IKや速度遷移との同期は未対応。
- 非Loop終端、Bindからの再生、Seekは滑らかな遷移を保証する操作ではない。
- スケールの符号が反転して混合中に0へ到達する素材は、従来の単一Clipのスケール補間と同じく法線行列が特異になる。エラーとして検出する。
- 量産キャラクター数でのCPU負荷測定、初期読込時のozz構築の非同期化は未実施。
