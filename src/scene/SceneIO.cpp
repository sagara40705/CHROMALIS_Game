#include "scene/Scene.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <charconv>
#include <fstream>
#include <stdexcept>

namespace chromalis {
namespace {
using Json = nlohmann::json;
std::string Text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::filesystem::path Path(const std::string& text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
EntityId Id(const Json& value) {
    const auto text = value.get<std::string>();
    EntityId result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) throw std::runtime_error("Invalid decimal scene ID");
    return result;
}
const char* KindName(SceneAssetKind kind) {
    switch (kind) {
    case SceneAssetKind::Static: return "static";
    case SceneAssetKind::Skeletal: return "skeletal";
    case SceneAssetKind::Effect: return "effect";
    case SceneAssetKind::Animation: return "animation";
    }
    throw std::runtime_error("Invalid scene asset kind");
}
SceneAssetKind ReadKind(const std::string& name) {
    if (name == "static") return SceneAssetKind::Static;
    if (name == "skeletal") return SceneAssetKind::Skeletal;
    if (name == "effect") return SceneAssetKind::Effect;
    if (name == "animation") return SceneAssetKind::Animation;
    throw std::runtime_error("Unknown scene asset kind: " + name);
}
bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code error;
    if (std::filesystem::equivalent(a, b, error) && !error) return true;
    const auto left = std::filesystem::weakly_canonical(a).wstring();
    const auto right = std::filesystem::weakly_canonical(b).wstring();
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
Json Encode(const SceneDocument& document, const std::filesystem::path& destination) {
    Json root{{"version", 9}, {"nextId", std::to_string(document.nextId)}};
    const auto& view = document.view;
    const auto& p = document.play;
    root["play"] = {{"player", std::to_string(p.player)}, {"cameraTarget", std::to_string(p.cameraTarget)},
        {"walkSpeed", p.walkSpeed}, {"runSpeed", p.runSpeed}, {"turnSpeed", p.turnSpeed},
        {"forwardYaw", p.forwardYaw}, {"groundHeight", p.groundHeight}, {"minimum", p.minimum}, {"maximum", p.maximum},
        {"clips", p.clips}, {"fixedTarget", p.fixedTarget}, {"cameraHeight", p.cameraHeight}, {"cameraFollow", p.cameraFollow}};
    root["view"] = {{"background", view.background}, {"yaw", view.yaw}, {"pitch", view.pitch}, {"distance", view.distance}, {"unlit", view.unlit}};
    root["view"]["lighting"] = {{"direction", view.lighting.direction}, {"color", view.lighting.color}, {"ambient", view.lighting.ambient}};
    const auto& shadow = view.lighting.shadow;
    root["view"]["lighting"]["shadow"] = {{"enabled", shadow.enabled}, {"resolution", shadow.resolution},
        {"center", shadow.center}, {"tint", shadow.tint}, {"extent", shadow.extent}, {"depth", shadow.depth},
        {"strength", shadow.strength}, {"bias", shadow.bias}, {"normalBias", shadow.normalBias}, {"filterRadius", shadow.filterRadius}};
    root["materials"] = Json::array();
    for (const auto& material : document.materials) {
        const auto& m = material.surface;
        root["materials"].push_back({{"id", material.id}, {"baseColor", m.baseColor}, {"emission", m.emission}, {"shadow", m.shadow},
            {"shading", m.shading == ShadingModel::Lit ? "lit" : m.shading == ShadingModel::Unlit ? "unlit" : m.shading == ShadingModel::Pbr ? "pbr" : "toon"},
            {"alphaBlend", m.alphaBlend}, {"metallic", m.metallic}, {"roughness", m.roughness}, {"normalScale", m.normalScale}, {"occlusionStrength", m.occlusionStrength},
            {"cutoff", m.cutoff}, {"threshold", m.threshold}, {"softness", m.softness}, {"alphaMask", m.alphaMask}, {"doubleSided", m.doubleSided},
            {"castShadow", m.castShadow}, {"receiveShadow", m.receiveShadow}});
    }
    root["assets"] = Json::array();
    const auto base = std::filesystem::absolute(destination).parent_path();
    for (const auto& asset : document.assets) {
        if (SamePath(destination, asset.path)) throw std::runtime_error("Scene output collides with an asset");
        auto relative = std::filesystem::absolute(asset.path).lexically_normal().lexically_relative(base);
        if (relative.empty() || relative.is_absolute()) throw std::runtime_error("Scene and assets must be on the same volume");
        root["assets"].push_back({{"id", asset.id}, {"kind", KindName(asset.kind)}, {"path", Text(relative)}});
    }
    root["entities"] = Json::array();
    for (const auto& entity : document.entities) {
        const auto& a = entity.animation;
        const auto& e = entity.effect;
        root["entities"].push_back({{"id", std::to_string(entity.id)}, {"parent", std::to_string(entity.parent)},
            {"name", entity.name}, {"asset", entity.asset},
            {"transform", {{"position", entity.local.position}, {"rotation", entity.local.rotation}, {"scale", entity.local.scale}}},
            {"animation", {{"library", a.library}, {"clip", a.clip}, {"time", a.time}, {"speed", a.speed}, {"loop", a.loop}, {"autoplay", a.autoplay}, {"restPose", a.restPose}, {"bindPose", a.bindPose}}},
            {"effect", {{"count", e.count}, {"autoplay", e.autoplay}, {"follow", std::to_string(e.follow)}, {"joint", e.joint}, {"followTransform", e.followTransform}}}});
        auto& behaviours = root["entities"].back()["behaviours"] = Json::array();
        const auto& c = entity.collider; const auto& b = entity.rigidBody;
        root["entities"].back()["collider"] = {{"enabled", c.enabled}, {"shape", static_cast<int>(c.shape)},
            {"center", c.center}, {"size", c.size}, {"radius", c.radius}, {"height", c.height}};
        root["entities"].back()["rigidBody"] = {{"enabled", b.enabled}, {"motion", static_cast<int>(b.motion)},
            {"mass", b.mass}, {"friction", b.friction}, {"restitution", b.restitution}, {"gravityFactor", b.gravityFactor},
            {"linearDamping", b.linearDamping}, {"angularDamping", b.angularDamping}, {"lockRotation", b.lockRotation},
            {"continuous", b.continuous}, {"velocity", b.velocity}};
        for (const auto& behaviour : entity.behaviours) {
            const auto settings = Json::parse(behaviour.settings);
            if (!settings.is_object()) throw std::runtime_error("Behaviour settings must be an object");
            behaviours.push_back({{"id", std::to_string(behaviour.id)}, {"type", behaviour.type}, {"version", behaviour.version}, {"enabled", behaviour.enabled}, {"settings", settings}});
        }
        auto& bindings = root["entities"].back()["materials"] = Json::array();
        for (const auto& binding : entity.materials) bindings.push_back({{"slot", binding.slot}, {"material", binding.material}});
    }
    return root;
}
}

SceneDocument LoadScene(const std::filesystem::path& path) {
    try {
        const auto size = std::filesystem::file_size(path);
        if (size > 16u * 1024 * 1024) throw std::runtime_error("Scene exceeds 16 MiB");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open scene");
        const auto root = Json::parse(stream);
        if (!root.at("version").is_number_integer() || root.at("version") < 1 || root.at("version") > 9) throw std::runtime_error("Unsupported scene version");
        SceneDocument document;
        document.legacyBehaviours = root.at("version") < 7;
        document.nextId = Id(root.at("nextId"));
        if (root.at("version") >= 5) {
            const auto& value = root.at("play"); auto& p = document.play;
            p.player = Id(value.at("player")); p.cameraTarget = Id(value.at("cameraTarget"));
            p.walkSpeed = value.at("walkSpeed"); p.runSpeed = value.at("runSpeed"); p.turnSpeed = value.at("turnSpeed");
            p.forwardYaw = value.at("forwardYaw"); p.groundHeight = value.at("groundHeight");
            p.minimum = value.at("minimum").get<std::array<float, 2>>(); p.maximum = value.at("maximum").get<std::array<float, 2>>();
            p.clips = value.at("clips").get<std::array<std::string, 3>>(); p.fixedTarget = value.at("fixedTarget").get<std::array<float, 3>>();
            p.cameraHeight = value.at("cameraHeight"); p.cameraFollow = value.at("cameraFollow");
        }
        const auto& view = root.at("view");
        document.view.background = view.at("background").get<std::array<float, 4>>();
        document.view.yaw = view.at("yaw").get<float>();
        document.view.pitch = view.at("pitch").get<float>();
        document.view.distance = view.at("distance").get<float>();
        document.view.unlit = view.at("unlit").get<bool>();
        if (root.at("version") >= 3) {
            const auto& lighting = view.at("lighting");
            document.view.lighting = {lighting.at("direction").get<std::array<float, 3>>(),
                lighting.at("color").get<std::array<float, 3>>(), lighting.at("ambient").get<std::array<float, 3>>()};
            if (root.at("version") >= 4) {
                const auto& s = lighting.at("shadow");
                auto& shadow = document.view.lighting.shadow;
                shadow.enabled = s.at("enabled");
                if (!s.at("resolution").is_number_unsigned() || s.at("resolution").get<uint64_t>() > 4096) throw std::runtime_error("Invalid shadow resolution");
                shadow.resolution = s.at("resolution");
                shadow.center = s.at("center").get<std::array<float, 3>>(); shadow.tint = s.at("tint").get<std::array<float, 3>>();
                shadow.extent = s.at("extent"); shadow.depth = s.at("depth"); shadow.strength = s.at("strength");
                shadow.bias = s.at("bias"); shadow.normalBias = s.at("normalBias"); shadow.filterRadius = s.at("filterRadius");
            }
            const auto& materials = root.at("materials");
            if (!materials.is_array() || materials.size() > 4096) throw std::runtime_error("Invalid material collection");
            for (const auto& value : materials) {
                SceneDocument::Material material;
                material.id = value.at("id");
                auto& m = material.surface;
                m.baseColor = value.at("baseColor").get<std::array<float, 4>>();
                m.emission = value.at("emission").get<std::array<float, 3>>();
                m.shadow = value.at("shadow").get<std::array<float, 3>>();
                const auto shading = value.at("shading").get<std::string>();
                if (shading != "lit" && shading != "unlit" && shading != "toon" && shading != "pbr") throw std::runtime_error("Unknown shading model");
                m.shading = shading == "lit" ? ShadingModel::Lit : shading == "unlit" ? ShadingModel::Unlit : shading == "pbr" ? ShadingModel::Pbr : ShadingModel::Toon;
                m.cutoff = value.at("cutoff"); m.threshold = value.at("threshold"); m.softness = value.at("softness");
                if (root.at("version") >= 6) {
                    m.alphaBlend = value.at("alphaBlend"); m.metallic = value.at("metallic"); m.roughness = value.at("roughness");
                    m.normalScale = value.at("normalScale"); m.occlusionStrength = value.at("occlusionStrength");
                }
                m.alphaMask = value.at("alphaMask"); m.doubleSided = value.at("doubleSided");
                if (root.at("version") >= 4) { m.castShadow = value.at("castShadow"); m.receiveShadow = value.at("receiveShadow"); }
                document.materials.push_back(std::move(material));
            }
        }
        const auto& assets = root.at("assets");
        const auto& entities = root.at("entities");
        if (!assets.is_array() || !entities.is_array() || assets.size() > 10000 || entities.size() > 10000) throw std::runtime_error("Invalid scene collections");
        const auto base = std::filesystem::absolute(path).parent_path();
        for (const auto& value : assets) {
            const auto relative = Path(value.at("path").get<std::string>());
            if (relative.empty() || relative.has_root_path()) throw std::runtime_error("Scene asset path must be relative");
            document.assets.push_back({value.at("id").get<std::string>(), ReadKind(value.at("kind").get<std::string>()), (base / relative).lexically_normal()});
        }
        for (const auto& value : entities) {
            SceneEntity entity;
            entity.id = Id(value.at("id")); entity.parent = Id(value.at("parent"));
            entity.name = value.at("name").get<std::string>(); entity.asset = value.at("asset").get<std::string>();
            const auto& transform = value.at("transform");
            entity.local.position = transform.at("position").get<std::array<float, 3>>();
            entity.local.rotation = transform.at("rotation").get<std::array<float, 4>>();
            entity.local.scale = transform.at("scale").get<std::array<float, 3>>();
            const auto& a = value.at("animation");
            entity.animation = {a.at("clip").get<std::string>(), a.at("time").get<float>(), a.at("speed").get<float>(),
                a.at("loop").get<bool>(), a.at("autoplay").get<bool>(), a.at("restPose").get<bool>(), a.at("bindPose").get<bool>(), a.value("library", std::string{})};
            const auto& e = value.at("effect");
            entity.effect = {e.at("count").get<int>(), e.at("autoplay").get<bool>(), Id(e.at("follow")), e.at("joint").get<std::string>()};
            if (root.at("version") >= 8) entity.effect.followTransform = e.at("followTransform").get<bool>();
            if (root.at("version") >= 9) {
                const auto& c = value.at("collider"); const auto& b = value.at("rigidBody");
                if (!c.at("shape").is_number_integer() || c.at("shape") < 0 || c.at("shape") > 2 ||
                    !b.at("motion").is_number_integer() || b.at("motion") < 0 || b.at("motion") > 2)
                    throw std::runtime_error("Invalid physics shape/motion");
                entity.collider = {c.at("enabled").get<bool>(), static_cast<ColliderShape>(c.at("shape").get<int>()),
                    c.at("center").get<std::array<float, 3>>(), c.at("size").get<std::array<float, 3>>(), c.at("radius").get<float>(), c.at("height").get<float>()};
                entity.rigidBody = {b.at("enabled").get<bool>(), static_cast<BodyMotion>(b.at("motion").get<int>()),
                    b.at("mass").get<float>(), b.at("friction").get<float>(), b.at("restitution").get<float>(), b.at("gravityFactor").get<float>(),
                    b.at("linearDamping").get<float>(), b.at("angularDamping").get<float>(), b.at("lockRotation").get<bool>(),
                    b.at("continuous").get<bool>(), b.at("velocity").get<std::array<float, 3>>()};
            }
            if (root.at("version") >= 3) {
                const auto& bindings = value.at("materials");
                if (!bindings.is_array() || bindings.size() > 4096) throw std::runtime_error("Invalid material bindings");
                for (const auto& binding : bindings) {
                    const auto& slot = binding.at("slot");
                    if (!slot.is_number_unsigned() || slot.get<uint64_t>() > UINT32_MAX) throw std::runtime_error("Invalid material slot");
                    entity.materials.push_back({slot.get<uint32_t>(), binding.at("material").get<std::string>()});
                }
            }
            if (root.at("version") >= 7) {
                const auto& behaviours = value.at("behaviours");
                if (!behaviours.is_array() || behaviours.size() > 128) throw std::runtime_error("Invalid Behaviour collection");
                for (const auto& b : behaviours) {
                    if (!b.at("settings").is_object() || !b.at("version").is_number_unsigned() || b.at("version").get<uint64_t>() > UINT32_MAX)
                        throw std::runtime_error("Invalid Behaviour settings/version");
                    entity.behaviours.push_back({Id(b.at("id")), b.at("type").get<std::string>(), b.at("version").get<uint32_t>(), b.at("enabled").get<bool>(), b.at("settings").dump()});
                }
            }
            document.entities.push_back(std::move(entity));
        }
        MigrateSceneBehaviours(document);
        ValidateScene(document);
        return document;
    } catch (const std::exception& error) {
        throw std::runtime_error("Scene " + Text(path) + ": " + error.what());
    }
}
void MigrateSceneBehaviours(SceneDocument& document) {
    if (!document.legacyBehaviours) return;
    if (document.play.player) {
        const auto& p = document.play;
        Json settings{{"walkSpeed", p.walkSpeed}, {"runSpeed", p.runSpeed}, {"turnSpeed", p.turnSpeed},
            {"forwardYaw", p.forwardYaw}, {"groundHeight", p.groundHeight},
            {"minX", p.minimum[0]}, {"minZ", p.minimum[1]}, {"maxX", p.maximum[0]}, {"maxZ", p.maximum[1]},
            {"idleClip", p.clips[0]}, {"walkClip", p.clips[1]}, {"runClip", p.clips[2]},
            {"cameraTarget", std::to_string(p.cameraTarget)}, {"cameraHeight", p.cameraHeight}, {"cameraFollow", p.cameraFollow}};
        auto& entity = document.Entity(p.player);
        uint64_t id = 1;
        for (const auto& b : entity.behaviours) id = std::max(id, b.id + 1);
        entity.behaviours.push_back({id, "chromalis.player", 1, true, settings.dump()});
    }
    document.legacyBehaviours = false;
}
void SaveScene(const SceneDocument& source, const std::filesystem::path& path) {
    auto document = source;
    MigrateSceneBehaviours(document);
    if (path.extension() != L".cscene") throw std::runtime_error("Scene output must use .cscene extension");
    ValidateScene(document);
    const auto text = Encode(document, path).dump(2) + "\n";
    if (text.size() > 16u * 1024 * 1024) throw std::runtime_error("Scene exceeds 16 MiB");
    const auto destination = std::filesystem::absolute(path);
    std::filesystem::create_directories(destination.parent_path());
    auto temporary = destination;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    // CREATE_NEWで既存ファイルを一切開かず、検証済みの候補だけを公開する。
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create scene temporary file");
    try {
        DWORD written = 0;
        const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size() && FlushFileBuffers(file);
        CloseHandle(file); file = INVALID_HANDLE_VALUE;
        if (!ok) throw std::runtime_error("Scene write failed");
        (void)LoadScene(temporary);
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Scene publish failed");
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
}
