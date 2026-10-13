#include "scene/SceneRuntime.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace chromalis {
namespace {
struct LoadedAsset {
    Renderer* renderer = nullptr;
    Log* log = nullptr;
    SceneAsset source;
    MeshHandle mesh;
    std::unique_ptr<SkeletalAsset> skeleton;
    uint64_t generation = 0;
    ~LoadedAsset() {
        if (mesh.slot == UINT32_MAX) return;
        try { renderer->ReleaseMesh(mesh); }
        catch (const std::exception& error) { log->Write(std::string("Mesh release failed: ") + error.what()); }
    }
};
struct Instance {
    std::shared_ptr<LoadedAsset> asset;
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
    const auto& clips = player.Asset().clips;
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
    std::vector<size_t> parents, follows, joints;
    std::vector<DrawItem> draws;
};

SceneRuntime::SceneRuntime(Renderer& renderer, Log& log) : renderer_(renderer), log_(log), state_(std::make_unique<State>()) {}
SceneRuntime::~SceneRuntime() {
    try { Clear(); }
    catch (const std::exception& error) { log_.Write(std::string("Scene shutdown failed: ") + error.what()); }
}
const SceneDocument& SceneRuntime::Document() const { return state_->document; }
bool SceneRuntime::Replace(SceneDocument document, const std::string& reload) {
    try {
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
            } else if (source.kind == SceneAssetKind::Skeletal) {
                asset->skeleton = std::make_unique<SkeletalAsset>(LoadSkeletalAsset(source.path));
                if (asset->skeleton->mesh.id != source.id) throw std::runtime_error("Skeletal asset ID mismatch: " + source.id);
                asset->mesh = renderer_.CreateMesh(asset->skeleton->mesh, &asset->skeleton->skin);
            } else {
                // 未配置アセットも制作物として読めることを確認する。
                VfxPlayer validation(renderer_.Device(), renderer_.Queue(), Renderer::SceneFormat);
                if (!validation.Reload(source.path)) throw std::runtime_error(validation.Status());
            }
            candidate->assets.emplace(source.id, std::move(asset));
        }
        for (const auto& entity : candidate->document.entities) {
            auto instance = std::make_shared<Instance>();
            if (!entity.asset.empty()) {
                instance->asset = candidate->assets.at(entity.asset);
                const auto old = state_->indices.find(entity.id);
                if (reload != "*" && old != state_->indices.end()) {
                    const auto& previous = state_->instances[old->second];
                    if (previous->asset == instance->asset && previous->animationInitial == entity.animation && previous->effectInitial == entity.effect) {
                        // 再生状態は共有し、参照先Indexだけは候補Scene専用に保持する。
                        candidate->instances.push_back(previous);
                        continue;
                    }
                }
                instance->animationInitial = entity.animation;
                instance->effectInitial = entity.effect;
                if (instance->asset->skeleton) {
                    instance->animation = std::make_unique<AnimationPlayer>(*instance->asset->skeleton);
                    SetInitialState(*instance->animation, entity.animation);
                } else if (instance->asset->source.kind == SceneAssetKind::Effect) {
                    instance->effect = std::make_unique<VfxPlayer>(renderer_.Device(), renderer_.Queue(), Renderer::SceneFormat);
                    if (!instance->effect->Reload(instance->asset->source.path)) throw std::runtime_error(instance->effect->Status());
                    if (entity.effect.autoplay) instance->effect->Play(entity.effect.count);
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
    if (target.asset != asset.id) { target.animation = {}; target.effect = {}; }
    target.asset = asset.id;
    auto found = std::find_if(document.assets.begin(), document.assets.end(), [&](const auto& value) { return value.id == asset.id; });
    if (found == document.assets.end()) document.assets.push_back(asset);
    else {
        if (found->kind != asset.kind) { status_ = "Asset ID belongs to another kind"; return false; }
        *found = asset;
    }
    std::erase_if(document.assets, [&](const auto& value) {
        return std::none_of(document.entities.begin(), document.entities.end(), [&](const auto& object) { return object.asset == value.id; });
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
            remaining.local.position = {world._41, world._42, world._43};
        }
    }
    // 最後の参照を削除したアセットは、このSceneからも解放する。
    std::erase_if(document.assets, [&](const auto& asset) {
        return std::none_of(document.entities.begin(), document.entities.end(), [&](const auto& value) { return value.asset == asset.id; });
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
void SceneRuntime::UpdateState(State& state, float seconds, bool advance) {
    using namespace DirectX;
    if (advance) for (const auto& instance : state.instances) if (instance->animation) instance->animation->Update(seconds);
    state.draws.clear();
    for (size_t i : state.order) {
        auto& instance = *state.instances[i];
        const auto& entity = state.document.entities[i];
        const auto local = LocalMatrix(entity.local);
        auto world = XMLoadFloat4x4(&local);
        if (state.parents[i] != SIZE_MAX) world *= XMLoadFloat4x4(&state.worlds[state.parents[i]]);
        XMStoreFloat4x4(&state.worlds[i], world);
        if (!instance.asset) continue;
        if (instance.effect) {
            if (entity.effect.follow) {
                const auto index = state.follows[i];
                const auto& target = *state.instances[index]->animation;
                const auto socket = target.Socket(state.joints[i], state.worlds[index]);
                state.worlds[i]._41 += socket.x;
                state.worlds[i]._42 += socket.y;
                state.worlds[i]._43 += socket.z;
            }
            if (advance) instance.effect->UpdateMatrix(seconds, state.worlds[i]);
        } else {
            DrawItem item{instance.asset->mesh, state.worlds[i], {}, {}};
            if (instance.animation) {
                item.bones = instance.animation->Palette();
                item.normals = instance.animation->Normals();
            }
            state.draws.push_back(item);
        }
    }
}
std::span<const DrawItem> SceneRuntime::DrawItems() const { return state_->draws; }
void SceneRuntime::DrawEffects(ID3D12GraphicsCommandList* commands) {
    for (const auto& instance : state_->instances)
        if (instance->effect) instance->effect->Draw(renderer_.CurrentFrame(), commands, renderer_.View(), renderer_.Projection());
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
}
