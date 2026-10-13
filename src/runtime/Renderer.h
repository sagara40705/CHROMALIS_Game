#pragma once
#include "runtime/Diagnostics.h"
#include "assets/StaticAsset.h"
#include "assets/SkeletalAsset.h"
#include "assets/Material.h"
#include <DirectXMath.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <memory>

namespace chromalis {
using Microsoft::WRL::ComPtr;
struct MeshHandle {
    uint32_t slot = UINT32_MAX, generation = 0;
    bool operator==(const MeshHandle&) const = default;
};
struct DrawItem {
    MeshHandle mesh;
    DirectX::XMFLOAT4X4 world;
    std::span<const DirectX::XMFLOAT4X4> bones, normals;
    std::span<const MaterialSurface> materials;
};
struct RenderSettings {
    SceneLighting lighting;
    std::array<float, 4> clear{0.055f, 0.07f, 0.11f, 1.0f};
    float x = 0, y = 0, scale = 1;
    bool vsync = true;
    bool showModels = true, unlit = false;
    bool diagnosticTriangle = false;
    float cameraYaw = 0.35f, cameraPitch = 0.25f, cameraDistance = 7;
    std::array<float, 3> cameraTarget{0, 0, 0};
};
struct RendererOptions { bool warp = false, debug = false, gpuValidation = false; };
enum class AssetCaptureCheck { None, Corners, UpdatedCorners, Midtone, VfxAlpha };

class Renderer final {
public:
    static constexpr UINT FrameCount = 2;
    static constexpr DXGI_FORMAT OutputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT SceneFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    explicit Renderer(Log& log) : log_(log) {}
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void Initialize(HWND window, UINT width, UINT height, const RendererOptions& options);
    void Resize(UINT width, UINT height);
    void SetSceneSurface(UINT width, UINT height, D3D12_CPU_DESCRIPTOR_HANDLE srv,
        const std::array<float, 4>& clear = RenderSettings{}.clear);
    void DisableSceneSurface();
    ID3D12GraphicsCommandList* BeginFrame(const RenderSettings& settings, std::span<const DrawItem> items, bool triangle = false);
    void BeginOverlay();
    bool EndFrame(bool vsync, bool capture);
    bool TestVisible();
    void SaveCapture(const std::filesystem::path& path, bool verifyTriangle, AssetCaptureCheck assetCheck = AssetCaptureCheck::None);
    void WaitIdle();
    void DrainDiagnostics();
    MeshHandle CreateMesh(const StaticAsset& asset, const std::vector<SkinVertex>* skin = nullptr);
    void ReleaseMesh(MeshHandle mesh);
    std::span<const MeshMaterial> MeshMaterials(MeshHandle mesh) const;
    size_t MaterialUseCount(MeshHandle mesh, uint32_t slot) const;
    UINT CurrentFrame() const { return frameIndex_; }
    const DirectX::XMFLOAT4X4& View() const { return view_; }
    const DirectX::XMFLOAT4X4& Projection() const { return projection_; }
    ID3D12Device* Device() const { return device_.Get(); }
    ID3D12CommandQueue* Queue() const { return queue_.Get(); }
    UINT64 CompletedFence() const;
    UINT64 RetireFence() const { return nextFence_; }
    const std::string& AdapterName() const { return adapterName_; }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }
    size_t DiagnosticErrors() const { return diagnosticErrors_; }
    double FenceWaitMs() const { return fenceWaitMs_; }
    double GpuFrameMs() const { return gpuFrameMs_; }
private:
    struct GpuModel;
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> constants;
        std::byte* mapped = nullptr;
        ComPtr<ID3D12Resource> bones;
        std::byte* bonesMapped = nullptr;
        UINT64 boneCapacity = 256 * 128;
        ComPtr<ID3D12Resource> materialConstants;
        std::byte* materialsMapped = nullptr;
        UINT64 materialCapacity = 0;
        ComPtr<ID3D12Resource> timestamps;
        UINT64 fence = 0;
    };
    void CreateTargets();
    void CreatePipeline();
    void CreateMeshPipeline();
    void CreateDepth();
    void DrawModels(const RenderSettings& settings, std::span<const DrawItem> items, bool shadowPass = false);
    void CreateShadowTargets(UINT resolution);
    void WriteShadowDescriptor(GpuModel& model);
    void DrawShadow(const RenderSettings& settings, std::span<const DrawItem> items);
    void PrepareBoneUpload(std::span<const DrawItem> items);
    void PrepareMaterialUpload(std::span<const DrawItem> items);
    void WaitFor(UINT64 value);
    void Transition(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    void PrepareCapture();
    void CheckDevice(HRESULT result, std::string_view operation);
    Log& log_;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    std::array<ComPtr<ID3D12Resource>, FrameCount> targets_;
    std::array<Frame, FrameCount> frames_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pipeline_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    ComPtr<ID3D12Resource> readback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT captureLayout_{};
    UINT64 captureBytes_ = 0;
    UINT64 nextFence_ = 1;
    UINT width_ = 0, height_ = 0, frameIndex_ = 0, rtvStride_ = 0;
    std::string adapterName_;
    size_t diagnosticErrors_ = 0;
    double fenceWaitMs_ = 0;
    double gpuFrameMs_ = 0;
    UINT64 timestampFrequency_=0;
    ComPtr<ID3D12QueryHeap> timestampHeap_;
    std::array<float, 4> lastClear_{};
    struct GpuModel {
        std::string id;
        ComPtr<ID3D12Resource> vertex, index, tangent;
        D3D12_VERTEX_BUFFER_VIEW tangentView{};
        bool hasTangents = false;
        ComPtr<ID3D12Resource> skin;
        D3D12_VERTEX_BUFFER_VIEW skinView{};
        std::vector<ComPtr<ID3D12Resource>> textures;
        ComPtr<ID3D12DescriptorHeap> srv, samplers;
        D3D12_VERTEX_BUFFER_VIEW vb{};
        D3D12_INDEX_BUFFER_VIEW ib{};
        std::vector<MeshPart> parts;
        std::vector<DirectX::XMFLOAT3> partCenters;
        std::vector<MeshMaterial> materials;
    };
    struct MeshEntry { std::unique_ptr<GpuModel> model; uint32_t generation = 1; };
    std::vector<MeshEntry> meshes_;
    DirectX::XMFLOAT4X4 view_{}, projection_{};
    std::unique_ptr<GpuModel> UploadModel(const StaticAsset& asset, const std::vector<SkinVertex>* skin);
    ComPtr<ID3D12Resource> depth_;
    ComPtr<ID3D12Resource> sceneColor_, sceneDepth_;
    ComPtr<ID3D12DescriptorHeap> sceneRtv_, sceneDsv_;
    UINT sceneWidth_ = 0, sceneHeight_ = 0;
    std::array<float, 4> sceneClear_{};
    D3D12_CPU_DESCRIPTOR_HANDLE sceneSrv_{};
    bool sceneSurfaceRecording_ = false;
    ComPtr<ID3D12DescriptorHeap> dsv_;
    ComPtr<ID3D12RootSignature> meshRoot_;
    std::array<ComPtr<ID3D12PipelineState>, 4> meshPso_;
    std::array<ComPtr<ID3D12PipelineState>, 4> skinPso_;
    std::array<ComPtr<ID3D12PipelineState>, 2> shadowMeshPso_, shadowSkinPso_;
    ComPtr<ID3D12Resource> shadowDepth_;
    ComPtr<ID3D12DescriptorHeap> shadowDsv_;
    UINT shadowResolution_ = 0;
    DirectX::XMFLOAT4X4 lightViewProjection_{};
};
}
