#include "gameplay/Behaviour.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chromalis {
// CMakeが一覧から生成する。共通実行部は個別Behaviourのヘッダーを参照しない。
void RegisterGameBehaviours(BehaviourRegistry& registry);
void BehaviourRegistry::Add(BehaviourType type, std::string id, std::string sourceFilename) {
    if (id.empty() || Find(id) || !type.create || type.name.empty()) throw std::runtime_error("Invalid/duplicate Behaviour registration: " + id);
    type.id = std::move(id);
    type.sourceFilename = std::move(sourceFilename);
    types_.push_back(std::move(type));
}
const BehaviourType* BehaviourRegistry::Find(const std::string& id) const {
    const auto found = std::find_if(types_.begin(), types_.end(), [&](const auto& type) { return type.id == id; });
    return found == types_.end() ? nullptr : &*found;
}
const BehaviourRegistry& GameBehaviours() {
    static const auto registry = [] { BehaviourRegistry value; RegisterGameBehaviours(value); return value; }();
    return registry;
}
BehaviourSettings ResolveBehaviourSettings(const BehaviourType& type, const BehaviourDefinition& definition, const SceneDocument& scene) {
    if (definition.version != type.version) throw std::runtime_error("Unsupported Behaviour version: " + type.name);
    auto values = BehaviourSettings::parse(definition.settings);
    if (!values.is_object()) throw std::runtime_error("Behaviour settings must be an object");
    for (const auto& field : type.fields) {
        if (!values.contains(field.name)) values[field.name] = field.initial;
        const auto& value = values.at(field.name);
        const auto fail = [&] { throw std::runtime_error(type.name + "." + field.name + ": invalid setting"); };
        if (field.initial.is_number()) {
            if (!value.is_number() || (field.initial.is_number_integer() && !value.is_number_integer())) fail();
            const double number = value.get<double>();
            if (!std::isfinite(number) || number < field.minimum || number > field.maximum) fail();
        } else if (field.initial.is_boolean()) { if (!value.is_boolean()) fail(); }
        else if (field.initial.is_string()) {
            if (!value.is_string() || value.get_ref<const std::string&>().size() > 1024) fail();
        } else if (field.initial.is_array()) {
            if (!value.is_array() || value.size() != 3) fail();
            for (const auto& component : value) {
                if (!component.is_number()) fail();
                const auto number = component.get<double>();
                if (!std::isfinite(number) || number < field.minimum || number > field.maximum) fail();
            }
        } else fail();
        if (field.kind == BehaviourFieldKind::Entity) {
            const auto text = value.get<std::string>();
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) fail();
            const auto id = std::stoull(text);
            if (id) (void)scene.Entity(id);
        }
        if (field.kind == BehaviourFieldKind::Asset && !value.get<std::string>().empty()) (void)scene.Asset(value.get<std::string>());
    }
    return values;
}
BehaviourDefinition MakeBehaviour(const BehaviourType& type, const SceneEntity& entity) {
    uint64_t id = 1;
    for (const auto& existing : entity.behaviours) id = std::max(id, existing.id + 1);
    BehaviourSettings settings = BehaviourSettings::object();
    for (const auto& field : type.fields) settings[field.name] = field.initial;
    return {id, type.id, type.version, true, settings.dump()};
}
}
