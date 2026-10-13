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
        WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"ChromalisMaterialTest";
        CheckWin32(RegisterClassW(&type), "Register material test window");
        handle = CreateWindowW(type.lpszClassName, L"CHROMALIS Material Test", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, nullptr, nullptr, type.hInstance, nullptr);
        Require(handle != nullptr, "Create material test window");
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
    Require(file.good(), "Material capture read failed");
    for (size_t c = 0; c < 3; ++c) {
        if (std::abs(int(pixel[2 - c]) - Encode(expected[c])) > 4) {
            std::cerr << "pixel x=" << x << " channel=" << c << " actual=" << int(pixel[2 - c]) << " expected=" << Encode(expected[c]) << '\n';
            throw std::runtime_error("Material pixel mismatch");
        }
    }
}

void Test(Renderer& renderer, Log& log, const std::filesystem::path& root) {
    auto asset = Quad("material/probe", 2, 2, 0);
    asset.mesh.textures.push_back({1, 1, {128, 128, 128, 255}});
    asset.mesh.textures.push_back({1, 1, {230, 128, 204, 255}});
    asset.mesh.materials[0].texture = 1;
    SceneRuntime scene(renderer, log); SceneDocument document;
    document.view.yaw = document.view.pitch = 0; document.view.distance = 7;
    auto& lighting = document.view.lighting;
    lighting.direction = {0, 0, -1}; lighting.color = {1, 1, 1}; lighting.ambient = {0, 0, 0};
    const auto id = document.Add("Surface"); document.Entity(id).asset = asset.mesh.id;
    MaterialSurface surface; surface.shading = ShadingModel::Pbr; surface.metallic = 0; surface.roughness = 1;
    document.materials = {{"surface", surface}}; document.Entity(id).materials = {{0, "surface"}};
    const auto capture = root / "material.bmp";
    const auto draw = [&] {
        Require(scene.Apply(document), scene.Status().c_str());
        RenderSettings settings; settings.cameraYaw = settings.cameraPitch = 0; settings.cameraDistance = 7; settings.lighting = lighting;
        for (int frame = 0; frame < 3; ++frame) {
            scene.Update(0); renderer.BeginFrame(settings, scene.DrawItems()); renderer.BeginOverlay(); renderer.EndFrame(false, frame == 2);
        }
        renderer.SaveCapture(capture, false);
    };
    const double color = std::pow((128 / 255.0 + 0.055) / 1.055, 2.4), pi = 3.141592653589793;
    for (bool skin : {false, true}) {
        asset.mesh.materials[0].normalMap = {};
        SaveStaticAsset(asset.mesh, root / "probe.cmesh"); SaveSkeletalAsset(asset, root / "probe.cskin");
        document.assets = {{asset.mesh.id, skin ? SceneAssetKind::Skeletal : SceneAssetKind::Static, root / (skin ? "probe.cskin" : "probe.cmesh")}};
        document.materials[0].surface = surface;
        draw(); double expected = (color * 0.96 + 0.01) / pi;
        CheckPixel(capture, renderer, 0, {expected, expected, expected});
        document.materials[0].surface.metallic = 1;
        draw(); expected = color / (4 * pi); CheckPixel(capture, renderer, 0, {expected, expected, expected});
        document.materials[0].surface = surface; document.materials[0].surface.roughness = 0.5f;
        draw(); expected = color * 0.96 / pi + 0.16 / pi; CheckPixel(capture, renderer, 0, {expected, expected, expected});
        // 同じ画像を色とDataとして読み分け、MRのG/Bを取り違えない。
        asset.mesh.textures.push_back({1, 1, {0, 128, 0, 255}});
        asset.mesh.materials[0].metallicRoughnessMap.texture = static_cast<uint32_t>(asset.mesh.textures.size() - 1);
        document.materials[0].surface = surface;
        const auto publish = [&] {
            if (skin) SaveSkeletalAsset(asset, root / "probe.cskin"); else SaveStaticAsset(asset.mesh, root / "probe.cmesh");
            Require(scene.ReloadAsset(asset.mesh.id), "Material reload failed");
        };
        publish(); draw(); expected = color * 0.96 / pi + 0.01 / (pi * std::pow(128 / 255.0, 4));
        CheckPixel(capture, renderer, 0, {expected, expected, expected});
        asset.mesh.materials[0].metallicRoughnessMap = {};
        document.materials[0].surface.shading = ShadingModel::Lit;
        asset.mesh.materials[0].normalMap.texture = 2;
        publish(); draw();
        double nx = 230 / 127.5 - 1, ny = 128 / 127.5 - 1, nz = 204 / 127.5 - 1;
        expected = color * nz / std::sqrt(nx * nx + ny * ny + nz * nz);
        CheckPixel(capture, renderer, 0, {expected, expected, expected});
        // authored tangentでも同じ結果。Skinning経由の接線Bufferを含む。
        asset.mesh.tangents.assign(4, {1, 0, 0, 1}); publish(); draw();
        CheckPixel(capture, renderer, 0, {expected, expected, expected});
        document.materials[0].surface.normalScale = 0; draw(); CheckPixel(capture, renderer, 0, {color, color, color});
        asset.mesh.tangents.clear(); asset.mesh.materials[0].normalMap = {};
        publish();
        document.materials[0].surface = surface; document.materials[0].surface.metallic = 0;
        document.materials[0].surface.occlusionStrength = 1;
        lighting.color = {}; lighting.ambient = {1, 1, 1};
        asset.mesh.materials[0].occlusionMap.texture = 1; publish(); draw();
        expected = color * 128 / 255.0; CheckPixel(capture, renderer, 0, {expected, expected, expected});
        asset.mesh.materials[0].occlusionMap = {}; asset.mesh.materials[0].emissiveMap.texture = 1;
        lighting.ambient = {}; document.materials[0].surface.emission = {0.5f, 0.5f, 0.5f}; publish(); draw();
        CheckPixel(capture, renderer, 0, {color * 0.5, color * 0.5, color * 0.5});
        asset.mesh.materials[0].emissiveMap = {}; lighting.color = {1, 1, 1};
        // 半透明は線形合成し、個体の列挙順に依存しない。
        asset.mesh.materials[0].texture = 0; publish();
        MaterialSurface blue; blue.shading = ShadingModel::Unlit; blue.baseColor = {0, 0, 1, 1};
        auto red = blue; red.baseColor = {1, 0, 0, 0.5f}; red.alphaBlend = true;
        auto green = red; green.baseColor = {0, 1, 0, 0.5f};
        document.materials = {{"surface", blue}, {"red", red}, {"green", green}};
        document.Entity(id).local.position[2] = 1;
        const auto front = document.Add("Front"), back = document.Add("Back");
        document.Entity(front).asset = document.Entity(back).asset = asset.mesh.id;
        document.Entity(front).materials = {{0, "green"}}; document.Entity(front).local.position[2] = -1;
        document.Entity(back).materials = {{0, "red"}};
        draw(); CheckPixel(capture, renderer, 0, {0.25, 0.5, 0.25});
        document.Entity(front).local.position[2] = 2; draw(); CheckPixel(capture, renderer, 0, {0.5, 0, 0.5});
        document.materials[1].surface.baseColor[3] = 0; draw(); CheckPixel(capture, renderer, 0, {0, 0, 1});
        document.DeleteSubtree(front); document.DeleteSubtree(back); document.Entity(id).local.position[2] = 0;
        document.materials = {{"surface", surface}};
        asset.mesh.materials[0].texture = 1;
        scene.Clear();
    }
    // 大きいUV範囲で最小Mipを選ばせ、GPU側のMipコピーとsRGB平均を確認する。
    asset.mesh.textures[1] = {3, 1, {0,0,0,255, 255,255,255,255, 0,0,0,255}};
    for (auto& vertex : asset.mesh.vertices) { vertex.uv[0] *= 2048; vertex.uv[1] *= 2048; }
    SaveStaticAsset(asset.mesh, root / "probe.cmesh");
    document.assets = {{asset.mesh.id, SceneAssetKind::Static, root / "probe.cmesh"}};
    document.materials[0].surface.shading = ShadingModel::Unlit;
    draw(); CheckPixel(capture, renderer, 0, {1.0/3, 1.0/3, 1.0/3});
    scene.Clear();
    auto many = asset.mesh; many.materials.resize(1024);
    const auto handle = renderer.CreateMesh(many); renderer.ReleaseMesh(handle);
    renderer.Resize(800, 600); draw(); CheckPixel(capture, renderer, 0, {1.0/3, 1.0/3, 1.0/3});
    scene.Clear();
    renderer.WaitIdle(); renderer.DrainDiagnostics(); Require(renderer.DiagnosticErrors() == 0, "Material GPU validation errors");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 3, "Expected output directory and warp/hardware");
        const auto root = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(root);
        Log log(root / "material.log"); Window window; Renderer renderer(log);
        const bool warp = std::wstring_view(argv[2]) == L"warp";
        renderer.Initialize(window.handle, 960, 720, {warp, true, !warp});
        Test(renderer, log, root);
        std::cout << "PASS: PBR BRDF/maps, normal static/skin/tangent, AO, emission, alpha sorting/depth, reload, release\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
