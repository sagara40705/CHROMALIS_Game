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
    bool ReloadAsset(const std::string& id);
    bool AssignAsset(EntityId entity, const std::filesystem::path& path, SceneAssetKind kind);
    bool Delete(EntityId entity);
    void Clear();

    void Update(float seconds);
    std::span<const DrawItem> DrawItems() const;
    void DrawEffects(ID3D12GraphicsCommandList* commands);
    AnimationPlayer* Animation(EntityId entity);
    VfxPlayer* Effect(EntityId entity);
    const DirectX::XMFLOAT4X4& World(EntityId entity) const;
    uint64_t Generation(EntityId entity) const;

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
