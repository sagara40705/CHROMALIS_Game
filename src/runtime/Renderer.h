#pragma once
#include "runtime/Diagnostics.h"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

namespace chromalis {
using Microsoft::WRL::ComPtr;
struct RenderSettings {
    std::array<float, 4> clear{0.055f, 0.07f, 0.11f, 1.0f};
    float x = 0, y = 0, scale = 1;
    bool vsync = true;
};
struct RendererOptions { bool warp = false, debug = false, gpuValidation = false; };

class Renderer final {
public:
    static constexpr UINT FrameCount = 2;
    static constexpr DXGI_FORMAT OutputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    explicit Renderer(Log& log) : log_(log) {}
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void Initialize(HWND window, UINT width, UINT height, const RendererOptions& options);
    void Resize(UINT width, UINT height);
    ID3D12GraphicsCommandList* BeginFrame(const RenderSettings& settings);
    bool EndFrame(bool vsync, bool capture);
    bool TestVisible();
    void SaveCapture(const std::filesystem::path& path, bool verifyTriangle);
    void WaitIdle();
    void DrainDiagnostics();
    ID3D12Device* Device() const { return device_.Get(); }
    ID3D12CommandQueue* Queue() const { return queue_.Get(); }
    UINT64 CompletedFence() const;
    UINT64 RetireFence() const { return nextFence_; }
    const std::string& AdapterName() const { return adapterName_; }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }
    size_t DiagnosticErrors() const { return diagnosticErrors_; }
    double FenceWaitMs() const { return fenceWaitMs_; }
private:
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> constants;
        std::byte* mapped = nullptr;
        UINT64 fence = 0;
    };
    void CreateTargets();
    void CreatePipeline();
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
    std::array<float, 4> lastClear_{};
};
}
