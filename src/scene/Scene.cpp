#include "scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace chromalis {
using namespace DirectX;
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<size_t N> void Finite(const std::array<float, N>& values) {
    for (float value : values) Require(std::isfinite(value), "Scene contains a nonfinite number");
}
}

const SceneEntity& SceneDocument::Entity(EntityId id) const {
    const auto found = std::find_if(entities.begin(), entities.end(), [id](const auto& entity) { return entity.id == id; });
    if (found == entities.end()) throw std::runtime_error("Scene entity does not exist: " + std::to_string(id));
    return *found;
}
SceneEntity& SceneDocument::Entity(EntityId id) {
    return const_cast<SceneEntity&>(std::as_const(*this).Entity(id));
}
const SceneAsset& SceneDocument::Asset(const std::string& id) const {
    const auto found = std::find_if(assets.begin(), assets.end(), [&](const auto& asset) { return asset.id == id; });
    if (found == assets.end()) throw std::runtime_error("Scene asset does not exist: " + id);
    return *found;
}
EntityId SceneDocument::Add(std::string name, EntityId parent) {
    Require(nextId && nextId < std::numeric_limits<EntityId>::max(), "Scene entity ID exhausted");
    if (parent) (void)Entity(parent);
    SceneEntity entity;
    entity.id = nextId;
    entity.name = std::move(name);
    entity.parent = parent;
    entities.push_back(std::move(entity));
    return nextId++;
}
void SceneDocument::Reparent(EntityId id, EntityId parent) {
    auto candidate = *this;
    candidate.Entity(id).parent = parent;
    ValidateScene(candidate);
    *this = std::move(candidate);
}
void SceneDocument::DeleteSubtree(EntityId id) {
    (void)Entity(id);
    const auto order = SceneOrder(*this);
    std::unordered_set<EntityId> removed{id};
    for (size_t index : order) {
        const auto& entity = entities[index];
        if (removed.contains(entity.parent)) removed.insert(entity.id);
    }
    std::erase_if(entities, [&](const auto& entity) { return removed.contains(entity.id); });
    for (auto& entity : entities) {
        if (removed.contains(entity.effect.follow)) {
            entity.effect.follow = 0;
            entity.effect.joint.clear();
        }
    }
}
XMFLOAT4X4 LocalMatrix(const Transform& transform) {
    XMFLOAT4X4 matrix;
    const auto& p = transform.position;
    const auto& q = transform.rotation;
    const auto& s = transform.scale;
    XMStoreFloat4x4(&matrix, XMMatrixScaling(s[0], s[1], s[2]) *
        XMMatrixRotationQuaternion(XMVectorSet(q[0], q[1], q[2], q[3])) * XMMatrixTranslation(p[0], p[1], p[2]));
    return matrix;
}
std::vector<size_t> SceneOrder(const SceneDocument& document) {
    std::unordered_map<EntityId, size_t> indices;
    for (size_t i = 0; i < document.entities.size(); ++i)
        Require(document.entities[i].id && indices.emplace(document.entities[i].id, i).second, "Duplicate/zero entity ID");
    std::vector<uint8_t> state(document.entities.size());
    std::vector<size_t> order;
    std::function<void(size_t, size_t)> visit = [&](size_t index, size_t depth) {
        Require(depth <= 128, "Scene hierarchy exceeds 128 levels");
        Require(state[index] != 1, "Scene hierarchy contains a cycle");
        if (state[index] == 2) return;
        state[index] = 1;
        if (const auto parent = document.entities[index].parent) {
            const auto found = indices.find(parent);
            Require(found != indices.end(), "Scene parent does not exist");
            visit(found->second, depth + 1);
        }
        if (const auto target = document.entities[index].effect.follow) {
            const auto found = indices.find(target);
            Require(found != indices.end(), "Socket target does not exist");
            visit(found->second, depth + 1);
        }
        state[index] = 2;
        order.push_back(index);
    };
    for (size_t i = 0; i < state.size(); ++i) visit(i, 0);
    std::vector<size_t> depths(state.size());
    for (size_t index : order) {
        const auto parent = document.entities[index].parent;
        depths[index] = parent ? depths[indices.at(parent)] + 1 : 0;
        Require(depths[index] <= 128, "Scene hierarchy exceeds 128 levels");
    }
    return order;
}
std::vector<XMFLOAT4X4> EvaluateWorlds(const SceneDocument& document, const std::vector<size_t>& order) {
    std::unordered_map<EntityId, size_t> indices;
    std::vector<XMFLOAT4X4> result(document.entities.size());
    for (size_t index : order) {
        const auto& entity = document.entities[index];
        const auto local = LocalMatrix(entity.local);
        auto world = XMLoadFloat4x4(&local);
        if (entity.parent) world *= XMLoadFloat4x4(&result[indices.at(entity.parent)]);
        XMStoreFloat4x4(&result[index], world);
        indices.emplace(entity.id, index);
    }
    return result;
}
void ValidateScene(const SceneDocument& document) {
    Require(document.entities.size() <= 10000 && document.assets.size() <= 10000, "Scene exceeds 10000 objects/assets");
    Require(document.nextId != 0, "Invalid next entity ID");
    Finite(document.view.background);
    for (float value : document.view.background) Require(value >= 0 && value <= 1, "Invalid background color");
    Require(std::isfinite(document.view.yaw) && std::isfinite(document.view.pitch) && std::abs(document.view.pitch) < 1.55f &&
        std::isfinite(document.view.distance) && document.view.distance > 0.05f, "Invalid scene camera");
    std::unordered_set<std::string> assets;
    for (const auto& asset : document.assets) {
        Require(!asset.id.empty() && asset.id.size() <= 256 && assets.insert(asset.id).second, "Invalid/duplicate scene asset ID");
        Require(!asset.path.empty(), "Empty scene asset path");
        Require(asset.kind == SceneAssetKind::Static || asset.kind == SceneAssetKind::Skeletal || asset.kind == SceneAssetKind::Effect || asset.kind == SceneAssetKind::Animation, "Invalid asset kind");
    }
    const auto order = SceneOrder(document);
    for (const auto& entity : document.entities) {
        Require(entity.id < document.nextId, "nextId must exceed every entity ID");
        Require(!entity.name.empty() && entity.name.size() <= 256, "Invalid entity name");
        Finite(entity.local.position); Finite(entity.local.rotation); Finite(entity.local.scale);
        float norm = 0;
        for (float value : entity.local.rotation) norm += value * value;
        Require(std::abs(norm - 1) < 0.001f, "Scene rotation must be a unit quaternion");
        for (float value : entity.local.scale) Require(value > 0, "Scene scale must be positive");
        if (entity.asset.empty()) {
            Require(entity.animation.library.empty(), "Empty object cannot reference animations");
            Require(!entity.effect.follow, "Empty object cannot follow a socket");
            continue;
        }
        const auto kind = document.Asset(entity.asset).kind;
        const auto& animation = entity.animation;
        Require(kind != SceneAssetKind::Animation, "Animation library cannot be used as a model");
        if (!animation.library.empty()) Require(kind == SceneAssetKind::Skeletal && document.Asset(animation.library).kind == SceneAssetKind::Animation,
            "Shared animations require a skeletal model and animation library");
        Require(std::isfinite(animation.time) && animation.time >= 0 && std::isfinite(animation.speed), "Invalid animation initial state");
        Require(!(animation.restPose && animation.bindPose), "Select either Rest or Bind pose");
        Require(entity.effect.count >= 1 && entity.effect.count <= 64, "Effect count must be 1..64");
        if (entity.effect.follow) {
            Require(kind == SceneAssetKind::Effect && entity.parent == 0, "Socket following requires a root VFX object");
            const auto& target = document.Entity(entity.effect.follow);
            Require(!target.asset.empty() && document.Asset(target.asset).kind == SceneAssetKind::Skeletal && !entity.effect.joint.empty(), "Invalid socket target");
            // 追従先が自分の子孫だと、確定前のWorldを参照してしまう。
            for (auto parent = target.parent; parent; parent = document.Entity(parent).parent)
                Require(parent != entity.id, "Socket target depends on its follower");
        }
    }
    // 外部データ境界で合成結果も検査し、描画中の重複検査を避ける。
    for (const auto& world : EvaluateWorlds(document, order)) {
        XMFLOAT4X4 inverse;
        XMStoreFloat4x4(&inverse, XMMatrixInverse(nullptr, XMLoadFloat4x4(&world)));
        for (const auto& row : world.m) for (float value : row) Require(std::isfinite(value), "Scene world matrix overflow");
        for (const auto& row : inverse.m) for (float value : row) Require(std::isfinite(value), "Scene world matrix cannot be inverted");
    }
}
}
