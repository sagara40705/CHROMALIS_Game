#pragma once
#include "scene/Scene.h"
#include <memory>
#include <optional>
#include <span>

namespace chromalis {
struct PhysicsPose { EntityId entity; Transform transform; };
struct PhysicsHit { EntityId entity; float distance; std::array<float, 3> position; };
// PlayごとのJolt World。Jolt型やBodyIDを永続データ・Gameplayへ漏らさない。
class PhysicsWorld final {
public:
    explicit PhysicsWorld(const SceneDocument& document);
    ~PhysicsWorld();
    void Step(float seconds);
    std::span<const PhysicsPose> Poses() const;
    void SetKinematicTarget(EntityId entity, const Transform& transform);
    void AddImpulse(EntityId entity, const std::array<float, 3>& impulse);
    void SetVelocity(EntityId entity, const std::array<float, 3>& velocity);
    std::array<float, 3> Velocity(EntityId entity) const;
    std::optional<PhysicsHit> Raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction, float distance) const;
    size_t BodyCount() const;
    size_t ActiveCount() const;
    uint64_t Steps() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
