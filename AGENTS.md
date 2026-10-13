# Game / 共通Runtime

このGitリポジトリはPublic。Windows x64 / C++20 / DX12を対象とし、Editorなしでもビルド・実行できる。

- 非公開素材・内部文書・認証情報・個人パスを追加しない。依存と公開素材の版・ライセンスを保持する。
- RuntimeへImGui、制作元glTF解析、Editor依存を持ち込まない。`src/cooker`で制作元を変換し、`src/assets`でRuntime形式を検証する。
- 関係する契約だけ読む: Scene/所有権=`docs/scene.md`、Behaviour=`docs/behaviours.md`、Asset=`docs/static-assets.md`/`docs/asset-authoring.md`、Animation=`docs/shared-animation.md`/`docs/animation-playback.md`、描画=`docs/materials.md`/`docs/lighting-shadows.md`、VFX=`docs/vfx-runtime.md`、物理=`docs/physics.md`。
- DX12資源・Descriptor・Uploadの再利用はFence完了が条件。Scene/Asset IDとGPU handleを混同しない。外部入力を境界で検証し、確定済み条件をホットパスで重複確認しない。
- Behaviour追加は`Gameplay/Behaviours/behaviours.json`とC++を変更する。生成された登録コードを直接編集しない。既存のScene保存互換性・更新失敗時の正常データ保持を維持する。
- コメントは日本語。既存差分を保持し、依頼なしにcommit/pushしない。Game単独で作業する場合、`README.md`のGameルート用Presetsを使う。親から作業中は親のビルドツリーを再利用し、別ツリーを毎回作らない。
- 変更した実行/テストターゲットをビルドし、`ctest --preset debug -N -R '<対象>'`で選択を確認して実行する。CTestはビルドしないため、必要なfixture生成テストのexeも更新する。共通契約変更は利用先も検証する。
