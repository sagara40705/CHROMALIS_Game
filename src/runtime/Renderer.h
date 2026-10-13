#pragma once
#include "runtime/Diagnostics.h"
#include "assets/StaticAsset.h"
#include "animation/AnimationPlayer.h"
#include "runtime/VfxPlayer.h"
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
struct ModelPose {
    std::array<float, 3> position{0,0,0};
    float yaw = 0, scale = 1;
};
struct RenderSettings {
    std::array<float, 4> clear{0.055f, 0.07f, 0.11f, 1.0f};
    float x = 0, y = 0, scale = 1;
    bool vsync = true;
    bool showModels = true, unlit = false;
    float cameraYaw = 0.35f, cameraPitch = 0.25f, cameraDistance = 7;
    std::array<ModelPose, 2> models{ModelPose{{-1.25f,0,0},0,0.7f}, ModelPose{{1.25f,0,0.6f},0.5f,0.7f}};
    ModelPose animated{{0,-1,0},0,1};
    VfxSettings vfx;
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
    ID3D12GraphicsCommandList* BeginFrame(const RenderSettings& settings);
    bool EndFrame(bool vsync, bool capture);
    bool TestVisible();
    void SaveCapture(const std::filesystem::path& path, bool verifyTriangle, AssetCaptureCheck assetCheck = AssetCaptureCheck::None);
    void WaitIdle();
    void ReleasePreviewAssets();
    void DrainDiagnostics();
    bool ReloadModel(size_t slot, const std::filesystem::path& path);
    bool ReloadAnimation(const std::filesystem::path& path);
    AnimationPlayer* Animation() { return animation_.get(); }
    bool ReloadVfx(const std::filesystem::path& path);
    VfxPlayer* Vfx() { return vfx_.get(); }
    void UpdatePresentation(float seconds,const RenderSettings& settings);
    const std::string& AssetStatus() const { return assetStatus_; }
    uint64_t ModelGeneration(size_t slot) const { return modelGenerations_.at(slot); }
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
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> constants;
        std::byte* mapped = nullptr;
        ComPtr<ID3D12Resource> bones;
        std::byte* bonesMapped = nullptr;
        ComPtr<ID3D12Resource> timestamps;
        UINT64 fence = 0;
    };
    void CreateTargets();
    void CreatePipeline();
    void CreateMeshPipeline();
    void CreateDepth();
    void DrawModels(const RenderSettings& settings);
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
        ComPtr<ID3D12Resource> vertex, index;
        ComPtr<ID3D12Resource> skin;
        D3D12_VERTEX_BUFFER_VIEW skinView{};
        std::vector<ComPtr<ID3D12Resource>> textures;
        ComPtr<ID3D12DescriptorHeap> srv, samplers;
        D3D12_VERTEX_BUFFER_VIEW vb{};
        D3D12_INDEX_BUFFER_VIEW ib{};
        std::vector<MeshPart> parts;
        std::vector<MeshMaterial> materials;
    };
    std::array<std::unique_ptr<GpuModel>, 2> models_;
    std::unique_ptr<GpuModel> animatedModel_;
    std::unique_ptr<AnimationPlayer> animation_;
    std::unique_ptr<VfxPlayer> vfx_;
    std::unique_ptr<GpuModel> UploadModel(const StaticAsset& asset, const std::vector<SkinVertex>* skin, bool& submitted);
    std::array<uint64_t, 2> modelGenerations_{};
    std::string assetStatus_ = "No cooked models loaded";
    ComPtr<ID3D12Resource> depth_;
    ComPtr<ID3D12DescriptorHeap> dsv_;
    ComPtr<ID3D12RootSignature> meshRoot_;
    std::array<ComPtr<ID3D12PipelineState>, 2> meshPso_;
    std::array<ComPtr<ID3D12PipelineState>, 2> skinPso_;
};
}
