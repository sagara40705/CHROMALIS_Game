#include "scene/SceneRuntime.h"
#include <cmath>
#include <fstream>
#include <iostream>

using namespace chromalis;
using namespace DirectX;
namespace {
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Window {
    HWND handle{};
    Window() {
        WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"ChromalisShadowTest";
        CheckWin32(RegisterClassW(&type), "Register shadow test window");
        handle = CreateWindowW(type.lpszClassName, L"CHROMALIS Shadow Test", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, nullptr, nullptr, type.hInstance, nullptr);
        Require(handle != nullptr, "Create shadow test window");
    }
    ~Window() { if (handle) DestroyWindow(handle); }
};
SkeletalAsset Quad(const std::string& id, float width, float height, float z) {
    SkeletalAsset result;
    auto& mesh = result.mesh; mesh.id = id;
    mesh.vertices = {{{-width, height, z}, {0, 0, -1}, {0, 0}}, {{width, height, z}, {0, 0, -1}, {1, 0}},
        {{width, -height, z}, {0, 0, -1}, {1, 1}}, {{-width, -height, z}, {0, 0, -1}, {0, 1}}};
    mesh.indices = {0, 1, 2, 0, 2, 3}; mesh.parts = {{0, 6, 0}}; mesh.materials.emplace_back();
    mesh.materials[0].nearest = 1; mesh.textures = {{1, 1, {255, 255, 255, 255}}};
    result.joints = {{"root", -1, {}}}; result.bindings.resize(1); result.skin.resize(4);
    XMStoreFloat4x4(&result.bindings[0].inverseBind, XMMatrixIdentity());
    result.clips = {{"move", 1, {{0, 0, {{0, {0, 0, 0, 0}}, {1, {0, 0, 0, 0}}}}}}};
    return result;
}
int Encode(double linear) { return static_cast<int>(std::lround(255 * (linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055))); }
void CheckPixel(const std::filesystem::path& path, Renderer& renderer, float x, const std::array<double, 3>& expected) {
    std::ifstream file(path, std::ios::binary); BITMAPFILEHEADER header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    const float focal = renderer.Height() * 0.5f / std::tan(XM_PIDIV4 * 0.5f);
    const auto px = static_cast<int>(renderer.Width() * 0.5f + focal * x / 7);
    file.seekg(header.bfOffBits + (static_cast<std::streamoff>(renderer.Height() / 2) * renderer.Width() + px) * 4);
    unsigned char pixel[4]{}; file.read(reinterpret_cast<char*>(pixel), 4);
    Require(file.good(), "Shadow capture read failed");
    for (size_t c = 0; c < 3; ++c) {
        if (std::abs(int(pixel[2 - c]) - Encode(expected[c])) > 4) {
            std::cerr << "pixel x=" << x << " channel=" << c << " actual=" << int(pixel[2 - c]) << " expected=" << Encode(expected[c]) << '\n';
            throw std::runtime_error("Shadow pixel mismatch");
        }
    }
}
void Test(Renderer& renderer, Log& log, const std::filesystem::path& root) {
    auto receiver = Quad("shadow/receiver", 3, 2, 1);
    auto caster = Quad("shadow/caster", 0.35f, 0.35f, 0);
    caster.clips[0].channels[0].keys[1].value[0] = 2;
    SaveStaticAsset(receiver.mesh, root / "receiver.cmesh"); SaveSkeletalAsset(receiver, root / "receiver.cskin");
    SaveStaticAsset(caster.mesh, root / "caster.cmesh"); SaveSkeletalAsset(caster, root / "caster.cskin");
    SceneRuntime scene(renderer, log); SceneDocument document;
    document.view.yaw = document.view.pitch = 0; document.view.distance = 6;
    auto& lighting = document.view.lighting;
    lighting.direction = {-1, 0, -1}; lighting.color = {0.8f, 0.8f, 0.8f}; lighting.ambient = {0.1f, 0.1f, 0.1f};
    lighting.shadow.enabled = true; lighting.shadow.resolution = 512; lighting.shadow.extent = 10;
    lighting.shadow.depth = 20; lighting.shadow.bias = 0.003f; lighting.shadow.normalBias = 0;
    MaterialSurface surface; surface.baseColor = {0.5f, 0.5f, 0.5f, 1};
    document.materials = {{"receiver", surface}, {"caster", {}}};
    const auto floor = document.Add("Receiver"), occluder = document.Add("Caster");
    document.Entity(floor).asset = receiver.mesh.id; document.Entity(floor).materials = {{0, "receiver"}};
    document.Entity(occluder).asset = caster.mesh.id; document.Entity(occluder).materials = {{0, "caster"}};
    document.Entity(occluder).local.position = {-1, 0, 0}; document.Entity(occluder).animation.autoplay = false;
    const auto capture = root / "shadow.bmp";
    const auto draw = [&] {
        Require(scene.Apply(document), scene.Status().c_str());
        RenderSettings settings; settings.cameraYaw = settings.cameraPitch = 0; settings.cameraDistance = 6; settings.lighting = lighting;
        for (int frame = 0; frame < 4; ++frame) {
            scene.Update(0);
            renderer.BeginFrame(settings, scene.DrawItems()); renderer.BeginOverlay(); renderer.EndFrame(false, frame == 3);
        }
        renderer.SaveCapture(capture, false);
    };
    const double lit = 0.5 * (0.1 + 0.8 / std::sqrt(2.0));
    for (bool skinned : {false, true}) {
        const auto kind = skinned ? SceneAssetKind::Skeletal : SceneAssetKind::Static;
        document.assets = {{receiver.mesh.id, kind, root / (skinned ? "receiver.cskin" : "receiver.cmesh")},
            {caster.mesh.id, kind, root / (skinned ? "caster.cskin" : "caster.cmesh")}};
        draw(); CheckPixel(capture, renderer, 0, {0.05, 0.05, 0.05}); CheckPixel(capture, renderer, 1, {lit, lit, lit});
        // 色付きの投影影は直接光だけに作用し、環境光は残る。
        lighting.shadow.tint = {0.2f, 0.4f, 0.6f}; lighting.shadow.strength = 0.5f;
        draw(); CheckPixel(capture, renderer, 0, {0.05 + (lit - 0.05) * 0.6, 0.05 + (lit - 0.05) * 0.7, 0.05 + (lit - 0.05) * 0.8});
        lighting.shadow.tint = {}; lighting.shadow.strength = 1;
        document.materials[0].surface.receiveShadow = false;
        draw(); CheckPixel(capture, renderer, 0, {lit, lit, lit});
        document.materials[0].surface.receiveShadow = true; document.materials[1].surface.castShadow = false;
        draw(); CheckPixel(capture, renderer, 0, {lit, lit, lit});
        document.materials[1].surface.castShadow = true;
        document.materials[0].surface.shading = ShadingModel::Toon;
        document.materials[0].surface.emission = {0.02f, 0.03f, 0.04f};
        draw(); CheckPixel(capture, renderer, 0, {0.07, 0.08, 0.09}); CheckPixel(capture, renderer, 1, {0.47, 0.48, 0.49});
        document.materials[0].surface = surface;
        document.materials[0].surface.shading = ShadingModel::Unlit;
        draw(); CheckPixel(capture, renderer, 0, {0.5, 0.5, 0.5});
        document.materials[0].surface = surface;
        if (skinned) document.Entity(occluder).animation.time = 1;
        else document.Entity(occluder).local.position[0] = 1;
        draw(); CheckPixel(capture, renderer, 0, {lit, lit, lit}); CheckPixel(capture, renderer, 2, {0.05, 0.05, 0.05});
        document.Entity(occluder).animation.time = 0; document.Entity(occluder).local.position[0] = -1;
    }
    // MASKの透明半分が影にも出る。更新後のSampler/Texture経路を検証する。
    caster.mesh.textures = {{2, 1, {255, 255, 255, 255, 255, 255, 255, 0}}};
    SaveSkeletalAsset(caster, root / "caster.cskin");
    Require(scene.ReloadAsset(caster.mesh.id), "Shadow caster reload failed");
    document.materials[1].surface.alphaMask = true;
    draw(); CheckPixel(capture, renderer, -0.2f, {0.05, 0.05, 0.05}); CheckPixel(capture, renderer, 0.2f, {lit, lit, lit});
    document.materials[1].surface.cutoff = 2;
    draw(); CheckPixel(capture, renderer, -0.2f, {lit, lit, lit});
    document.materials[1].surface.alphaMask = false;
    for (uint32_t resolution : {256u, 1024u, 512u}) {
        lighting.shadow.resolution = resolution; draw(); CheckPixel(capture, renderer, 0, {0.05, 0.05, 0.05});
    }
    renderer.Resize(800, 600); draw(); CheckPixel(capture, renderer, 0, {0.05, 0.05, 0.05});
    for (float radius : {0.0f, 2.0f}) {
        lighting.shadow.filterRadius = radius; draw(); CheckPixel(capture, renderer, 0, {0.05, 0.05, 0.05});
    }
    lighting.shadow.center = {100, 0, 0}; draw(); CheckPixel(capture, renderer, 0, {lit, lit, lit});
    lighting.shadow.center = {}; lighting.direction = {0, 1, 0};
    draw(); CheckPixel(capture, renderer, 0, {0.05, 0.05, 0.05});
    lighting.direction = {-1, 0, -1};
    lighting.shadow.enabled = false; draw(); CheckPixel(capture, renderer, 0, {lit, lit, lit});
    scene.Clear(); renderer.WaitIdle(); renderer.DrainDiagnostics(); Require(renderer.DiagnosticErrors() == 0, "D3D12 shadow validation errors");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 3, "Expected output directory and warp/hardware");
        const auto root = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(root);
        Log log(root / "shadow.log"); Window window; Renderer renderer(log);
        const bool warp = std::wstring_view(argv[2]) == L"warp";
        renderer.Initialize(window.handle, 960, 720, {warp, true, !warp});
        Test(renderer, log, root);
        std::cout << "PASS: static/skinned caster and receiver, animation pose, tint/strength, Toon/emission, MASK, resize/resolution, release\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
