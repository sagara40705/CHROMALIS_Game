#pragma once
#include "scene/Scene.h"
#include "animation/AnimationPlayer.h"
#include "runtime/Renderer.h"
#include "runtime/VfxPlayer.h"
#include <memory>

namespace chromalis {
// Rendererより後に生成し、GPUを待機してから先に破棄する。
class SceneRuntime final {
public:
    SceneRuntime(Renderer& renderer, Log& log);
    ~SceneRuntime();
    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;

    const SceneDocument& Document() const;
    const std::filesystem::path& Path() const { return path_; }
    const std::string& Status() const { return status_; }
    bool Dirty() const { return dirty_; }
    void MarkClean() { dirty_ = false; }
    uint64_t Revision() const { return revision_; }

    bool Apply(SceneDocument document);
    bool Open(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path);
    bool New();
    bool RefreshAssets();
    bool ReloadAsset(const std::string& id);
    bool ReloadAsset(const std::string& id, const std::filesystem::path& path);
    bool AssignAsset(EntityId entity, const std::filesystem::path& path, SceneAssetKind kind);
    bool AssignAsset(EntityId entity, const SceneAsset& asset);
    bool Delete(EntityId entity);
    void Clear();

    void Update(float seconds);
    // 保存用Documentを変更せず、次のUpdateで使う実行中のLocalを設定する。
    void SetRuntimeTransform(EntityId entity, const Transform& transform);
    const Transform& RuntimeTransform(EntityId entity) const;
    std::span<const DrawItem> DrawItems() const;
    void DrawEffects(ID3D12GraphicsCommandList* commands);
    AnimationPlayer* Animation(EntityId entity);
    VfxPlayer* Effect(EntityId entity);
    std::span<const MeshMaterial> ImportedMaterials(EntityId entity) const;
    size_t MaterialUseCount(EntityId entity, uint32_t slot) const;
    const DirectX::XMFLOAT4X4& World(EntityId entity) const;
    uint64_t Generation(EntityId entity) const;
    EntityId Pick(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction) const;

private:
    struct State;
    bool Replace(SceneDocument document, const std::string& reload);
    void UpdateState(State& state, float seconds, bool advance = true);
    Renderer& renderer_;
    Log& log_;
    std::unique_ptr<State> state_;
    std::filesystem::path path_;
    std::string status_ = "Empty scene";
    bool dirty_ = false;
    uint64_t revision_ = 0, nextGeneration_ = 1;
};
}
