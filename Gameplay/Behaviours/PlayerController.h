#pragma once
#include "gameplay/Behaviour.h"
#include "gameplay/PlayerMotion.h"

namespace chromalis {
// 平面移動・歩行アニメーション・追従カメラの既存契約を維持する。
class PlayerController final : public Behaviour {
public:
    explicit PlayerController(const BehaviourSettings& settings) : settings_(settings) {}
    static BehaviourType Describe();
    void Start(BehaviourContext& context) override;
    void Update(BehaviourContext& context, float seconds) override;
    void ConfigureView(const SceneRuntime& scene, RenderSettings& settings) const override;
    const PlayerMotion& Motion() const { return motion_; }
private:
    BehaviourSettings settings_;
    PlayerMotion motion_;
    EntityId player_ = 0;
    Transform initial_;
    std::array<int, 3> clips_{};
    Locomotion previous_ = Locomotion::Idle;
};
}
