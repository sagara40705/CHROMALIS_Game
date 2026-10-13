#include "scene/SceneRuntime.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <DirectXCollision.h>
#include <limits>

namespace chromalis {
namespace {
struct LoadedAsset {
    Renderer* renderer = nullptr;
    Log* log = nullptr;
    SceneAsset source;
    MeshHandle mesh;
    std::shared_ptr<const SkeletalAsset> skeleton;
    std::shared_ptr<const AnimationRig> rig;
    std::unique_ptr<VfxPlayer> effect;
    std::vector<DirectX::XMFLOAT3> pickVertices;
    std::vector<uint32_t> pickIndices;
    uint64_t generation = 0;
    ~LoadedAsset() {
        if (mesh.slot == UINT32_MAX) return;
        try { renderer->ReleaseMesh(mesh); }
        catch (const std::exception& error) { log->Write(std::string("Mesh release failed: ") + error.what()); }
    }
};
struct Instance {
    std::shared_ptr<LoadedAsset> asset;
    std::shared_ptr<LoadedAsset> animationAsset;
    AnimationInitialState animationInitial;
    EffectInitialState effectInitial;
    std::unique_ptr<AnimationPlayer> animation;
    std::unique_ptr<VfxPlayer> effect;
};
size_t FindJoint(const AnimationPlayer& player, const std::string& name) {
    const auto& joints = player.Asset().joints;
    const auto found = std::find_if(joints.begin(), joints.end(), [&](const auto& joint) { return joint.name == name; });
    if (found == joints.end()) throw std::runtime_error("Scene socket joint not found: " + name);
    return static_cast<size_t>(found - joints.begin());
}
void SetInitialState(AnimationPlayer& player, const AnimationInitialState& initial) {
    const auto& clips = player.Clips();
    size_t clip = 0;
    if (!initial.clip.empty()) {
        const auto found = std::find_if(clips.begin(), clips.end(), [&](const auto& value) { return value.name == initial.clip; });
        if (found == clips.end()) throw std::runtime_error("Scene animation clip not found: " + initial.clip);
        clip = static_cast<size_t>(found - clips.begin());
    }
    if (initial.time > clips[clip].duration) throw std::runtime_error("Scene start time exceeds clip duration");
    player.playback = {static_cast<int>(clip), initial.time, initial.speed, initial.autoplay, initial.loop, initial.restPose, initial.bindPose};
    player.Evaluate();
}
}
struct SceneRuntime::State {
    SceneDocument document;
    std::vector<size_t> order;
    std::unordered_map<EntityId, size_t> indices;
    std::unordered_map<std::string, std::shared_ptr<LoadedAsset>> assets;
    std::vector<std::shared_ptr<Instance>> instances;
    std::vector<DirectX::XMFLOAT4X4> worlds;
    std::vector<Transform> locals;
    std::vector<size_t> parents, follows, joints;
    std::vector<DrawItem> draws;
    std::vector<std::vector<MaterialSurface>> materials;
    std::vector<VfxPlayer*> effectGroups;
};

SceneRuntime::SceneRuntime(Renderer& renderer, Log& log) : renderer_(renderer), log_(log), state_(std::make_unique<State>()) {}
SceneRuntime::~SceneRuntime() {
    try { Clear(); }
    catch (const std::exception& error) { log_.Write(std::string("Scene shutdown failed: ") + error.what()); }
}
const SceneDocument& SceneRuntime::Document() const { return state_->document; }
bool SceneRuntime::Replace(SceneDocument document, const std::string& reload) {
    try {
        MigrateSceneBehaviours(document);
        ValidateScene(document);
        renderer_.WaitIdle();
        auto candidate = std::make_unique<State>();
        candidate->document = std::move(document);
        candidate->order = SceneOrder(candidate->document);
        for (size_t i = 0; i < candidate->document.entities.size(); ++i) candidate->indices.emplace(candidate->document.entities[i].id, i);

        // 変更のないリソースだけを共有する。失敗した候補は公開しない。
        for (const auto& source : candidate->document.assets) {
            const auto old = state_->assets.find(source.id);
            if (source.id != reload && reload != "*" && old != state_->assets.end() &&
                old->second->source.path == source.path && old->second->source.kind == source.kind) {
                candidate->assets.emplace(source.id, old->second);
                continue;
            }
            auto asset = std::make_shared<LoadedAsset>();
            asset->renderer = &renderer_; asset->log = &log_; asset->source = source;
            asset->generation = nextGeneration_++;
            if (source.kind == SceneAssetKind::Static) {
                const auto cpu = LoadStaticAsset(source.path);
                if (cpu.id != source.id) throw std::runtime_error("Static asset ID mismatch: " + source.id);
                asset->mesh = renderer_.CreateMesh(cpu);
                for (const auto& vertex : cpu.vertices) asset->pickVertices.emplace_back(vertex.position[0], vertex.position[1], vertex.position[2]);
                asset->pickIndices = cpu.indices;
            } else if (source.kind == SceneAssetKind::Skeletal) {
                asset->skeleton = std::make_shared<const SkeletalAsset>(LoadSkeletalAsset(source.path));
                if (asset->skeleton->mesh.id != source.id) throw std::runtime_error("Skeletal asset ID mismatch: " + source.id);
                asset->mesh = renderer_.CreateMesh(asset->skeleton->mesh, &asset->skeleton->skin);
            } else if (source.kind == SceneAssetKind::Animation) {
                auto library = LoadAnimationLibrary(source.path);
                if (library.id != source.id) throw std::runtime_error("Animation library ID mismatch: " + source.id);
                asset->rig = std::make_shared<const AnimationRig>(std::move(library));
            } else {
                // 未配置アセットも制作物として読めることを確認する。
                asset->effect = std::make_unique<VfxPlayer>(renderer_.Device(), renderer_.Queue(), Renderer::SceneFormat);
                if (!asset->effect->Reload(source.path)) throw std::runtime_error(asset->effect->Status());
            }
            candidate->assets.emplace(source.id, std::move(asset));
        }
        for (const auto& entity : candidate->document.entities) {
            const auto previousIndex = state_->indices.find(entity.id);
            const bool preserveLocal = reload != "*" && previousIndex != state_->indices.end() &&
                state_->document.entities[previousIndex->second].local == entity.local;
            candidate->locals.push_back(preserveLocal ? state_->locals[previousIndex->second] : entity.local);
            // 材質は候補Sceneの値として解決し、共有中のPlayerや旧Sceneへ書き込まない。
            std::vector<MaterialSurface> surfaces;
            if (!entity.materials.empty()) {
                const auto defaults = renderer_.MeshMaterials(candidate->assets.at(entity.asset)->mesh);
                for (const auto& material : defaults) surfaces.push_back(ImportedSurface(material));
                for (const auto& binding : entity.materials) {
                    if (binding.slot >= surfaces.size()) throw std::runtime_error("Material slot out of range on entity: " + entity.name);
                    const auto found = std::find_if(candidate->document.materials.begin(), candidate->document.materials.end(),
                        [&](const auto& material) { return material.id == binding.material; });
                    surfaces[binding.slot] = found->surface;
                }
            }
            candidate->materials.push_back(std::move(surfaces));
            auto instance = std::make_shared<Instance>();
            if (!entity.asset.empty()) {
                instance->asset = candidate->assets.at(entity.asset);
                if (!entity.animation.library.empty()) instance->animationAsset = candidate->assets.at(entity.animation.library);
                const auto old = state_->indices.find(entity.id);
                if (reload != "*" && old != state_->indices.end()) {
                    const auto& previous = state_->instances[old->second];
                    if (previous->asset == instance->asset && previous->animationAsset == instance->animationAsset && previous->animationInitial == entity.animation && previous->effectInitial == entity.effect) {
                        // 再生状態は共有し、参照先Indexだけは候補Scene専用に保持する。
                        candidate->instances.push_back(previous);
                        continue;
                    }
                }
                instance->animationInitial = entity.animation;
                instance->effectInitial = entity.effect;
                if (instance->asset->skeleton) {
                    if (!instance->animationAsset && !instance->asset->rig)
                        instance->asset->rig = std::make_shared<const AnimationRig>(*instance->asset->skeleton);
                    instance->animation = std::make_unique<AnimationPlayer>(instance->asset->skeleton,
                        instance->animationAsset ? instance->animationAsset->rig : instance->asset->rig);
                    SetInitialState(*instance->animation, entity.animation);
                } else if (instance->asset->source.kind == SceneAssetKind::Effect) {
                    instance->effect = std::make_unique<VfxPlayer>(*instance->asset->effect);
                    if (entity.effect.autoplay) {
                        instance->effect->Play(entity.effect.count);
                        if (instance->effect->Instances() != entity.effect.count) throw std::runtime_error(instance->effect->Status());
                    }
                }
            }
            candidate->instances.push_back(std::move(instance));
        }
        // 参照の妥当性をすべて確認してから状態を入れ替える。
        for (const auto& entity : candidate->document.entities) {
            candidate->parents.push_back(entity.parent ? candidate->indices.at(entity.parent) : SIZE_MAX);
            const auto target = entity.effect.follow ? candidate->indices.at(entity.effect.follow) : SIZE_MAX;
            candidate->follows.push_back(target);
            candidate->joints.push_back(target == SIZE_MAX ? 0 : FindJoint(*candidate->instances[target]->animation, entity.effect.joint));
        }
        candidate->worlds = EvaluateWorlds(candidate->document, candidate->order);
        // アセットの定義順で一度だけ描画する。配置数分のManager更新とフレーム領域を作らない。
        for (const auto& asset : candidate->document.assets)
            if (auto* effect = candidate->assets.at(asset.id)->effect.get()) candidate->effectGroups.push_back(effect);
        candidate->draws.reserve(candidate->instances.size());
        UpdateState(*candidate, 0, false);
        renderer_.WaitIdle();
        state_ = std::move(candidate);
        ++revision_;
        status_ = "Scene applied";
        return true;
    } catch (const GpuFailure&) {
        throw;
    } catch (const std::exception& error) {
        status_ = std::string("Scene update failed; previous scene retained: ") + error.what();
        log_.Write(status_);
        if (FAILED(renderer_.Device()->GetDeviceRemovedReason())) throw;
        return false;
    }
}
bool SceneRuntime::Apply(SceneDocument document) {
    if (!Replace(std::move(document), {})) return false;
    dirty_ = true;
    return true;
}
bool SceneRuntime::Open(const std::filesystem::path& path) {
    try {
        if (!Replace(LoadScene(path), "*")) return false;
        path_ = std::filesystem::absolute(path);
        dirty_ = false;
        status_ = "Scene loaded";
        return true;
    } catch (const GpuFailure&) {
        throw;
    } catch (const std::exception& error) {
        status_ = std::string("Scene open failed; retained: ") + error.what();
        log_.Write(status_);
        if (FAILED(renderer_.Device()->GetDeviceRemovedReason())) throw;
        return false;
    }
}
bool SceneRuntime::Save(const std::filesystem::path& path) {
    try {
        SaveScene(Document(), path);
        path_ = std::filesystem::absolute(path);
        dirty_ = false;
        status_ = "Scene saved";
        return true;
    } catch (const std::exception& error) {
        status_ = std::string("Scene save failed: ") + error.what();
        log_.Write(status_);
        return false;
    }
}
bool SceneRuntime::RefreshAssets() { return Replace(Document(), "*"); }
bool SceneRuntime::New() {
    if (!Apply(SceneDocument{})) return false;
    path_.clear(); dirty_ = false;
    return true;
}
bool SceneRuntime::ReloadAsset(const std::string& id) {
    if (!state_->assets.contains(id)) { status_ = "Cannot reload unknown asset: " + id; return false; }
    return Replace(Document(), id);
}
bool SceneRuntime::AssignAsset(EntityId entity, const std::filesystem::path& path, SceneAssetKind kind) {
    try {
        auto id = std::string("effect/") + std::to_string(entity);
        if (kind == SceneAssetKind::Static) id = LoadStaticAsset(path).id;
        if (kind == SceneAssetKind::Skeletal) id = LoadSkeletalAsset(path).mesh.id;
        if (kind == SceneAssetKind::Animation) id = LoadAnimationLibrary(path).id;
        return AssignAsset(entity, {id, kind, std::filesystem::absolute(path).lexically_normal()});
    } catch (const GpuFailure&) {
        throw;
    } catch (const std::exception& error) {
        status_ = std::string("Asset assignment failed; retained: ") + error.what();
        log_.Write(status_);
        if (FAILED(renderer_.Device()->GetDeviceRemovedReason())) throw;
        return false;
    }
}
bool SceneRuntime::ReloadAsset(const std::string& id, const std::filesystem::path& path) {
    auto document = Document();
    const auto found = std::find_if(document.assets.begin(), document.assets.end(), [&](const auto& asset) { return asset.id == id; });
    if (found == document.assets.end()) { status_ = "Asset is not used by this Scene: " + id; return false; }
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    const bool moved = found->path != absolute;
    found->path = absolute;
    if (!Replace(std::move(document), id)) return false;
    dirty_ |= moved;
    return true;
}
bool SceneRuntime::AssignAsset(EntityId entity, const SceneAsset& asset) {
    auto document = Document();
    auto& target = document.Entity(entity);
    if (asset.kind == SceneAssetKind::Animation) {
        if (target.asset.empty() || document.Asset(target.asset).kind != SceneAssetKind::Skeletal) { status_ = "Select a skeletal model before assigning shared animations"; return false; }
        target.animation = {}; target.animation.library = asset.id;
    } else {
        if (target.asset != asset.id) {
            target.materials.clear();
            if (asset.kind != SceneAssetKind::Skeletal || target.animation.library.empty()) target.animation = {};
            target.effect = {};
        }
        target.asset = asset.id;
    }
    auto found = std::find_if(document.assets.begin(), document.assets.end(), [&](const auto& value) { return value.id == asset.id; });
    if (found == document.assets.end()) document.assets.push_back(asset);
    else {
        if (found->kind != asset.kind) { status_ = "Asset ID belongs to another kind"; return false; }
        *found = asset;
    }
    std::erase_if(document.assets, [&](const auto& value) {
        return std::none_of(document.entities.begin(), document.entities.end(), [&](const auto& object) { return object.asset == value.id || object.animation.library == value.id; });
    });
    if (!Replace(std::move(document), asset.id)) return false;
    dirty_ = true;
    return true;
}
bool SceneRuntime::Delete(EntityId entity) {
    auto document = Document();
    document.DeleteSubtree(entity);
    for (auto& remaining : document.entities) {
        if (Document().Entity(remaining.id).effect.follow && !remaining.effect.follow) {
            const auto& world = World(remaining.id);
            if (Document().Entity(remaining.id).effect.followTransform) {
                using namespace DirectX;
                XMVECTOR scale, rotation, translation;
                if (!XMMatrixDecompose(&scale, &rotation, &translation, XMLoadFloat4x4(&world))) {
                    status_ = "Cannot detach a sheared socket transform; remove socket following first";
                    return false;
                }
                XMFLOAT3 s; XMFLOAT4 r;
                XMStoreFloat3(&s, scale); XMStoreFloat4(&r, rotation);
                remaining.local.scale = {s.x, s.y, s.z};
                remaining.local.rotation = {r.x, r.y, r.z, r.w};
                remaining.local.position = {world._41, world._42, world._43};
                const auto reconstructed = LocalMatrix(remaining.local);
                for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
                    if (std::abs(reconstructed.m[row][column] - world.m[row][column]) > 0.0001f * std::max(1.0f, std::abs(world.m[row][column]))) {
                        status_ = "Cannot detach a sheared socket transform; remove socket following first";
                        return false;
                    }
            }
            remaining.local.position = {world._41, world._42, world._43};
        }
    }
    // 最後の参照を削除したアセットは、このSceneからも解放する。
    std::erase_if(document.assets, [&](const auto& asset) {
        return std::none_of(document.entities.begin(), document.entities.end(), [&](const auto& value) { return value.asset == asset.id || value.animation.library == asset.id; });
    });
    return Apply(std::move(document));
}
void SceneRuntime::Clear() {
    renderer_.WaitIdle();
    state_ = std::make_unique<State>();
    renderer_.DrainDiagnostics();
}
void SceneRuntime::Update(float seconds) {
    UpdateState(*state_, seconds);
}
void SceneRuntime::SetRuntimeTransform(EntityId entity, const Transform& transform) {
    ValidateTransform(transform);
    state_->locals.at(state_->indices.at(entity)) = transform;
}
const Transform& SceneRuntime::RuntimeTransform(EntityId entity) const {
    return state_->locals.at(state_->indices.at(entity));
}
void SceneRuntime::UpdateState(State& state, float seconds, bool advance) {
    using namespace DirectX;
    if (advance) for (const auto& instance : state.instances) if (instance->animation) instance->animation->Update(seconds);
    state.draws.clear();
    for (size_t i : state.order) {
        auto& instance = *state.instances[i];
        const auto& entity = state.document.entities[i];
        const auto local = LocalMatrix(state.locals[i]);
        auto world = XMLoadFloat4x4(&local);
        if (state.parents[i] != SIZE_MAX) world *= XMLoadFloat4x4(&state.worlds[state.parents[i]]);
        XMStoreFloat4x4(&state.worlds[i], world);
        if (!instance.asset) continue;
        if (instance.effect) {
            if (entity.effect.follow) {
                const auto index = state.follows[i];
                const auto& target = *state.instances[index]->animation;
                const auto socket = target.SocketTransform(state.joints[i], state.worlds[index]);
                if (entity.effect.followTransform) {
                    // ローカルオフセット→Joint姿勢→対象Worldの順。行列のまま渡し非一様スケールも保持する。
                    XMStoreFloat4x4(&state.worlds[i], world * XMLoadFloat4x4(&socket));
                } else {
                    state.worlds[i]._41 += socket._41;
                    state.worlds[i]._42 += socket._42;
                    state.worlds[i]._43 += socket._43;
                }
            }
            if (advance) instance.effect->SetWorld(state.worlds[i]);
        } else {
            DrawItem item{instance.asset->mesh, state.worlds[i], {}, {}, state.materials[i]};
            if (instance.animation) {
                item.bones = instance.animation->Palette();
                item.normals = instance.animation->Normals();
            }
            state.draws.push_back(item);
        }
    }
    if (advance) for (auto* effect : state.effectGroups) effect->Update(seconds);
}
std::span<const DrawItem> SceneRuntime::DrawItems() const { return state_->draws; }
std::span<const MeshMaterial> SceneRuntime::ImportedMaterials(EntityId entity) const {
    const auto found = state_->indices.find(entity);
    if (found == state_->indices.end()) return {};
    const auto& asset = state_->instances[found->second]->asset;
    if (!asset || asset->mesh.slot == UINT32_MAX) return {};
    return renderer_.MeshMaterials(asset->mesh);
}
size_t SceneRuntime::MaterialUseCount(EntityId entity, uint32_t slot) const {
    const auto found = state_->indices.find(entity);
    if (found == state_->indices.end()) return 0;
    const auto& asset = state_->instances[found->second]->asset;
    return !asset || asset->mesh.slot == UINT32_MAX ? 0 : renderer_.MaterialUseCount(asset->mesh, slot);
}
void SceneRuntime::DrawEffects(ID3D12GraphicsCommandList* commands) {
    for (auto* effect : state_->effectGroups)
        effect->Draw(renderer_.CurrentFrame(), commands, renderer_.View(), renderer_.Projection());
}
AnimationPlayer* SceneRuntime::Animation(EntityId entity) {
    const auto found = state_->indices.find(entity);
    return found == state_->indices.end() ? nullptr : state_->instances[found->second]->animation.get();
}
VfxPlayer* SceneRuntime::Effect(EntityId entity) {
    const auto found = state_->indices.find(entity);
    return found == state_->indices.end() ? nullptr : state_->instances[found->second]->effect.get();
}
const DirectX::XMFLOAT4X4& SceneRuntime::World(EntityId entity) const { return state_->worlds.at(state_->indices.at(entity)); }
uint64_t SceneRuntime::Generation(EntityId entity) const {
    const auto found = state_->indices.find(entity);
    if (found == state_->indices.end()) return 0;
    const auto& instance = state_->instances[found->second];
    return instance->asset ? instance->asset->generation : 0;
}
EntityId SceneRuntime::Pick(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction) const {
    using namespace DirectX;
    float nearest = std::numeric_limits<float>::max();
    EntityId selected = 0;
    const auto rayOrigin = XMLoadFloat3(&origin), rayDirection = XMVector3Normalize(XMLoadFloat3(&direction));
    for (size_t i = 0; i < state_->instances.size(); ++i) {
        const auto& instance = *state_->instances[i];
        if (!instance.asset || instance.effect) continue;
        const auto inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&state_->worlds[i]));
        const auto localOrigin = XMVector3TransformCoord(rayOrigin, inverse);
        const auto localVector = XMVector3TransformNormal(rayDirection, inverse);
        const auto length = XMVectorGetX(XMVector3Length(localVector));
        const auto localDirection = XMVectorScale(localVector, 1 / length);
        const auto& asset = *instance.asset;
        std::span<const XMFLOAT3> vertices = asset.pickVertices;
        std::vector<XMFLOAT3> posedVertices;
        const auto* indices = &asset.pickIndices;
        if (instance.animation) {
            // クリック時だけ現在姿勢をCPUで計算する。GPU readbackは行わない。
            const auto& skeleton = *asset.skeleton;
            const auto palette = instance.animation->Palette();
            posedVertices.resize(skeleton.mesh.vertices.size());
            indices = &skeleton.mesh.indices;
            for (size_t v = 0; v < posedVertices.size(); ++v) {
                const auto& position = skeleton.mesh.vertices[v].position;
                const auto p = XMVectorSet(position[0], position[1], position[2], 1);
                auto result = XMVectorZero();
                for (size_t j = 0; j < 4; ++j) result += XMVectorScale(XMVector3TransformCoord(p, XMLoadFloat4x4(&palette[skeleton.skin[v].joints[j]])), skeleton.skin[v].weights[j]);
                XMStoreFloat3(&posedVertices[v], result);
            }
            vertices = posedVertices;
        }
        for (size_t triangle = 0; triangle < indices->size(); triangle += 3) {
            float distance = 0;
            if (TriangleTests::Intersects(localOrigin, localDirection, XMLoadFloat3(&vertices[(*indices)[triangle]]),
                XMLoadFloat3(&vertices[(*indices)[triangle + 1]]), XMLoadFloat3(&vertices[(*indices)[triangle + 2]]), distance) && distance / length < nearest) {
                nearest = distance / length; selected = state_->document.entities[i].id;
            }
        }
    }
    return selected;
}
}
