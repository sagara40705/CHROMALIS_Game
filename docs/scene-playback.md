# Scene再生・平面移動の契約

## Behaviour移行後（Scene v7）

現在はScenePlaybackが登録Behaviourの実行を管理し、以下のプレイヤー処理は`Gameplay/Behaviours/PlayerController`に移動した。v1〜6のplay設定は読み込み時にBehaviourへ移行し、v7ではEntityのbehavioursが実行設定の正本となる。設定場所はInspectorのBehaviour欄。詳細は[Behaviour契約](behaviours.md)。下記の操作・移動規約は移行後も維持する。

Gameの`--scene`とEditorのPlayは同じ`ScenePlayback`を使う。Scene名・オブジェクト名・専用exeによる機能分岐は持たない。

`.cscene` v5の`play`は、playerとcameraTarget（Entity IDの十進文字列、0は操作なし／固定カメラ）、walkSpeed/runSpeed/turnSpeed、forwardYaw、groundHeight、minimum/maximum（XZ）、clips（Idle/Walk/Runの順）、fixedTarget、cameraHeight/cameraFollowを持つ。角度はrad、距離m、時間s。カメラyaw/pitch/distanceは既存viewを使う。v1〜4は操作なしの固定カメラとして読み込み可能。

Play開始時にroot skeletal entity、Clip参照、配置の高さとXZ範囲を検証する。失敗時はEditorの編集Sceneを保持する。現在は操作対象1体。Rは原点ではなく配置時の位置・向き・カメラへ戻す。移動時の向きはY軸回転で、傾斜面・横倒しキャラクターは対象外。Clip再生速度は実移動速度に合わせ、境界で停止したときはIdleへ切り替える。Walk/Run切替は位相を引き継ぐが、姿勢のクロスフェードは未実装。

1. ホストがWin32入力を収集し、EditorはゲームViewの操作権を持つ時だけ渡す。
2. ScenePlaybackが位置・向き・使用Clipを更新する。DocumentのLocalやDirtyは変更しない。
3. SceneRuntime.UpdateがCPU姿勢、親子World、Socket、VFXを評価する。
4. ScenePlayback.ConfigureViewが評価済みWorldからカメラを決め、Rendererが描画する。

Pause中は移動・Clip切替を含む実行更新を行わない。StopはGPU待機後に実行用コピーを破棄する。保存は編集用Documentだけが対象。アセット識別子、Entity ID、GPU handleは混同しない。

CPU移動検証は`player.motion`、姿勢・移動距離・描画・再読込の回帰は`player.scene`。実素材を使う検証は次の形式で行える。

```powershell
& path/to/ChromalisScenePlaybackTests.exe output-directory hardware path/to/PlayerMovement.cscene
```

一般Game起動は`ChromalisGame.exe --scene path/to/Scene.cscene`。素材は事前にCookする。Public GameにはPrivate Humanoid素材を同梱しない。
