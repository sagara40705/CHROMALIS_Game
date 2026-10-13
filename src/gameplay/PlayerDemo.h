#pragma once
#include "gameplay/PlayerMotion.h"
#include "runtime/Application.h"

namespace chromalis {
// デモ固有の操作とClip選択。RendererやAnimation自体は移動ルールを持たない。
class PlayerDemo final : public GameLoop {
public:
    void Initialize(SceneRuntime& scene) override;
    void Update(SceneRuntime& scene, const InputState& input, float seconds, RenderSettings& settings) override;
    const PlayerMotion& Motion() const { return motion_; }
private:
    PlayerMotion motion_;
    EntityId player_ = 0;
    Transform initial_;
    std::array<int, 3> clips_{};
    Locomotion previous_ = Locomotion::Idle;
};
}
