#include "scene/Scene.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace chromalis;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void Reject(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Require(rejected, "Invalid scene was accepted");
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
void Hierarchy() {
    SceneDocument scene;
    const auto parent = scene.Add("Parent"), child = scene.Add("Child", parent);
    scene.Entity(parent).local.position = {10, 0, 0};
    scene.Entity(parent).local.scale = {2, 3, 4};
    scene.Entity(child).local.position = {1, 2, 3};
    ValidateScene(scene);
    auto worlds = EvaluateWorlds(scene, SceneOrder(scene));
    Require(worlds[1]._41 == 12 && worlds[1]._42 == 6 && worlds[1]._43 == 12, "Parent/local multiplication order");
    scene.Entity(parent).local.rotation = {0, 0, std::sqrt(0.5f), std::sqrt(0.5f)};
    scene.Entity(child).local.rotation = {0, 0, std::sin(DirectX::XM_PI / 8), std::cos(DirectX::XM_PI / 8)};
    worlds = EvaluateWorlds(scene, SceneOrder(scene));
    Require(std::abs(worlds[1]._41 - 4) < 0.001f && std::abs(worlds[1]._42 - 2) < 0.001f, "Parent rotation order");
    Require(std::abs(worlds[1]._11 * worlds[1]._21 + worlds[1]._12 * worlds[1]._22) > 1, "Composed shear was lost");
    Reject([&] { scene.Reparent(parent, child); });
    Require(scene.Entity(parent).parent == 0, "Failed reparent modified scene");
    scene.Reparent(child, 0);
    Require(scene.Entity(child).local.position[0] == 1, "Reparent must keep local");
    scene.Reparent(child, parent);
    const auto grandchild = scene.Add("Grandchild", child);
    scene.DeleteSubtree(child);
    Require(scene.entities.size() == 1 && scene.Add("New") > grandchild, "Subtree deletion or ID reuse");
    auto bad = scene; bad.entities[1].parent = 999;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[1].id = parent;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[0].local.scale[0] = 0;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[0].local.rotation[3] = 2;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.entities[0].local.position[0] = std::numeric_limits<float>::quiet_NaN();
    Reject([&] { ValidateScene(bad); });
    scene.entities[0].local.scale = {0.001f, 0.001f, 0.001f};
    ValidateScene(scene);
    SceneDocument deep;
    EntityId id = 0;
    for (int i = 0; i < 130; ++i) id = deep.Add("Level", id);
    Reject([&] { ValidateScene(deep); });
}
void Persistence(const std::filesystem::path& root) {
    SceneDocument scene;
    const auto model = scene.Add("Model");
    scene.assets.push_back({"model", SceneAssetKind::Skeletal, root / L"素材.cskin"});
    scene.assets.push_back({"effect", SceneAssetKind::Effect, root / "effect.efkefc"});
    scene.Entity(model).asset = "model";
    const auto effect = scene.Add("Effect");
    scene.Entity(effect).asset = "effect";
    scene.Entity(effect).effect.follow = model;
    scene.Entity(effect).effect.joint = "Hand";
    scene.Entity(effect).effect.followTransform = true;
    scene.Entity(model).animation.time = 0.25f;
    MaterialSurface surface;
    surface.shading = ShadingModel::Toon; surface.cutoff = 2; surface.alphaMask = true;
    surface.emission = {0.1f, 0.2f, 0.3f};
    scene.materials.push_back({"shared/toon", surface});
    scene.Entity(model).materials.push_back({0, "shared/toon"});
    scene.view.lighting.color = {1, 0.5f, 0.25f};
    scene.view.lighting.shadow.enabled = true; scene.view.lighting.shadow.resolution = 2048;
    scene.view.lighting.shadow.tint = {0.2f, 0.3f, 0.4f};
    scene.materials[0].surface.castShadow = false; scene.materials[0].surface.receiveShadow = false;
    scene.materials[0].surface.shading = ShadingModel::Pbr;
    scene.materials[0].surface.alphaBlend = true; scene.materials[0].surface.alphaMask = false;
    scene.materials[0].surface.metallic = 0.3f; scene.materials[0].surface.roughness = 0.7f;
    scene.materials[0].surface.normalScale = 0.6f; scene.materials[0].surface.occlusionStrength = 0.2f;
    surface = scene.materials[0].surface;
    const auto path = root / L"シーン.cscene";
    SaveScene(scene, path);
    const auto bytes = Read(path);
    const auto loaded = LoadScene(path);
    Require(loaded.view.lighting.shadow.enabled && loaded.view.lighting.shadow.resolution == 2048 &&
        loaded.view.lighting.shadow.tint == scene.view.lighting.shadow.tint, "Shadow roundtrip failed");
    Require(loaded.materials[0].surface == surface && loaded.Entity(model).materials[0].material == "shared/toon" &&
        loaded.view.lighting.color == scene.view.lighting.color, "Material/light roundtrip failed");
    Require(loaded.Entity(effect).effect.follow == model && loaded.Entity(model).animation.time == 0.25f, "Roundtrip state");
    Require(loaded.Entity(effect).effect.followTransform, "Socket transform mode was not saved");
    auto legacy = bytes;
    legacy.replace(legacy.find("\"version\": 9"), 12, "\"version\": 7");
    { std::ofstream file(root / "legacy.cscene"); file << legacy; }
    Require(!LoadScene(root / "legacy.cscene").Entity(effect).effect.followTransform, "Legacy socket mode changed");
    SaveScene(loaded, root / "nested/copy.cscene");
    Require(LoadScene(root / "nested/copy.cscene").Asset("model").path == scene.Asset("model").path, "Save As rebasing");
    auto bad = scene; bad.Entity(model).parent = effect;
    Reject([&] { SaveScene(bad, path); });
    Require(Read(path) == bytes, "Failed save damaged original");
    bad = scene; bad.materials[0].surface.softness = 0;
    Reject([&] { SaveScene(bad, path); });
    bad = scene; bad.materials[0].surface.cutoff = -1;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.materials[0].surface.emission[0] = std::numeric_limits<float>::quiet_NaN();
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.view.lighting.direction = {0, 0, 0};
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.view.lighting.shadow.resolution = 1000;
    Reject([&] { SaveScene(bad, path); });
    bad = scene; bad.view.lighting.shadow.extent = 0;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.view.lighting.shadow.bias = std::numeric_limits<float>::quiet_NaN();
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.Entity(model).materials.push_back({0, "shared/toon"});
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.Entity(model).materials[0].material = "missing";
    Reject([&] { ValidateScene(bad); });
    Require(Read(path) == bytes, "Invalid material save changed original");
    bad = scene; bad.assets[0].path = path;
    Reject([&] { SaveScene(bad, path); });
    Require(Read(path) == bytes, "Asset collision damaged original");
    const auto alias = root / "alias.cscene";
    if (std::filesystem::exists(alias)) std::filesystem::remove(alias);
    std::filesystem::create_hard_link(path, alias);
    bad.assets[0].path = alias;
    Reject([&] { SaveScene(bad, path); });
    Require(Read(alias) == bytes, "Hardlink collision damaged source");
    std::filesystem::remove(alias);
    Reject([&] { SaveScene(scene, root / "texture.png"); });
    auto malformed = bytes;
    const auto version = malformed.find("\"version\": 9");
    Require(version != std::string::npos, "Missing schema version");
    malformed.replace(version, 12, "\"version\": 10");
    { std::ofstream file(root / "bad.cscene"); file << malformed; }
    Reject([&] { (void)LoadScene(root / "bad.cscene"); });
    scene.DeleteSubtree(model);
    Require(scene.Entity(effect).effect.follow == 0 && scene.Entity(effect).effect.joint.empty(), "Dangling socket after delete");
}
}
// Scene v5の実行設定と、参照先を消した場合の復旧を検証する。
void PlaySettings(const std::filesystem::path& root) {
    SceneDocument scene;
    scene.assets.push_back({"character", SceneAssetKind::Skeletal, root / "Character.cskin"});
    const auto id = scene.Add("Any name"); scene.Entity(id).asset = "character";
    scene.play.player = scene.play.cameraTarget = id;
    scene.play.walkSpeed = 2; scene.play.runSpeed = 4; scene.play.fixedTarget = {1,2,3};
    SaveScene(scene, root / "Play.cscene");
    const auto loaded = LoadScene(root / "Play.cscene");
    Require(loaded.play.player == id && loaded.play.cameraTarget == id && loaded.play.walkSpeed == 2 &&
        loaded.play.runSpeed == 4 && loaded.play.fixedTarget == scene.play.fixedTarget && loaded.play.clips == scene.play.clips, "Play settings roundtrip");
    auto bad = scene; bad.play.player = 900;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.play.cameraTarget = 900;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.play.runSpeed = -1;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.play.minimum = bad.play.maximum;
    Reject([&] { ValidateScene(bad); });
    bad = scene; bad.play.cameraHeight = std::numeric_limits<float>::quiet_NaN();
    Reject([&] { ValidateScene(bad); });
    scene.DeleteSubtree(id);
    Require(!scene.play.player && !scene.play.cameraTarget, "Dangling Play reference after delete");
    ValidateScene(scene);
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Expected test output directory");
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        Hierarchy(); Persistence(root); PlaySettings(root);
        std::cout << "PASS: hierarchy, IDs, invalid transforms, socket dependency, atomic persistence, relative paths\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
