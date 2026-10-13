#pragma once
#include "gameplay/PlayerMotion.h"
#include "runtime/Input.h"
#include "scene/SceneRuntime.h"

namespace chromalis {
// GameとEditorのPlayが共用する実行状態。保存用Sceneを変更しない。
class ScenePlayback final {
public:
    void Initialize(SceneRuntime& scene);
    void Update(SceneRuntime& scene, const InputState& input, float seconds);
    void ConfigureView(const SceneRuntime& scene, RenderSettings& settings) const;
    const PlayerMotion& Motion() const { return motion_; }
private:
    PlayerMotion motion_;
    EntityId player_ = 0;
    Transform initial_;
    std::array<int, 3> clips_{};
    Locomotion previous_ = Locomotion::Idle;
};
}
