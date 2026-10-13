#include "gameplay/ScenePlayback.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace chromalis {
ScenePlayback::ScenePlayback(ScenePlayback&& other) noexcept { *this = std::move(other); }
ScenePlayback& ScenePlayback::operator=(ScenePlayback&& other) noexcept {
    if (this != &other) {
        Stop(); instances_ = std::move(other.instances_); scene_ = std::exchange(other.scene_, nullptr);
        ownsPhysics_ = std::exchange(other.ownsPhysics_, false);
    }
    return *this;
}
void ScenePlayback::Initialize(SceneRuntime& scene, const BehaviourRegistry& registry) {
    ScenePlayback candidate;
    candidate.scene_ = &scene;
    const auto& document = scene.Document();
    std::set<EntityId> transformWriters;
    bool cameraWriter = false;
    for (const auto& entity : document.entities) for (const auto& definition : entity.behaviours) {
        if (!definition.enabled) continue;
        const auto* type = registry.Find(definition.type);
        if (!type) throw std::runtime_error("Missing Behaviour: " + definition.type + " on " + entity.name);
        if (type->writesTransform && entity.collider.enabled && (!entity.rigidBody.enabled || entity.rigidBody.motion != BodyMotion::Kinematic))
            throw std::runtime_error("Transform-writing Behaviour requires a Kinematic RigidBody: " + entity.name);
        if (type->writesTransform && !transformWriters.insert(entity.id).second)
            throw std::runtime_error("Multiple Transform writers on " + entity.name);
        if (type->ownsCamera && std::exchange(cameraWriter, true)) throw std::runtime_error("Multiple camera owners");
        auto instance = type->create(ResolveBehaviourSettings(*type, definition, document));
        if (!instance) throw std::runtime_error("Behaviour factory returned null: " + type->name);
        candidate.instances_.push_back({entity.id, definition.type, std::move(instance), false});
    }
    // 全設定の検証・生成後にStartする。途中失敗した実体もStopで後始末する。
    scene.StartPhysics();
    candidate.ownsPhysics_ = true;
    const InputState input;
    for (auto& instance : candidate.instances_) {
        BehaviourContext context{scene, instance.entity, input};
        instance.started = true;
        instance.behaviour->Start(context);
    }
    *this = std::move(candidate);
}
void ScenePlayback::Update(SceneRuntime& scene, const InputState& input, float seconds) {
    if (&scene != scene_ || !std::isfinite(seconds) || seconds < 0) throw std::runtime_error("Invalid Behaviour update context/time");
    seconds = std::min(seconds, 0.1f);
    for (auto& instance : instances_) {
        BehaviourContext context{scene, instance.entity, input};
        instance.behaviour->Update(context, seconds);
    }
    scene.StepPhysics(seconds);
}
void ScenePlayback::AfterPose(SceneRuntime& scene, const InputState& input, float seconds) {
    for (auto& instance : instances_) {
        BehaviourContext context{scene, instance.entity, input};
        instance.behaviour->AfterPose(context, std::min(seconds, 0.1f));
    }
}
void ScenePlayback::ConfigureView(const SceneRuntime& scene, RenderSettings& settings) const {
    const auto& document = scene.Document();
    settings.cameraYaw = document.view.yaw; settings.cameraPitch = document.view.pitch;
    settings.cameraDistance = document.view.distance; settings.cameraTarget = document.play.fixedTarget;
    if (document.play.cameraTarget) {
        const auto& world = scene.World(document.play.cameraTarget);
        settings.cameraTarget = {world._41, world._42 + document.play.cameraHeight, world._43};
    }
    for (const auto& instance : instances_) instance.behaviour->ConfigureView(scene, settings);
}
void ScenePlayback::Stop() noexcept {
    if (scene_) {
        const InputState input;
        for (auto it = instances_.rbegin(); it != instances_.rend(); ++it) if (it->started) {
            BehaviourContext context{*scene_, it->entity, input};
            it->behaviour->Stop(context);
        }
    }
    if (scene_ && ownsPhysics_) scene_->StopPhysics();
    instances_.clear(); scene_ = nullptr; ownsPhysics_ = false;
}
const Behaviour* ScenePlayback::Find(EntityId entity, const std::string& type) const {
    for (const auto& instance : instances_) if (instance.entity == entity && instance.type == type) return instance.behaviour.get();
    return nullptr;
}
}
