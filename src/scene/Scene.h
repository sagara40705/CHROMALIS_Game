#pragma once
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace chromalis {
using EntityId = uint64_t;
enum class SceneAssetKind { Static, Skeletal, Effect };

struct Transform {
    std::array<float, 3> position{0, 0, 0};
    std::array<float, 4> rotation{0, 0, 0, 1};
    std::array<float, 3> scale{1, 1, 1};
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
    bool operator==(const AnimationInitialState&) const = default;
};
struct EffectInitialState {
    int count = 1;
    bool autoplay = true;
    EntityId follow = 0;
    std::string joint;
    bool operator==(const EffectInitialState&) const = default;
};
struct SceneEntity {
    EntityId id = 0, parent = 0;
    std::string name = "Object", asset;
    Transform local;
    AnimationInitialState animation;
    EffectInitialState effect;
};
struct SceneView {
    std::array<float, 4> background{0.055f, 0.07f, 0.11f, 1};
    float yaw = 0.35f, pitch = 0.25f, distance = 7;
    bool unlit = false;
};

// 制作時の状態だけを保持する。再生中の時刻とGPU参照は保存しない。
struct SceneDocument {
    EntityId nextId = 1;
    SceneView view;
    std::vector<SceneAsset> assets;
    std::vector<SceneEntity> entities;
    const SceneEntity& Entity(EntityId id) const;
    SceneEntity& Entity(EntityId id);
    const SceneAsset& Asset(const std::string& id) const;
    EntityId Add(std::string name, EntityId parent = 0);
    void Reparent(EntityId id, EntityId parent);
    void DeleteSubtree(EntityId id);
};

DirectX::XMFLOAT4X4 LocalMatrix(const Transform& transform);
void ValidateScene(const SceneDocument& document);
std::vector<size_t> SceneOrder(const SceneDocument& document);
std::vector<DirectX::XMFLOAT4X4> EvaluateWorlds(const SceneDocument& document, const std::vector<size_t>& order);
SceneDocument LoadScene(const std::filesystem::path& path);
void SaveScene(const SceneDocument& document, const std::filesystem::path& path);
}
