#include "gameplay/ScenePlayback.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

using namespace chromalis;
using namespace DirectX;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Window {
    HWND handle{};
    Window() {
        WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"ChromalisPlayerTest";
        CheckWin32(RegisterClassW(&type), "Register player test");
        handle = CreateWindowW(type.lpszClassName, L"CHROMALIS Player Test", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, nullptr, nullptr, type.hInstance, nullptr);
        CheckWin32(handle != nullptr, "Create player test window");
    }
    ~Window() { if (handle) DestroyWindow(handle); }
};
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}
SceneDocument Fixture(const std::filesystem::path& root) {
    SkeletalAsset asset;
    asset.mesh.id = "test/player";
    asset.mesh.vertices = {{{-0.3f,0,0},{0,0,-1},{0,0}}, {{0,1.7f,0},{0,0,-1},{0,0}}, {{0.3f,0,0},{0,0,-1},{0,0}}};
    asset.mesh.indices = {0,1,2}; asset.mesh.parts = {{0,3,0}};
    asset.mesh.materials.emplace_back(); asset.mesh.materials[0].doubleSided = 1;
    asset.mesh.textures = {{1,1,{255,255,255,255}}};
    asset.joints = {{"Root",-1,{}}}; asset.skin.resize(3); asset.bindings.resize(1);
    XMStoreFloat4x4(&asset.bindings[0].inverseBind, XMMatrixIdentity());
    for (const auto* name : {"Idle", "Walk", "Run"})
        asset.clips.push_back({name,1,{{0,0,{{0,{0,0,0,0}}, {0.5f,{0,0.15f,0,0}}, {1,{0,0,0,0}}}}}});
    SaveSkeletalAsset(asset, root / "Player.cskin");
    SaveAnimationLibrary({"test/common", asset.joints, asset.clips, {}}, root / "Common.canim");
    SceneDocument document;
    document.assets = {{asset.mesh.id,SceneAssetKind::Skeletal,root/"Player.cskin"}, {"test/common",SceneAssetKind::Animation,root/"Common.canim"}};
    auto id = document.Add("Player");
    document.Entity(id).asset = asset.mesh.id; document.Entity(id).animation.library = "test/common";
    document.Entity(id).animation.clip = "Idle";
    const auto child = document.Add("Child", id); document.Entity(child).local.position = {0,2,0};
    document.play.player = document.play.cameraTarget = id;
    document.view.yaw = 0.35f; document.view.pitch = 0.55f; document.view.distance = 7;
    return document;
}
// 画素が背景一色のまま通過しないことを確認する。外観の良し悪しは別途目視する。
void CheckCapture(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    BITMAPFILEHEADER header{}; file.read(reinterpret_cast<char*>(&header), sizeof(header));
    file.seekg(header.bfOffBits);
    std::vector<unsigned char> pixels((std::istreambuf_iterator<char>(file)), {});
    size_t changed = 0;
    for (size_t i = 4; i + 3 < pixels.size(); i += 4)
        if (pixels[i] != pixels[0] || pixels[i+1] != pixels[1] || pixels[i+2] != pixels[2]) ++changed;
    Require(changed > 100, "Capture contains no visible geometry");
}
void Run(Renderer& renderer, SceneRuntime& scene, const std::filesystem::path& root, HWND window) {
    ScenePlayback demo; demo.Initialize(scene);
    const auto initial = scene.Document().Entity(1).local;
    const auto generation = scene.Generation(1);
    const auto mesh = scene.DrawItems().front().mesh;
    RenderSettings settings;
    settings.lighting = scene.Document().view.lighting;
    settings.clear = scene.Document().view.background;
    auto* player = scene.Animation(1);
    const auto frame = [&](const InputState& input, const char* capture = nullptr) {
        Pump(); demo.Update(scene, input, 1.0f/60); scene.Update(1.0f/60); demo.ConfigureView(scene, settings);
        renderer.BeginFrame(settings, scene.DrawItems()); renderer.BeginOverlay();
        renderer.EndFrame(false, capture != nullptr);
        if (capture) { renderer.SaveCapture(root/capture, false); CheckCapture(root/capture); }
        const auto& world = scene.World(1);
        Require(std::abs(world._41-demo.Motion().position[0]) < 0.0001f && std::abs(world._43-demo.Motion().position[2]) < 0.0001f, "Runtime transform not evaluated");
        Require(scene.Document().Entity(1).local == initial && !scene.Dirty(), "Simulation changed authoring state");
        Require(scene.Generation(1) == generation && scene.DrawItems().front().mesh == mesh && scene.Animation(1) == player, "Simulation rebuilt GPU asset/player");
    };
    InputState input;
    frame(input, "idle.bmp");
    Require(player->Clips()[player->playback.clip].name == "Idle", "Initial clip");
    input.down['W'] = true;
    const auto& joints = player->Asset().joints;
    const auto foot = std::find_if(joints.begin(), joints.end(), [](const auto& joint) { return joint.name == "Foot.L"; });
    XMFLOAT3 stanceStart{};
    // 配置時の向きを維持するため、開始直後は旋回する。次の周期で接地の滑りだけを測る。
    const auto walk = std::find_if(player->Clips().begin(), player->Clips().end(), [&](const auto& clip) { return clip.name == scene.Document().play.clips[1]; });
    const int cycleFrames = static_cast<int>(std::lround(walk->duration * 60));
    const int walkFrames = cycleFrames + 60;
    for (int i = 0; i < walkFrames; ++i) {
        frame(input, i == cycleFrames + 15 ? "walk.bmp" : nullptr);
        if (foot != joints.end() && (i == cycleFrames + 10 || i == cycleFrames + 20)) {
            const auto point = player->Socket(static_cast<size_t>(foot - joints.begin()), scene.World(1));
            if (i == cycleFrames + 10) stanceStart = point;
            else {
                const float slide = std::hypot(point.x-stanceStart.x, point.z-stanceStart.z);
                std::cout << "Walk stance foot drift (m): " << slide << '\n';
                Require(slide < 0.025f, "Humanoid walk speed does not match stance foot motion");
            }
        }
    }
    Require(player->Clips()[player->playback.clip].name == "Walk", "Walk clip");
    Require(std::abs(std::hypot(demo.Motion().position[0], demo.Motion().position[2])-ScenePlaySettings{}.walkSpeed * walkFrames / 60) < 0.001f, "Walk distance");
    const auto walkPalette = player->Palette();
    input.down[VK_SHIFT] = true;
    for (int i = 0; i < 60; ++i) {
        frame(input, i == 15 ? "run.bmp" : nullptr);
        if (foot != joints.end() && (i == 3 || i == 12)) {
            const auto point = player->Socket(static_cast<size_t>(foot-joints.begin()), scene.World(1));
            if (i == 3) stanceStart = point;
            else {
                const float slide = std::hypot(point.x-stanceStart.x, point.z-stanceStart.z);
                std::cout << "Run stance foot drift (m): " << slide << '\n';
                Require(slide < 0.025f, "Humanoid run speed does not match stance foot motion");
            }
        }
    }
    Require(player->Clips()[player->playback.clip].name == "Run", "Run clip");
    Require(std::abs(std::hypot(demo.Motion().position[0], demo.Motion().position[2])-
        (ScenePlaySettings{}.walkSpeed * walkFrames / 60 + ScenePlaySettings{}.runSpeed)) < 0.001f, "Run distance");
    frame(input);
    Require(std::memcmp(walkPalette.data(), player->Palette().data(), walkPalette.size()*sizeof(XMFLOAT4X4)) != 0, "Animation palette never changed");
    input = {}; frame(input, "stopped.bmp");
    Require(player->Clips()[player->playback.clip].name == "Idle", "Stop clip");
    const auto stopped = demo.Motion().position;
    input.down['E'] = true; input.down[VK_PRIOR] = true;
    for (int i = 0; i < 30; ++i) frame(input);
    Require(demo.Motion().position == stopped && settings.cameraYaw > 0.5f && settings.cameraDistance < 7, "Camera controls moved player or failed");
    renderer.Resize(800,600); frame({});
    ShowWindow(window, SW_MINIMIZE); Pump(); Require(IsIconic(window), "Minimize failed");
    ShowWindow(window, SW_RESTORE); Pump(); Require(!IsIconic(window), "Restore failed");
    renderer.Resize(960,720); frame({}, "restored.bmp");
    Require(std::abs(settings.cameraTarget[0]-stopped[0]) < 0.05f, "Camera did not follow");
    auto invalid = initial; invalid.position[0] = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false; try { scene.SetRuntimeTransform(1, invalid); } catch (...) { rejected = true; }
    Require(rejected, "Invalid runtime pose accepted");
    Require(scene.Save(root/"Saved.cscene"), "Scene save failed");
    Require(LoadScene(root/"Saved.cscene").Entity(1).local == initial, "Runtime pose leaked into save");
    Require(scene.ReloadAsset(scene.Document().Entity(1).asset), "Valid reload failed");
    scene.Update(0);
    Require(std::abs(scene.World(1)._43-stopped[2]) < 0.001f, "Reload reset runtime pose");
    const auto before = scene.World(1);
    Require(!scene.ReloadAsset(scene.Document().Entity(1).asset, root/"missing.cskin"), "Missing asset accepted");
    Require(scene.World(1)._43 == before._43, "Failed reload changed pose");
    Require(scene.Open(root/"Saved.cscene"), "Scene reopen failed");
    Require(scene.World(1)._43 == initial.position[2], "Open did not reset runtime pose");
    if (scene.Document().entities.size() == 2 && scene.Document().entities[1].name == "Child") {
        auto pose = initial; pose.position = {2,0,3};
        scene.SetRuntimeTransform(1, pose); scene.Update(0);
        Require(scene.World(2)._41 == 2 && scene.World(2)._42 == 2 && scene.World(2)._43 == 3, "Runtime parent pose did not reach child");
    }
    scene.Clear(); renderer.WaitIdle(); renderer.DrainDiagnostics();
    Require(renderer.DiagnosticErrors() == 0, "GPU validation error");
    std::cout << "PASS: idle/walk/run, distance, palette, camera, runtime ownership, reload, lifecycle, visible captures\n";
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 3 || argc == 4, "Expected output warp/hardware [scene]");
        const auto root = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(root);
        Log log(root/"player.log"); Window window; Renderer renderer(log);
        const bool warp = std::wstring_view(argv[2]) == L"warp";
        renderer.Initialize(window.handle,960,720,{warp,true,!warp});
        SceneRuntime scene(renderer,log);
        if (argc == 4) Require(scene.Open(argv[3]), scene.Status().c_str());
        else { Require(scene.Apply(Fixture(root)), "Fixture setup"); scene.MarkClean(); }
        Run(renderer,scene,root,window.handle);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
