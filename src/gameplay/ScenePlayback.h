#pragma once
#include "gameplay/Behaviour.h"

namespace chromalis {
// Playごとのロジック実体だけを所有する。Sceneより先に終了する。
class ScenePlayback final {
public:
    ~ScenePlayback() { Stop(); }
    ScenePlayback() = default;
    ScenePlayback(const ScenePlayback&) = delete;
    ScenePlayback& operator=(const ScenePlayback&) = delete;
    ScenePlayback(ScenePlayback&& other) noexcept;
    ScenePlayback& operator=(ScenePlayback&& other) noexcept;
    void Initialize(SceneRuntime& scene, const BehaviourRegistry& registry = GameBehaviours());
    void Update(SceneRuntime& scene, const InputState& input, float seconds);
    void AfterPose(SceneRuntime& scene, const InputState& input, float seconds);
    void ConfigureView(const SceneRuntime& scene, RenderSettings& settings) const;
    void Stop() noexcept;
    const Behaviour* Find(EntityId entity, const std::string& type) const;
private:
    struct Instance {
        EntityId entity;
        std::string type;
        std::unique_ptr<Behaviour> behaviour;
        bool started = false;
    };
    std::vector<Instance> instances_;
    SceneRuntime* scene_ = nullptr;
    bool ownsPhysics_ = false;
};
}
