#pragma once
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "assets/Material.h"

namespace chromalis {
using EntityId = uint64_t;
enum class SceneAssetKind { Static, Skeletal, Effect, Animation };

struct Transform {
    std::array<float, 3> position{0, 0, 0};
    std::array<float, 4> rotation{0, 0, 0, 1};
    std::array<float, 3> scale{1, 1, 1};
    bool operator==(const Transform&) const = default;
};
struct SceneAsset {
    std::string id;
    SceneAssetKind kind = SceneAssetKind::Static;
    std::filesystem::path path;
};
struct AnimationInitialState {
    std::string clip;
    float time = 0, speed = 1;
    bool loop = true, autoplay = true, restPose = false, bindPose = false;
    std::string library;
    bool operator==(const AnimationInitialState&) const = default;
};
struct EffectInitialState {
    int count = 1;
    bool autoplay = true;
    EntityId follow = 0;
    std::string joint;
    bool operator==(const EffectInitialState&) const = default;
};
struct BehaviourDefinition {
    uint64_t id = 1;
    std::string type;
    uint32_t version = 1;
    bool enabled = true;
    // 未登録の型の設定も失わない。解釈と型検証はPlay開始時に行う。
    std::string settings = "{}";
};
struct SceneEntity {
    EntityId id = 0, parent = 0;
    std::string name = "Object", asset;
    Transform local;
    AnimationInitialState animation;
    EffectInitialState effect;
    struct MaterialBinding { uint32_t slot = 0; std::string material; };
    std::vector<MaterialBinding> materials;
    std::vector<BehaviourDefinition> behaviours;
};
struct SceneView {
    std::array<float, 4> background{0.055f, 0.07f, 0.11f, 1};
    float yaw = 0.35f, pitch = 0.25f, distance = 7;
    bool unlit = false;
    SceneLighting lighting;
};
// Sceneごとに保存する実行設定。参照には表示名ではなくEntity IDを使う。
struct ScenePlaySettings {
    EntityId player = 0, cameraTarget = 0;
    float walkSpeed = 0.34f / 0.60f, runSpeed = 0.44f / (0.38f * 0.75f), turnSpeed = 12;
    float forwardYaw = 3.14159265f, groundHeight = 0;
    std::array<float, 2> minimum{-11.5f, -11.5f}, maximum{11.5f, 11.5f};
    std::array<std::string, 3> clips{"Idle", "Walk", "Run"};
    std::array<float, 3> fixedTarget{0, 0, 0};
    float cameraHeight = 0.9f, cameraFollow = 10;
};

// 制作時の状態だけを保持する。再生中の時刻とGPU参照は保存しない。
struct SceneDocument {
    bool legacyBehaviours = true;
    EntityId nextId = 1;
    SceneView view;
    ScenePlaySettings play;
    std::vector<SceneAsset> assets;
    std::vector<SceneEntity> entities;
    struct Material { std::string id; MaterialSurface surface; };
    std::vector<Material> materials;
    const SceneEntity& Entity(EntityId id) const;
    SceneEntity& Entity(EntityId id);
    const SceneAsset& Asset(const std::string& id) const;
    EntityId Add(std::string name, EntityId parent = 0);
    void Reparent(EntityId id, EntityId parent);
    void DeleteSubtree(EntityId id);
};

DirectX::XMFLOAT4X4 LocalMatrix(const Transform& transform);
void ValidateTransform(const Transform& transform);
void ValidateScene(const SceneDocument& document);
void MigrateSceneBehaviours(SceneDocument& document);
std::vector<size_t> SceneOrder(const SceneDocument& document);
std::vector<DirectX::XMFLOAT4X4> EvaluateWorlds(const SceneDocument& document, const std::vector<size_t>& order);
SceneDocument LoadScene(const std::filesystem::path& path);
void SaveScene(const SceneDocument& document, const std::filesystem::path& path);
}
