#pragma once
#include "scene/SceneRuntime.h"
#include "runtime/Input.h"
#include <nlohmann/json.hpp>
#include <functional>

namespace chromalis {
using BehaviourSettings = nlohmann::json;

// 実行ロジックにSceneの差し替え・保存・GPU所有権を渡さない。
class BehaviourScene {
public:
    BehaviourScene(SceneRuntime& scene) : scene_(scene) {}
    const SceneDocument& Document() const { return scene_.Document(); }
    const Transform& RuntimeTransform(EntityId entity) const { return scene_.RuntimeTransform(entity); }
    void SetRuntimeTransform(EntityId entity, const Transform& transform) { scene_.SetRuntimeTransform(entity, transform); }
    const DirectX::XMFLOAT4X4& World(EntityId entity) const { return scene_.World(entity); }
    AnimationPlayer* Animation(EntityId entity) { return scene_.Animation(entity); }
    VfxPlayer* Effect(EntityId entity) { return scene_.Effect(entity); }
private:
    SceneRuntime& scene_;
};
// コンテキストは呼び出し中だけ借用する。Sceneや入力のポインタを永続保存しない。
struct BehaviourContext {
    BehaviourScene scene;
    EntityId entity;
    const InputState& input;
};
class Behaviour {
public:
    virtual ~Behaviour() = default;
    virtual void Start(BehaviourContext&) {}
    virtual void Update(BehaviourContext&, float) {}
    virtual void AfterPose(BehaviourContext&, float) {}
    virtual void ConfigureView(const SceneRuntime&, RenderSettings&) const {}
    virtual void Stop(BehaviourContext&) noexcept {}
};
enum class BehaviourFieldKind { Value, Entity, Asset };
struct BehaviourField {
    std::string name;
    BehaviourSettings initial;
    double minimum = -1000000, maximum = 1000000;
    BehaviourFieldKind kind = BehaviourFieldKind::Value;
};
struct BehaviourType {
    std::string name;
    std::vector<BehaviourField> fields;
    std::function<std::unique_ptr<Behaviour>(const BehaviourSettings&)> create;
    bool writesTransform = false, ownsCamera = false;
    uint32_t version = 1;
    std::string id;
    // 登録一覧のファイル名。保存用IDやDescribeの名称とは分離する。
    std::string sourceFilename;
};
class BehaviourRegistry {
public:
    void Add(BehaviourType type, std::string id, std::string sourceFilename = {});
    const BehaviourType* Find(const std::string& id) const;
    const std::vector<BehaviourType>& Types() const { return types_; }
private:
    std::vector<BehaviourType> types_;
};
const BehaviourRegistry& GameBehaviours();
BehaviourSettings ResolveBehaviourSettings(const BehaviourType& type, const BehaviourDefinition& definition, const SceneDocument& scene);
BehaviourDefinition MakeBehaviour(const BehaviourType& type, const SceneEntity& entity);
}
