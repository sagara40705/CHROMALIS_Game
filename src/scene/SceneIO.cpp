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
    }
    throw std::runtime_error("Invalid scene asset kind");
}
SceneAssetKind ReadKind(const std::string& name) {
    if (name == "static") return SceneAssetKind::Static;
    if (name == "skeletal") return SceneAssetKind::Skeletal;
    if (name == "effect") return SceneAssetKind::Effect;
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
    Json root{{"version", 1}, {"nextId", std::to_string(document.nextId)}};
    const auto& view = document.view;
    root["view"] = {{"background", view.background}, {"yaw", view.yaw}, {"pitch", view.pitch}, {"distance", view.distance}, {"unlit", view.unlit}};
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
            {"animation", {{"clip", a.clip}, {"time", a.time}, {"speed", a.speed}, {"loop", a.loop}, {"autoplay", a.autoplay}, {"restPose", a.restPose}, {"bindPose", a.bindPose}}},
            {"effect", {{"count", e.count}, {"autoplay", e.autoplay}, {"follow", std::to_string(e.follow)}, {"joint", e.joint}}}});
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
        if (root.at("version") != 1) throw std::runtime_error("Unsupported scene version");
        SceneDocument document;
        document.nextId = Id(root.at("nextId"));
        const auto& view = root.at("view");
        document.view.background = view.at("background").get<std::array<float, 4>>();
        document.view.yaw = view.at("yaw").get<float>();
        document.view.pitch = view.at("pitch").get<float>();
        document.view.distance = view.at("distance").get<float>();
        document.view.unlit = view.at("unlit").get<bool>();
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
                a.at("loop").get<bool>(), a.at("autoplay").get<bool>(), a.at("restPose").get<bool>(), a.at("bindPose").get<bool>()};
            const auto& e = value.at("effect");
            entity.effect = {e.at("count").get<int>(), e.at("autoplay").get<bool>(), Id(e.at("follow")), e.at("joint").get<std::string>()};
            document.entities.push_back(std::move(entity));
        }
        ValidateScene(document);
        return document;
    } catch (const std::exception& error) {
        throw std::runtime_error("Scene " + Text(path) + ": " + error.what());
    }
}
void SaveScene(const SceneDocument& document, const std::filesystem::path& path) {
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
