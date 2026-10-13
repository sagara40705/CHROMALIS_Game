# Jolt物理基盤

Jolt Physics **5.5.0 / MIT**を採用。公式[リリース](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.5.0)のVS2026対応を確認し、CPU剛体機能を利用する。`cmake/Physics.cmake`で公式アーカイブとSHA-256を固定する。ライセンスはGame/Editorの配布先`licenses/JoltPhysics.txt`にコピーする。追加DLLは不要。オフライン構成は同じアーカイブを`CHROMALIS_JOLT_ARCHIVE`へ指定する。

## 制作データ

Scene v9で各Entityに`collider`と`rigidBody`を保存する。v1〜8は両方無効として読み込む。描画MeshとColliderは独立しており、Meshからの自動形状生成は行わない。

- Collider: Box（全幅XYZ）、Sphere（半径）、Capsule（Y軸・半球を含む全高と半径）、ローカルCenter。
- Colliderだけ有効ならStatic。RigidBodyを有効にするとStatic / Dynamic / Kinematicを選べる。
- RigidBody: 質量kg、摩擦、反発係数、重力倍率、線形/角減衰、回転固定、Continuous collision（Jolt LinearCast）、初速m/s。
- 現段階では階層ルートだけ。Socket追従との併用、負Scale、Play中のScale変更を拒否する。Boxは非一様Scale可、Sphere/Capsuleは一様Scaleのみ。Capsule全高は直径より大きくする。
- 入力境界で参照、有限値、寸法を検証する。実寸は0.0001〜10000m、Centerオフセットは各軸10000m以内。これは浮動小数点形状生成の安全範囲であり、描画距離や性能目標ではない。

## 実行と所有権

`SceneDocument`は制作時の設定、`SceneRuntime`はPlay中の`PhysicsWorld`を所有する。JoltのBodyIDは外部へ公開せず、Entity IDで参照する。PhysicsWorldはBody/Shapeを破棄してからJolt Worldを終了する。Rendererに物理の依存は追加しない。

左手系・Y上・m・秒というSceneの契約を維持する。Jolt境界では位置/ベクトルのZ、QuaternionのX/Yを反転する。CenterはScaleと回転を含めてBodyへ変換し、読み戻し時に除去する。Body中心をEntity原点へ誤って書き戻さない。

更新順は以下。

1. Behaviour.UpdateからKinematic目標やImpulse/Velocityを設定する。
2. Joltを固定1/60秒で更新し、確定した位置・回転を実行用Localへ反映する。
3. CPUアニメーション姿勢、World、Socket、VFXを評価する。
4. Behaviour.AfterPoseで確定Worldを参照し、描画する。

DynamicのTransformはJoltが所有する。Static/Dynamicへの`SetRuntimeTransform`は拒否する。Transformを書くBehaviourを併用する場合はKinematicを要求する。Kinematicは指定位置へ進みDynamicを押せるが、壁により自身が停止するCharacter Controllerではない。既存PlayerControllerは従来の平面移動のままで、自動的に物理制御へ変更しない。

`BehaviourContext.scene.Physics()`からPlay中のPhysicsWorldへアクセスできる。`AddImpulse`、`SetVelocity`、`Velocity`、最短ヒットの`Raycast`を提供する。物理のないSceneではnullptr。ポインタをStop後に保持しない。ImpulseはUpdate呼び出し回数に依存するため、単発入力などに使う。連続力の積分を固定tick APIとして公開する実装はまだない。

単一スレッドのJolt JobSystemを使用。1フレームの追い付き時間は0.1秒に制限し、長い停止分を全て再生しない。補間は未実装。重力は(0,-9.81,0)、個体の倍率で調整する。Static同士以外は衝突する。接触容量不足は診断として例外になり、EditorはPlayを停止して制作Sceneを保持する。作業メモリ16MiBを超える場合はmallocへフォールバックする。

Pauseは物理時間を停止し、Stopは実行コピーを破棄する。編集Sceneへの結果の書き戻しは行わない。Play中の物理Sceneの構造変更・アセット差し替えは拒否し、Editorの非同期アセット更新は既存方針どおりStop後に反映する。

## 現在の制限

Mesh/Convex/Compound Collider、Triggerと衝突イベント、カスタム衝突Layer、Joint/Constraint、Ragdoll、Character Controller、Root Motion衝突解決、Play中のBody追加/削除、固定tick Behaviour、描画補間は未実装。大量Bodyの性能評価も未実施。これらを対応済みとして扱わない。

CPU回帰は`physics.cpu`。Editor側のPlay/Pause/Stopと描画接続はDevelopmentの`editor.scene.warp`、実UI操作は`editor.workflow`で確認する。
