#include "scene/SceneRuntime.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace chromalis;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct TestWindow {
    HWND handle = nullptr;
    TestWindow() {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"ChromalisSceneTests";
        CheckWin32(RegisterClassW(&type), "Register test window");
        handle = CreateWindowW(type.lpszClassName, L"CHROMALIS Scene Tests", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, nullptr, nullptr, type.hInstance, nullptr);
        Require(handle != nullptr, "Create test window");
        ShowWindow(handle, SW_SHOWNOACTIVATE);
    }
    ~TestWindow() { if (handle) DestroyWindow(handle); }
};
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}
void Frame(Renderer& renderer, SceneRuntime& scene, bool capture = false) {
    Pump(); scene.Update(1.0f / 60);
    const auto& view = scene.Document().view;
    RenderSettings settings;
    settings.clear = view.background; settings.cameraYaw = view.yaw; settings.cameraPitch = view.pitch;
    settings.cameraDistance = view.distance; settings.unlit = view.unlit;
    auto* commands = renderer.BeginFrame(settings, scene.DrawItems());
    scene.DrawEffects(commands); renderer.BeginOverlay(); renderer.EndFrame(false, capture);
}
void IndependentPalettes(Renderer& renderer, SceneRuntime& scene, const std::filesystem::path& skin, const std::filesystem::path& root) {
    SceneDocument document;
    document.view.yaw = 0; document.view.pitch = 0; document.view.distance = 6; document.view.unlit = true;
    const auto asset = LoadSkeletalAsset(skin);
    document.assets.push_back({asset.mesh.id, SceneAssetKind::Skeletal, skin});
    const auto libraryPath = root / "palettes.canim";
    SaveAnimationLibrary({"scene/shared", asset.joints, asset.clips, {}}, libraryPath);
    document.assets.push_back({"scene/shared", SceneAssetKind::Animation, libraryPath});
    for (int i = 0; i < 3; ++i) {
        const auto id = document.Add("Independent pose");
        auto& entity = document.Entity(id);
        entity.asset = asset.mesh.id; entity.local.scale = {0.5f, 0.5f, 0.5f};
        entity.local.position = {-1.5f + i * 1.5f, 0, 0};
        entity.animation.time = i * 0.4f; entity.animation.autoplay = false;
        entity.animation.library = "scene/shared";
    }
    Require(scene.Apply(document), "Palette scene failed");
    Require(scene.DrawItems()[0].mesh == scene.DrawItems()[1].mesh, "Shared mesh was duplicated");
    Require(scene.Animation(1) != scene.Animation(2), "Playback must belong to each entity");
    Require(scene.Animation(1)->Rig() == scene.Animation(2)->Rig(), "Shared animation rig was duplicated");
    for (int i = 0; i < 5; ++i) Frame(renderer, scene, i == 4);
    const auto path = root / "palettes.bmp";
    renderer.SaveCapture(path, false);
    // 各モデルの左端付近を独立に投影する。同じBone領域を上書きすると外れる。
    std::ifstream file(path, std::ios::binary);
    BITMAPFILEHEADER header{}; BITMAPINFOHEADER info{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header)); file.read(reinterpret_cast<char*>(&info), sizeof(info));
    const float focal = renderer.Height() * 0.5f / std::tan(DirectX::XM_PIDIV4 * 0.5f);
    for (int i = 0; i < 3; ++i) {
        const float x = -1.5f + i * 1.5f + i * 0.2f - 0.43f;
        const auto px = static_cast<int>(renderer.Width() * 0.5f + focal * x / 5.5f);
        const auto py = static_cast<int>(renderer.Height() * 0.5f - focal * 0.3f / 5.5f);
        file.seekg(header.bfOffBits + (static_cast<std::streamoff>(py) * renderer.Width() + px) * 4);
        unsigned char bgr[3]{}; file.read(reinterpret_cast<char*>(bgr), 3);
        Require(file.good() && bgr[0] < 3 && bgr[1] < 3 && bgr[2] > 252, "Independent skin palette pixel mismatch");
    }
    Require(scene.Save(root / "shared.cscene") && scene.Open(root / "shared.cscene"), "Shared scene roundtrip failed");
    Require(scene.Document().Entity(2).animation.library == "scene/shared", "Library reference lost during save");
    const auto player = scene.Animation(2);
    const auto rig = player->Rig();
    const auto time = player->playback.time;
    const auto bytes = ReadAssetBytes(libraryPath);
    { std::ofstream libraryFile(libraryPath, std::ios::binary); libraryFile << "invalid"; }
    Require(!scene.ReloadAsset("scene/shared"), "Invalid animation reload accepted");
    Require(scene.Animation(2) == player && player->Rig() == rig && player->playback.time == time, "Failed reload lost live pose");
    { std::ofstream libraryFile(libraryPath, std::ios::binary); libraryFile.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    Require(scene.ReloadAsset("scene/shared"), "Common animation reload failed");
    Require(scene.Animation(1)->Rig() == scene.Animation(2)->Rig() && scene.Animation(2)->Rig() != rig, "Reload did not replace all shared users");
    // 正常な別モデルへの差し替えでも共通アニメーションの参照を維持する。
    auto variant = asset; variant.mesh.id = "scene/variant"; variant.clips.clear();
    const auto variantPath = root / "variant.cskin";
    SaveSkeletalAsset(variant, variantPath);
    Require(scene.AssignAsset(2, variantPath, SceneAssetKind::Skeletal), "Compatible model replacement failed");
    Require(scene.Document().Entity(2).animation.library == "scene/shared", "Model replacement lost animation library");
    const auto oldModel = scene.Animation(2);
    variant.joints[0].rest.translation[0] += 1;
    SaveSkeletalAsset(variant, variantPath);
    Require(!scene.ReloadAsset(variant.mesh.id) && scene.Animation(2) == oldModel, "Incompatible model reload lost live state");
    Frame(renderer, scene);
    Require(scene.Delete(1) && scene.Delete(2), "Shared user deletion failed");
    Require(scene.Document().Asset("scene/shared").kind == SceneAssetKind::Animation, "Deleting one user removed shared library");
    Require(scene.Delete(3) && scene.Document().assets.empty(), "Unused shared library was retained");
}
void NormalTransform(Renderer& renderer, SceneRuntime& scene, const std::filesystem::path& skin, const std::filesystem::path& root) {
    auto mesh = LoadSkeletalAsset(skin).mesh;
    mesh.id = "scene/normal";
    for (auto& vertex : mesh.vertices) { vertex.normal[0] = 1 / std::sqrt(3.0f); vertex.normal[1] = vertex.normal[0]; vertex.normal[2] = -vertex.normal[0]; }
    mesh.materials[0].unlit = 0;
    mesh.textures = {{1, 1, {255, 255, 255, 255}}};
    const auto path = root / "normal.cmesh";
    SaveStaticAsset(mesh, path);
    SceneDocument document;
    document.view.yaw = 0; document.view.pitch = 0; document.view.distance = 6;
    document.assets.push_back({mesh.id, SceneAssetKind::Static, path});
    const auto id = document.Add("Nonuniform normal");
    document.Entity(id).asset = mesh.id; document.Entity(id).local.scale = {2, 1, 0.5f};
    Require(scene.Apply(document), "Normal scene failed");
    Frame(renderer, scene, true);
    const auto capture = root / "normal.bmp";
    renderer.SaveCapture(capture, false);
    std::ifstream file(capture, std::ios::binary);
    BITMAPFILEHEADER header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    file.seekg(header.bfOffBits + (static_cast<std::streamoff>(renderer.Height() / 2) * renderer.Width() + renderer.Width() / 2) * 4);
    unsigned char bgr[3]{}; file.read(reinterpret_cast<char*>(bgr), 3);
    // 法線(1,1,-1)の逆転置結果(0.5,1,-2)を独立に計算する。
    const double light = 0.3 + 0.7 * 1.8 / std::sqrt(5.25 * 1.16);
    const auto expected = static_cast<int>(std::lround(255 * (1.055 * std::pow(light, 1.0 / 2.4) - 0.055)));
    for (auto channel : bgr) Require(std::abs(static_cast<int>(channel) - expected) <= 3, "Nonuniform inverse-transpose pixel mismatch");
}
SceneDocument Example(const std::filesystem::path& cube, const std::filesystem::path& figure, const std::filesystem::path& effect) {
    SceneDocument document;
    const auto mesh = LoadStaticAsset(cube); const auto skin = LoadSkeletalAsset(figure);
    document.assets = {{mesh.id, SceneAssetKind::Static, cube}, {skin.mesh.id, SceneAssetKind::Skeletal, figure}, {"scene/pulse", SceneAssetKind::Effect, effect}};
    const auto group = document.Add("Group");
    document.Entity(group).local.position = {0, -0.5f, 0};
    for (int i = 0; i < 3; ++i) {
        const auto id = document.Add("Cube " + std::to_string(i), group);
        auto& entity = document.Entity(id); entity.asset = mesh.id;
        entity.local.position = {-2.5f + i * 2.5f, -0.5f, 1}; entity.local.scale = {0.4f, 0.4f, 0.4f};
    }
    for (int i = 0; i < 2; ++i) {
        const auto id = document.Add("Character " + std::to_string(i), group);
        auto& entity = document.Entity(id); entity.asset = skin.mesh.id;
        entity.local.position = {-1.2f + 2.4f * i, 0, 0}; entity.animation.time = skin.clips[0].duration * i * 0.4f;
    }
    const auto vfx = document.Add("Socket effect");
    document.Entity(vfx).asset = "scene/pulse";
    document.Entity(vfx).effect = {3, true, 5, skin.joints[0].name};
    document.Entity(vfx).local.scale = {0.2f, 0.2f, 0.2f};
    return document;
}
void Transactions(Renderer& renderer, SceneRuntime& scene, const SceneDocument& example, const std::filesystem::path& root) {
    Require(scene.Apply(example), "Example scene failed");
    Require(scene.Save(root / "Example.cscene"), "Scene save failed");
    Require(scene.DrawItems().size() == 5, "Draw list still has fixed preview limits");
    Frame(renderer, scene);
    const auto generation = scene.Generation(5), revision = scene.Revision();
    const auto player = scene.Animation(5); const auto time = player->playback.time;
    auto bad = example; bad.Entity(5).animation.clip = "missing-clip";
    Require(!scene.Apply(bad), "Missing clip accepted");
    Require(scene.Animation(5) == player && player->playback.time == time && scene.Revision() == revision, "Failed transaction altered live state");
    bad = example; bad.Entity(7).effect.joint = "missing-joint";
    Require(!scene.Apply(bad) && scene.Generation(5) == generation, "Missing socket changed scene");
    Require(!scene.Open(root / "missing.cscene") && scene.Animation(5) == player, "Failed open lost old scene");
    const auto cube = example.assets[0].path;
    const auto bytes = ReadAssetBytes(cube);
    { std::ofstream file(cube, std::ios::binary); file << "invalid"; }
    const auto old = scene.Generation(2);
    Require(!scene.ReloadAsset(example.assets[0].id) && scene.Generation(2) == old, "Failed reload lost shared mesh");
    { std::ofstream file(cube, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    Require(scene.ReloadAsset(example.assets[0].id) && scene.Generation(2) != old && scene.Generation(2) == scene.Generation(3), "Shared reload failed");
    Require(scene.Animation(5) == player && player->playback.time == time, "Unrelated reload restarted animation");
    scene.Update(0);
    const auto position = scene.World(7);
    Require(scene.Delete(5), "Delete socket target failed");
    Require(!scene.Document().Entity(7).effect.follow && scene.World(7)._41 == position._41, "Delete did not detach at current position");
    Require(scene.Open(root / "Example.cscene"), "Scene restore failed");
    const auto moved = root / "relocated.cmesh";
    std::filesystem::copy_file(cube, moved, std::filesystem::copy_options::overwrite_existing);
    const auto sharedGeneration = scene.Generation(2);
    Require(!scene.ReloadAsset(example.assets[0].id, root / "missing.cmesh") && scene.Generation(2) == sharedGeneration, "Failed relocation lost live mesh");
    Require(scene.ReloadAsset(example.assets[0].id, moved) && scene.Dirty() && scene.Generation(2) != sharedGeneration,
        "Asset relocation failed");
    Require(scene.Document().Asset(example.assets[0].id).path == moved && scene.Generation(2) == scene.Generation(3), "Shared relocation mismatch");
    Require(!scene.AssignAsset(2, {"incorrect/id", SceneAssetKind::Static, cube}), "Mismatched catalog ID accepted");
    Require(scene.Document().Entity(2).asset == example.assets[0].id, "Failed assignment changed reference");
    auto expanded = scene.Document();
    const auto extraEffect = expanded.Add("Catalog effect");
    Require(scene.Apply(expanded), "Add effect target failed");
    Require(scene.AssignAsset(extraEffect, {"catalog/effect", SceneAssetKind::Effect, example.assets[2].path}), "Stable VFX ID assignment failed");
    Require(scene.Document().Entity(extraEffect).asset == "catalog/effect", "VFX catalog ID replaced with entity ID");
    Frame(renderer, scene);
    Require(scene.Open(root / "Example.cscene"), "Restore after catalog operations failed");
    Require(scene.Animation(5)->playback.time == example.Entity(5).animation.time && !scene.Dirty(), "Saved initial state was not restored");
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 60; ++i) {
        if (i == 15) renderer.Resize(800, 600);
        if (i == 30) renderer.Resize(960, 720);
        Frame(renderer, scene, i == 59);
    }
    renderer.SaveCapture(root / "scene.bmp", false);
    std::cout << "Scene mean wall ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60
        << " last GPU ms=" << renderer.GpuFrameMs() << '\n';
    scene.Clear(); renderer.WaitIdle(); renderer.DrainDiagnostics();
    Require(renderer.DiagnosticErrors() == 0, "D3D12 validation errors");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 7, "Expected output, cube, figure, palette skin, effect, warp/hardware");
        const auto root = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(root);
        const auto cube = root / "cube.cmesh";
        std::filesystem::copy_file(argv[2], cube, std::filesystem::copy_options::overwrite_existing);
        Log log(root / "scene.log"); TestWindow window; Renderer renderer(log);
        const bool warp = std::wstring_view(argv[6]) == L"warp";
        renderer.Initialize(window.handle, 960, 720, {warp, true, !warp});
        SceneRuntime scene(renderer, log);
        IndependentPalettes(renderer, scene, std::filesystem::absolute(argv[4]), root);
        // 最小化中は描画せず、復帰後に同じSceneとGPUリソースを再利用する。
        ShowWindow(window.handle, SW_MINIMIZE); Pump();
        Require(IsIconic(window.handle) != FALSE, "Window did not minimize");
        ShowWindow(window.handle, SW_RESTORE); Pump();
        Require(IsIconic(window.handle) == FALSE, "Window did not restore");
        Frame(renderer, scene);
        NormalTransform(renderer, scene, std::filesystem::absolute(argv[4]), root);
        Transactions(renderer, scene, Example(cube, std::filesystem::absolute(argv[3]), std::filesystem::absolute(argv[5])), root);
        std::cout << "PASS: independent GPU palettes, shared resources, hierarchy, reload rollback, socket detach, persistence, resize, release\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
