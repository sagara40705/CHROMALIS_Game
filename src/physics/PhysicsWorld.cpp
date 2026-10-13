#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include "physics/PhysicsWorld.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace chromalis {
namespace {
void Need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Finite(const std::array<float, 3>& values) {
    for (float value : values) Need(std::isfinite(value), "Nonfinite physics vector");
}
// 左手系との境界はZ反転に統一する。回転は軸性ベクトルなのでQuaternionのX/Yを反転する。
JPH::Vec3 Vector(const std::array<float, 3>& v) { return {v[0], v[1], -v[2]}; }
std::array<float, 3> Vector(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), -v.GetZ()}; }
JPH::Quat Rotation(const Transform& t) { return JPH::Quat(-t.rotation[0], -t.rotation[1], t.rotation[2], t.rotation[3]).Normalized(); }
struct Registration {
    Registration() { JPH::RegisterDefaultAllocator(); JPH::Factory::sInstance = new JPH::Factory; JPH::RegisterTypes(); }
    ~Registration() { JPH::UnregisterTypes(); delete JPH::Factory::sInstance; JPH::Factory::sInstance = nullptr; }
};
struct Layers final : JPH::BroadPhaseLayerInterface {
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() == 0 ? "Static" : "Moving"; }
#endif
};
struct PairFilter final : JPH::ObjectLayerPairFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a != 0 || b != 0; }
};
struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override { return a != 0 || b.GetValue() != 0; }
};
}
struct PhysicsWorld::Impl {
    Layers layers;
    PairFilter pairs;
    BroadFilter broad;
    JPH::PhysicsSystem system;
    JPH::TempAllocatorImplWithMallocFallback allocator{16 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    struct Body {
        JPH::BodyID id;
        BodyMotion motion;
        JPH::Vec3 offset;
        Transform target;
    };
    std::vector<Body> bodies;
    std::vector<PhysicsPose> poses;
    std::unordered_map<EntityId, size_t> indices;
    double accumulator = 0;
    uint64_t steps = 0;
    ~Impl() {
        auto& api = system.GetBodyInterface();
        for (const auto& body : bodies) { api.RemoveBody(body.id); api.DestroyBody(body.id); }
    }
};
PhysicsWorld::PhysicsWorld(const SceneDocument& document) {
    ValidateScene(document);
    static Registration registration;
    impl_ = std::make_unique<Impl>();
    auto& p = *impl_;
    const auto count = static_cast<JPH::uint>(std::count_if(document.entities.begin(), document.entities.end(), [](const auto& entity) { return entity.collider.enabled; }));
    p.system.Init(std::max(1024u, count), 0, std::max(2048u, count * 16), std::max(2048u, count * 16), p.layers, p.broad, p.pairs);
    p.system.SetGravity(JPH::Vec3(0, -9.81f, 0));
    p.bodies.reserve(count); p.poses.reserve(count); p.indices.reserve(count);
    auto& api = p.system.GetBodyInterface();
    for (const auto& entity : document.entities) {
        const auto& c = entity.collider; const auto& b = entity.rigidBody; const auto& t = entity.local;
        if (!c.enabled) continue;
        const auto motion = b.enabled ? b.motion : BodyMotion::Static;
        JPH::ShapeSettings::ShapeResult result;
        if (c.shape == ColliderShape::Box)
            result = JPH::BoxShapeSettings(JPH::Vec3(c.size[0] * t.scale[0], c.size[1] * t.scale[1], c.size[2] * t.scale[2]) * 0.5f, 0).Create();
        else if (c.shape == ColliderShape::Sphere) result = JPH::SphereShapeSettings(c.radius * t.scale[0]).Create();
        else result = JPH::CapsuleShapeSettings((c.height * 0.5f - c.radius) * t.scale[0], c.radius * t.scale[0]).Create();
        if (result.HasError()) throw std::runtime_error("Collider creation: " + std::string(result.GetError().c_str()));
        const auto offset = Vector(std::array<float, 3>{c.center[0] * t.scale[0], c.center[1] * t.scale[1], c.center[2] * t.scale[2]});
        const auto rotation = Rotation(t);
        const auto type = motion == BodyMotion::Static ? JPH::EMotionType::Static : motion == BodyMotion::Dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Kinematic;
        JPH::BodyCreationSettings settings(result.Get(), Vector(t.position) + rotation * offset, rotation, type, motion == BodyMotion::Static ? 0 : 1);
        settings.mUserData = entity.id;
        settings.mFriction = b.friction; settings.mRestitution = b.restitution; settings.mGravityFactor = b.gravityFactor;
        settings.mLinearDamping = b.linearDamping; settings.mAngularDamping = b.angularDamping;
        settings.mLinearVelocity = Vector(b.velocity);
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = b.mass;
        if (b.lockRotation) settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        if (b.continuous) settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        const auto id = api.CreateAndAddBody(settings, motion == BodyMotion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        Need(!id.IsInvalid(), "Jolt body capacity exceeded");
        p.bodies.push_back({id, motion, offset, t});
        p.indices.emplace(entity.id, p.poses.size()); p.poses.push_back({entity.id, t});
    }
    p.system.OptimizeBroadPhase();
}
PhysicsWorld::~PhysicsWorld() = default;
void PhysicsWorld::Step(float seconds) {
    Need(std::isfinite(seconds) && seconds >= 0, "Invalid physics delta");
    auto& p = *impl_;
    constexpr double fixed = 1.0 / 60.0;
    // 長い停止後に無制限の追い付き計算をしない。ゲーム側の更新上限も0.1秒。
    p.accumulator += std::min(double(seconds), 0.1);
    auto& api = p.system.GetBodyInterface();
    while (p.accumulator + 1e-9 >= fixed) {
        for (const auto& body : p.bodies) if (body.motion == BodyMotion::Kinematic) {
            const auto rotation = Rotation(body.target);
            api.MoveKinematic(body.id, Vector(body.target.position) + rotation * body.offset, rotation, static_cast<float>(fixed));
        }
        const auto error = p.system.Update(static_cast<float>(fixed), 1, &p.allocator, &p.jobs);
        Need(error == JPH::EPhysicsUpdateError::None, "Jolt pair/contact capacity exceeded");
        p.accumulator = std::max(0.0, p.accumulator - fixed); ++p.steps;
    }
    for (size_t i = 0; i < p.bodies.size(); ++i) {
        const auto& body = p.bodies[i];
        const auto rotation = api.GetRotation(body.id);
        auto& transform = p.poses[i].transform;
        transform.position = Vector(api.GetPosition(body.id) - rotation * body.offset);
        transform.rotation = {-rotation.GetX(), -rotation.GetY(), rotation.GetZ(), rotation.GetW()};
    }
}
std::span<const PhysicsPose> PhysicsWorld::Poses() const { return impl_->poses; }
void PhysicsWorld::SetKinematicTarget(EntityId entity, const Transform& transform) {
    ValidateTransform(transform);
    auto& body = impl_->bodies.at(impl_->indices.at(entity));
    Need(body.motion == BodyMotion::Kinematic, "Only Kinematic bodies accept Transform targets");
    Need(body.target.scale == transform.scale, "Changing Collider Scale during Play is unsupported");
    body.target = transform;
}
void PhysicsWorld::AddImpulse(EntityId entity, const std::array<float, 3>& impulse) {
    Finite(impulse);
    auto& body = impl_->bodies.at(impl_->indices.at(entity));
    Need(body.motion == BodyMotion::Dynamic, "Impulse requires a Dynamic RigidBody");
    impl_->system.GetBodyInterface().AddImpulse(body.id, Vector(impulse));
}
void PhysicsWorld::SetVelocity(EntityId entity, const std::array<float, 3>& velocity) {
    Finite(velocity);
    auto& body = impl_->bodies.at(impl_->indices.at(entity));
    Need(body.motion == BodyMotion::Dynamic, "Velocity requires a Dynamic RigidBody");
    impl_->system.GetBodyInterface().SetLinearVelocity(body.id, Vector(velocity));
}
std::array<float, 3> PhysicsWorld::Velocity(EntityId entity) const {
    return Vector(impl_->system.GetBodyInterface().GetLinearVelocity(impl_->bodies.at(impl_->indices.at(entity)).id));
}
std::optional<PhysicsHit> PhysicsWorld::Raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction, float distance) const {
    Finite(origin); Finite(direction);
    Need(std::isfinite(distance) && distance > 0, "Invalid ray distance");
    const auto vector = Vector(direction);
    Need(vector.LengthSq() > 1e-12f, "Invalid ray direction");
    const auto delta = vector.Normalized() * distance;
    const JPH::RRayCast ray(Vector(origin), delta);
    JPH::RayCastResult hit;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit)) return {};
    return PhysicsHit{impl_->system.GetBodyInterface().GetUserData(hit.mBodyID), hit.mFraction * distance, Vector(ray.GetPointOnRay(hit.mFraction))};
}
size_t PhysicsWorld::BodyCount() const { return impl_->bodies.size(); }
size_t PhysicsWorld::ActiveCount() const { return impl_->system.GetNumActiveBodies(JPH::EBodyType::RigidBody); }
uint64_t PhysicsWorld::Steps() const { return impl_->steps; }
}
