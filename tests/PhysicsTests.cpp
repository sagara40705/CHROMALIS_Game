#include "physics/PhysicsWorld.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace chromalis;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void Reject(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Require(rejected, "Invalid physics operation accepted");
}
void Near(float actual, float expected, float tolerance, const char* message) { Require(std::abs(actual - expected) < tolerance, message); }
SceneDocument Falling() {
    SceneDocument scene;
    const auto floor = scene.Add("Floor");
    scene.Entity(floor).collider.enabled = true;
    scene.Entity(floor).collider.size = {20, 1, 20};
    scene.Entity(floor).local.position = {0, -0.5f, 0};
    for (int i = 0; i < 3; ++i) {
        const auto id = scene.Add("Falling"); auto& e = scene.Entity(id);
        e.collider.enabled = e.rigidBody.enabled = true;
        e.collider.shape = static_cast<ColliderShape>(i);
        e.local.position = {static_cast<float>(i * 3), 4, 0};
        e.rigidBody.lockRotation = true;
    }
    return scene;
}
void Collision() {
    auto scene = Falling(); PhysicsWorld world(scene);
    Require(world.BodyCount() == 4 && world.ActiveCount() == 3, "Body creation count");
    world.Step(1.0f / 120); Require(world.Steps() == 0, "Fixed step ran early");
    world.Step(1.0f / 120); Require(world.Steps() == 1, "Fixed step accumulator");
    for (int i = 0; i < 300; ++i) world.Step(1.0f / 60);
    const auto poses = world.Poses();
    Near(poses[1].transform.position[1], 0.5f, 0.03f, "Box penetrated floor");
    Near(poses[2].transform.position[1], 0.5f, 0.03f, "Sphere penetrated floor");
    Near(poses[3].transform.position[1], 1, 0.03f, "Capsule penetrated floor");
    const auto hit = world.Raycast({0, 5, 0}, {0, -1, 0}, 10);
    Require(hit && hit->entity == scene.entities[1].id, "Raycast entity mapping");
    Near(hit->position[1], 1, 0.04f, "Raycast contact point");
    Require(!world.Raycast({30, 5, 0}, {0, -1, 0}, 10), "Ray miss");
    const auto steps = world.Steps(); world.Step(0); Require(world.Steps() == steps, "Zero delta advances physics");
    world.Step(10); Require(world.Steps() <= steps + 6, "Unbounded catchup");
    Require(scene.entities[1].local.position[1] == 4, "Physics modified authoring data");
    Reject([&] { world.AddImpulse(scene.entities[0].id, {0, 1, 0}); });
    Reject([&] { world.Step(-1); });
    Reject([&] { world.Raycast({}, {}, 1); });
}
void Motion() {
    SceneDocument scene; const auto id = scene.Add("Body"); auto& e = scene.Entity(id);
    e.collider.enabled = e.rigidBody.enabled = true; e.rigidBody.gravityFactor = 0;
    e.rigidBody.mass = 2; e.rigidBody.linearDamping = 0; e.collider.center = {1, 0, 0};
    e.local.rotation = {0, std::sqrt(0.5f), 0, std::sqrt(0.5f)};
    PhysicsWorld world(scene);
    world.AddImpulse(id, {0, 0, 4});
    Near(world.Velocity(id)[2], 2, 0.001f, "Impulse mass/handedness");
    for (int i = 0; i < 60; ++i) world.Step(1.0f / 60);
    Near(world.Poses()[0].transform.position[2], 2, 0.01f, "Offset/rotation/positive Z motion");
    Near(world.Poses()[0].transform.position[0], 0, 0.001f, "Center offset leaked into Transform");
    world.SetVelocity(id, {1, 0, 0}); Near(world.Velocity(id)[0], 1, 0.001f, "Velocity setter");
    Reject([&] { world.SetKinematicTarget(id, e.local); });
    e.rigidBody.motion = BodyMotion::Kinematic; PhysicsWorld kinematic(scene);
    auto target = e.local; target.position = {3, 2, 1};
    kinematic.SetKinematicTarget(id, target); kinematic.Step(1.0f / 60);
    for (size_t i = 0; i < 3; ++i) Near(kinematic.Poses()[0].transform.position[i], target.position[i], 0.001f, "Kinematic target");
    target.scale[0] = 2; Reject([&] { kinematic.SetKinematicTarget(id, target); });
}
void Persistence(const std::filesystem::path& root) {
    auto scene = Falling(); auto& body = scene.entities[1].rigidBody;
    body.mass = 2; body.friction = 0.7f; body.restitution = 0.4f; body.continuous = true; body.velocity = {1, 2, 3};
    scene.entities[1].collider.center = {0.1f, 0.2f, 0.3f};
    SaveScene(scene, root / "physics.cscene"); const auto loaded = LoadScene(root / "physics.cscene");
    const auto& actual = loaded.entities[1];
    Require(actual.collider.enabled && actual.collider.center == scene.entities[1].collider.center &&
        actual.rigidBody.enabled && actual.rigidBody.velocity == body.velocity && actual.rigidBody.mass == 2 &&
        actual.rigidBody.continuous && actual.rigidBody.lockRotation && actual.rigidBody.friction == 0.7f &&
        actual.rigidBody.restitution == 0.4f && loaded.entities[3].collider.shape == ColliderShape::Capsule, "Physics persistence");
    std::ifstream saved(root / "physics.cscene");
    std::string legacy{std::istreambuf_iterator<char>(saved), {}};
    legacy.replace(legacy.find("\"version\": 9"), 12, "\"version\": 8");
    { std::ofstream file(root / "legacy.cscene"); file << legacy; }
    const auto old = LoadScene(root / "legacy.cscene");
    Require(!old.entities[1].collider.enabled && !old.entities[1].rigidBody.enabled, "Legacy scene acquired physics");
    auto bad = scene; bad.entities[1].parent = scene.entities[0].id; Reject([&] { PhysicsWorld invalid(bad); });
    bad = scene; bad.entities[1].collider.enabled = false; Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[2].local.scale[0] = 2; Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[3].collider.height = 1; Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[1].rigidBody.mass = 0; Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[1].collider.radius = std::numeric_limits<float>::quiet_NaN(); Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[1].collider.size[0] = std::numeric_limits<float>::max(); Reject([&] { ValidateScene(bad); });
}
void Continuous() {
    SceneDocument scene; const auto floor = scene.Add("Thin floor"), bullet = scene.Add("Fast sphere");
    scene.Entity(floor).collider.enabled = true; scene.Entity(floor).collider.size = {10, 0.05f, 10};
    scene.Entity(floor).local.position[1] = -0.025f;
    auto& body = scene.Entity(bullet);
    body.collider.enabled = body.rigidBody.enabled = true; body.collider.shape = ColliderShape::Sphere; body.collider.radius = 0.1f;
    body.local.position[1] = 2; body.rigidBody.velocity = {0, -200, 0}; body.rigidBody.gravityFactor = 0; body.rigidBody.continuous = true;
    PhysicsWorld world(scene); world.Step(1.0f / 60);
    Require(world.Poses()[1].transform.position[1] > 0.08f, "Continuous sphere tunneled through thin floor");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Expected output directory");
        const auto root = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(root);
        Collision(); Motion(); Persistence(root); Continuous();
        std::cout << "PHYSICS PASS: shapes, collision, fixed step, motion, center, handedness, raycast, persistence, rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
