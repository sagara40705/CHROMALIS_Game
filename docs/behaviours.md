# C++ Behaviour

ScenePlaybackは個別のプレイヤー処理を持たず、登録されたBehaviourを実行する。実装は`Gameplay/Behaviours`、一覧は同ディレクトリの`behaviours.json`、共通契約は`src/gameplay/Behaviour.h`。

一覧のnameはヘッダー・ソース・クラス名、idはSceneが参照する安定ID。CMake構成時にソースと登録関数を生成する。共通Runtime・Renderer・ApplicationをBehaviour追加のたびに編集する必要はない。Editor生成機能を使わないGame単独開発でも、同じ形式の一覧で登録できる。

各型のDescribeは名前、基本フィールド、factory、Transform書き込み/カメラ所有の宣言、設定版を返す。設定はPlay開始前に検証し、コンストラクタで型付き値に変換する。float/int/bool/string/float3、Entity/Asset参照に対応する。

Start → Update → Jolt固定時間更新 → SceneRuntimeの姿勢・World更新 → AfterPose → ConfigureView → 描画の順。Stopは逆順で一度だけ呼ぶ。Start途中の失敗もStop対象。Stopはnoexceptとし、未初期化の状態でも終了できる実装にする。ScenePlaybackをSceneより先に破棄する。Pause中はUpdate/AfterPoseを実行しない。

BehaviourContextとその中の参照は呼び出し中だけ有効。BehaviourSceneはLocal/World、Animation、VFX、Physicsの操作に限定し、Sceneの保存・差し替え・構造変更やRendererは公開しない。RuntimeTransformが実行Localの正本で、Documentは制作時の初期設定。AfterPoseではWorld/Jointを読むだけとし、Transformを変更しない。

Transformを書き込む型はwritesTransform=trueにし、自分のEntityを更新する。同一Entityに複数のTransform書き手、またはSceneに複数のカメラ所有者がある場合は開始を拒否する。一般のBehaviour間依存・イベント・動的生成削除は今回扱わない。物理Bodyの所有権・Kinematic制御は[physics.md](physics.md)を参照。Root Motionを導入する場合は、姿勢から移動量取得→衝突を解決→World/Socket確定→判定という段階を追加する。GPUから骨を読み戻さない。

Scene v7の各Entityは`behaviours`配列を持つ。各要素は`id`（Entity内の十進文字列）、`type`（安定型ID）、`version`、`enabled`、`settings`（JSON object）。未知の型・設定を読み込みと再保存で保持し、有効な未登録型や非対応版はPlayで拒否する。無効なBehaviourは実体を作らない。

v1〜6のSceneは旧play.playerをPlayerControllerへ移行する。旧設定は互換情報として残るが、移動パラメータの正本はBehaviour設定になる。PlayerControllerは既存のPlayerMotionを使い、平面移動・Idle/Walk/Run・カメラを維持する。Play開始はroot skeletal entity・Clip・配置と範囲を検証する。DLL、Lua、Behaviourの固定tick、Play中の設定差し替えは未実装（物理更新は固定1/60秒）。

CPU設定/ライフサイクルとScene接続の検証は`behaviour.warp`。既存の移動回帰は`player.motion`と`player.scene`を維持する。
