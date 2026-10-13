#include "runtime/Renderer.h"
#include "Triangle_vs.h"
#include "Triangle_ps.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>
#include <vector>

namespace chromalis {
namespace {
D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{};
    h.Type = type; h.CreationNodeMask = 1; h.VisibleNodeMask = 1;
    return h;
}
}

Renderer::~Renderer() {
    // Also covers exceptions during partial initialization. No exceptions cross teardown.
    try { WaitIdle(); } catch (const std::exception& e) { log_.Write(e.what()); }
    for (auto& frame : frames_) if (frame.mapped) frame.constants->Unmap(0, nullptr);
    for (auto& frame : frames_) if (frame.bonesMapped) frame.bones->Unmap(0,nullptr);
    for (auto& frame : frames_) if (frame.materialsMapped) frame.materialConstants->Unmap(0, nullptr);
    if (fenceEvent_) CloseHandle(fenceEvent_);
}
void Renderer::Initialize(HWND window, UINT width, UINT height, const RendererOptions& options) {
    UINT factoryFlags = 0;
    if (options.debug) {
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),
            "D3D12 Debug Layer unavailable. Install Windows Graphics Tools or explicitly use --no-debug");
        debug->EnableDebugLayer();
        if (options.gpuValidation) {
            ComPtr<ID3D12Debug1> debug1;
            Check(debug.As(&debug1), "Query GPU-based validation");
            debug1->SetEnableGPUBasedValidation(TRUE);
        }
        factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        log_.Write(options.gpuValidation ? "Debug Layer + GPU validation enabled" : "Debug Layer enabled");
    } else log_.Write("Debug Layer disabled (not a validation run)");
    Check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");
    ComPtr<IDXGIAdapter1> chosen;
    if (options.warp) {
        Check(factory_->EnumWarpAdapter(IID_PPV_ARGS(&chosen)), "EnumWarpAdapter");
        Check(D3D12CreateDevice(chosen.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)), "Create WARP D3D12 device");
    } else {
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT hr = factory_->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            Check(hr, "Enumerate adapter");
            DXGI_ADAPTER_DESC1 desc{};
            Check(adapter->GetDesc1(&desc), "Get adapter description");
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            ComPtr<ID3D12Device> candidate;
            hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&candidate));
            if (FAILED(hr)) {
                log_.Write("Rejected adapter " + Utf8(desc.Description) + ": " + HresultText(hr));
                continue;
            }
            D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
            hr = candidate->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
            if (FAILED(hr) || model.HighestShaderModel < D3D_SHADER_MODEL_6_0) {
                log_.Write("Rejected adapter " + Utf8(desc.Description) + ": Shader Model 6.0 required");
                continue;
            }
            chosen = adapter; device_ = candidate; break;
        }
        if (!device_) throw std::runtime_error("No hardware adapter supports D3D12 FL11_0 + Shader Model 6.0. Update drivers; --warp is an explicit correctness-test option.");
    }
    D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
    Check(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)), "Query Shader Model 6.0");
    if (model.HighestShaderModel < D3D_SHADER_MODEL_6_0) throw std::runtime_error("Shader Model 6.0 unavailable");
    DXGI_ADAPTER_DESC1 desc{};
    Check(chosen->GetDesc1(&desc), "Get chosen adapter description");
    adapterName_ = Utf8(desc.Description);
    log_.Write(std::format("Adapter: {} | explicit WARP={} | FL11_0 / SM6.0 | SDK 10.0.26100.0", adapterName_, options.warp));
    if (options.debug) {
        Check(device_.As(&infoQueue_), "Query D3D12InfoQueue");
        if (IsDebuggerPresent()) {
            Check(infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE), "Break on corruption");
            Check(infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE), "Break on error");
        }
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)), "Create graphics queue");
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "Create fence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CheckWin32(fenceEvent_ != nullptr, "Create fence event");
    width_ = width; height_ = height;
    DXGI_SWAP_CHAIN_DESC1 swap{};
    swap.Width = width; swap.Height = height; swap.Format = OutputFormat;
    swap.SampleDesc.Count = 1; swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.BufferCount = FrameCount; swap.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> initialSwap;
    Check(factory_->CreateSwapChainForHwnd(queue_.Get(), window, &swap, nullptr, nullptr, &initialSwap), "Create swap chain");
    Check(initialSwap.As(&swapChain_), "Query swap chain 3");
    Check(factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "Disable exclusive Alt+Enter");
    D3D12_DESCRIPTOR_HEAP_DESC rtv{};
    rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtv.NumDescriptors = FrameCount*2;
    Check(device_->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&rtvHeap_)), "Create RTV heap");
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(rtv.Type);
    CreateTargets();
    D3D12_QUERY_HEAP_DESC queries{};queries.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;queries.Count=FrameCount*2;
    Check(device_->CreateQueryHeap(&queries,IID_PPV_ARGS(&timestampHeap_)),"Create GPU timestamps");
    Check(queue_->GetTimestampFrequency(&timestampFrequency_),"Get GPU timestamp frequency");
    for (auto& frame : frames_) {
        Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)), "Create command allocator");
        const auto heap = Heap(D3D12_HEAP_TYPE_UPLOAD);
        const auto buffer = BufferDesc(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.constants)), "Create per-frame upload constants");
        D3D12_RANGE noRead{0, 0};
        Check(frame.constants->Map(0, &noRead, reinterpret_cast<void**>(&frame.mapped)), "Map upload constants");
        const auto bones=BufferDesc(256*128);
        Check(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bones,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&frame.bones)),"Create frame bone palette");
        Check(frame.bones->Map(0,&noRead,reinterpret_cast<void**>(&frame.bonesMapped)),"Map frame bone palette");
        const auto readHeap=Heap(D3D12_HEAP_TYPE_READBACK);
        const auto timestamps=BufferDesc(16);
        Check(device_->CreateCommittedResource(&readHeap,D3D12_HEAP_FLAG_NONE,&timestamps,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&frame.timestamps)),"Create timestamp readback");
    }
    Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&list_)), "Create command list");
    Check(list_->Close(), "Close initial command list");
    CreatePipeline();
    CreateMeshPipeline();
    CreateDepth();
    DrainDiagnostics();
}
void Renderer::CreateTargets() {
    auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FrameCount; ++i) {
        Check(swapChain_->GetBuffer(i, IID_PPV_ARGS(&targets_[i])), "Get swap-chain buffer");
        const auto name = std::format(L"BackBuffer {}", i);
        Check(targets_[i]->SetName(name.c_str()), "Name back buffer");
        device_->CreateRenderTargetView(targets_[i].Get(), nullptr, handle);
        auto scene=handle;scene.ptr+=FrameCount*rtvStride_;
        D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=SceneFormat;view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        device_->CreateRenderTargetView(targets_[i].Get(),&view,scene);
        handle.ptr += rtvStride_;
    }
}
void Renderer::CreatePipeline() {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 1; rootDesc.pParameters = &parameter;
    ComPtr<ID3DBlob> blob, errors;
    const auto hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (errors) log_.Write(std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
    Check(hr, "Serialize root signature");
    Check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)), "Create root signature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_.Get();
    pso.VS = {Triangle_vs, sizeof(Triangle_vs)}; pso.PS = {Triangle_ps, sizeof(Triangle_ps)};
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1; pso.RTVFormats[0] = OutputFormat; pso.SampleDesc.Count = 1;
    Check(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline_)), "Create triangle PSO");
}
void Renderer::CheckDevice(HRESULT result, std::string_view operation) {
    if (FAILED(result)) {
        log_.Write(std::string(operation) + ": " + HresultText(result));
        log_.Write("Device removed reason: " + HresultText(device_->GetDeviceRemovedReason()));
        DrainDiagnostics();
        throw GpuFailure(std::string(operation) + ": " + HresultText(result));
    }
}
UINT64 Renderer::CompletedFence() const {
    const auto value = fence_->GetCompletedValue();
    if (value == std::numeric_limits<UINT64>::max())
        throw GpuFailure("GPU device removed while reading fence: " + HresultText(device_->GetDeviceRemovedReason()));
    return value;
}
void Renderer::WaitFor(UINT64 value) {
    if (CompletedFence() >= value) return;
    CheckDevice(fence_->SetEventOnCompletion(value, fenceEvent_), "Set fence completion event");
    const auto result = WaitForSingleObject(fenceEvent_, 10000);
    if (result == WAIT_FAILED) throw GpuFailure("Wait for GPU fence: " + HresultText(HRESULT_FROM_WIN32(GetLastError())));
    if (result != WAIT_OBJECT_0) throw GpuFailure("GPU fence wait exceeded 10 seconds; no automatic recovery. Device reason: " + HresultText(device_->GetDeviceRemovedReason()));
    if (CompletedFence() < value) throw GpuFailure("Fence signaled without reaching requested value");
}
void Renderer::WaitIdle() {
    if (!queue_ || !fence_ || !fenceEvent_) return;
    const auto value = nextFence_++;
    CheckDevice(queue_->Signal(fence_.Get(), value), "Signal idle fence");
    WaitFor(value);
    DrainDiagnostics();
}
void Renderer::Resize(UINT width, UINT height) {
    if (width == width_ && height == height_) return;
    // Called only for nonzero client extents, between frames.
    WaitIdle();
    for (auto& target : targets_) target.Reset();
    depth_.Reset();
    readback_.Reset();
    CheckDevice(swapChain_->ResizeBuffers(FrameCount, width, height, OutputFormat, 0), "Resize swap-chain buffers");
    width_ = width; height_ = height;
    CreateTargets();
    log_.Write(std::format("Resize completed: {}x{} (GPU idle before release)", width, height));
    CreateDepth();
}
void Renderer::Transition(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = targets_[frameIndex_].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before; barrier.Transition.StateAfter = after;
    list_->ResourceBarrier(1, &barrier);
}
ID3D12GraphicsCommandList* Renderer::BeginFrame(const RenderSettings& settings, std::span<const DrawItem> items, bool triangle) {
    sceneSurfaceRecording_ = sceneColor_ && !triangle;
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    auto& frame = frames_[frameIndex_];
    const auto start = std::chrono::steady_clock::now();
    WaitFor(frame.fence);
    PrepareBoneUpload(items);
    ValidateLighting(settings.lighting);
    if (settings.lighting.shadow.enabled) CreateShadowTargets(settings.lighting.shadow.resolution);
    PrepareMaterialUpload(items);
    if(frame.fence) {
        UINT64* times=nullptr;D3D12_RANGE range{0,16};Check(frame.timestamps->Map(0,&range,reinterpret_cast<void**>(&times)),"Read completed GPU timestamps");
        gpuFrameMs_=static_cast<double>(times[1]-times[0])*1000.0/static_cast<double>(timestampFrequency_);D3D12_RANGE noWrite{0,0};frame.timestamps->Unmap(0,&noWrite);
    }
    fenceWaitMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CheckDevice(frame.allocator->Reset(), "Reset frame allocator after its fence");
    CheckDevice(list_->Reset(frame.allocator.Get(), pipeline_.Get()), "Reset command list");
    list_->EndQuery(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frameIndex_*2);
    if (!triangle && settings.showModels && settings.lighting.shadow.enabled) DrawShadow(settings, items);
    const std::array<float, 4> constants{settings.x, settings.y, static_cast<float>(width_) / static_cast<float>(height_), settings.scale};
    std::memcpy(frame.mapped, constants.data(), sizeof(constants));
    lastClear_ = settings.clear;
    Transition(D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameIndex_) * rtvStride_;
    list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list_->ClearRenderTargetView(rtv, settings.clear.data(), 0, nullptr);
    const auto drawWidth = sceneColor_ && !triangle ? sceneWidth_ : width_;
    const auto drawHeight = sceneColor_ && !triangle ? sceneHeight_ : height_;
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(drawWidth), static_cast<float>(drawHeight), 0, 1};
    const D3D12_RECT rect{0, 0, static_cast<LONG>(drawWidth), static_cast<LONG>(drawHeight)};
    list_->RSSetViewports(1, &viewport); list_->RSSetScissorRects(1, &rect);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    using namespace DirectX;
    const float cp = std::cos(settings.cameraPitch), sp = std::sin(settings.cameraPitch);
    const auto target = XMVectorSet(settings.cameraTarget[0], settings.cameraTarget[1], settings.cameraTarget[2], 0);
    const auto eye = target + XMVectorSet(std::sin(settings.cameraYaw) * cp * settings.cameraDistance,
        sp * settings.cameraDistance, -std::cos(settings.cameraYaw) * cp * settings.cameraDistance, 1);
    XMStoreFloat4x4(&view_, XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0)));
    XMStoreFloat4x4(&projection_, XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(drawWidth) / drawHeight, 0.05f, 500));
    if (triangle) {
        list_->SetGraphicsRootSignature(root_.Get());
        list_->SetGraphicsRootConstantBufferView(0, frame.constants->GetGPUVirtualAddress());
        list_->DrawInstanced(3, 1, 0, 0);
    } else {
        auto scene = rtv;
        scene.ptr += FrameCount * rtvStride_;
        auto dsv = dsv_->GetCPUDescriptorHandleForHeapStart();
        if (sceneColor_) {
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = {sceneColor_.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
            list_->ResourceBarrier(1, &barrier);
            scene = sceneRtv_->GetCPUDescriptorHandleForHeapStart(); dsv = sceneDsv_->GetCPUDescriptorHandleForHeapStart();
            const auto decode = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
            const float clear[]{decode(settings.clear[0]), decode(settings.clear[1]), decode(settings.clear[2]), settings.clear[3]};
            list_->ClearRenderTargetView(scene, clear, 0, nullptr);
        }
        list_->OMSetRenderTargets(1, &scene, FALSE, &dsv);
        list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        if (settings.showModels) DrawModels(settings, items);
    }
    return list_.Get();
}
void Renderer::BeginOverlay() {
    if (sceneSurfaceRecording_) {
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {sceneColor_.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        list_->ResourceBarrier(1, &barrier);
    }
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    const D3D12_RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    list_->RSSetViewports(1, &viewport); list_->RSSetScissorRects(1, &rect);
    // Editorは表示色を直接出力するため、VFXの後でUNORM Viewへ戻す。
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += frameIndex_ * rtvStride_;
    list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}
void Renderer::PrepareCapture() {
    const auto desc = targets_[frameIndex_]->GetDesc();
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &captureLayout_, nullptr, nullptr, &captureBytes_);
    const auto heap = Heap(D3D12_HEAP_TYPE_READBACK);
    const auto buffer = BufferDesc(captureBytes_);
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)), "Create capture readback");
    Transition(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback_.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = captureLayout_;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = targets_[frameIndex_].Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
}
bool Renderer::EndFrame(bool vsync, bool capture) {
    list_->EndQuery(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frameIndex_*2+1);
    list_->ResolveQueryData(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frameIndex_*2,2,frames_[frameIndex_].timestamps.Get(),0);
    if (capture) PrepareCapture();
    else Transition(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    CheckDevice(list_->Close(), "Close frame command list");
    ID3D12CommandList* lists[]{list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const auto present = swapChain_->Present(vsync ? 1 : 0, 0);
    CheckDevice(present, "Present");
    const auto signal = nextFence_++;
    CheckDevice(queue_->Signal(fence_.Get(), signal), "Signal frame fence");
    frames_[frameIndex_].fence = signal;
    DrainDiagnostics();
    return present != DXGI_STATUS_OCCLUDED;
}
bool Renderer::TestVisible() {
    const auto hr = swapChain_->Present(0, DXGI_PRESENT_TEST);
    CheckDevice(hr, "Test swap-chain visibility");
    return hr != DXGI_STATUS_OCCLUDED;
}
void Renderer::SaveCapture(const std::filesystem::path& path, bool verifyTriangle, AssetCaptureCheck assetCheck) {
    WaitIdle();
    std::vector<unsigned char> pixels(static_cast<size_t>(width_) * height_ * 4);
    unsigned char* mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(captureBytes_)};
    Check(readback_->Map(0, &range, reinterpret_cast<void**>(&mapped)), "Map capture readback");
    for (UINT y = 0; y < height_; ++y) {
        const auto* row = mapped + captureLayout_.Offset + static_cast<size_t>(y) * captureLayout_.Footprint.RowPitch;
        auto* out = pixels.data() + static_cast<size_t>(y) * width_ * 4;
        for (UINT x = 0; x < width_; ++x) {
            out[x * 4] = row[x * 4 + 2]; out[x * 4 + 1] = row[x * 4 + 1];
            out[x * 4 + 2] = row[x * 4]; out[x * 4 + 3] = 255;
        }
    }
    D3D12_RANGE noWrite{0, 0};
    readback_->Unmap(0, &noWrite);
    if (verifyTriangle) {
        // Right-side interior is away from the editor. A clear-only frame must fail.
        const auto at = (static_cast<size_t>(height_ / 2) * width_ + static_cast<UINT>(static_cast<float>(width_) * 0.675f)) * 4;
        const auto bg = (static_cast<size_t>(height_ - 5) * width_ + width_ - 5) * 4;
        if (pixels[at] < 50 || pixels[at + 1] < 50 || pixels[at + 2] < 50)
            throw std::runtime_error("GPU readback: expected colored triangle interior, found background/invalid pixels");
        for (size_t c = 0; c < 3; ++c) {
            const auto expected = static_cast<int>(std::lround(lastClear_[2 - c] * 255.0f));
            if (std::abs(static_cast<int>(pixels[bg + c]) - expected) > 2)
                throw std::runtime_error("GPU readback: background color mismatch");
        }
        log_.Write("GPU readback PASS: triangle interior and SDR background verified");
    }
    if (!path.empty()) {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
        info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width_);
        info.biHeight = -static_cast<LONG>(height_); info.biPlanes = 1; info.biBitCount = 32;
        info.biCompression = BI_RGB; info.biSizeImage = static_cast<DWORD>(pixels.size());
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
        stream.write(reinterpret_cast<const char*>(&info), sizeof(info));
        stream.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        if (!stream) throw std::runtime_error("Cannot save GPU capture: " + path.string());
        log_.Write("GPU capture saved: " + path.string());
    }
    if (assetCheck == AssetCaptureCheck::VfxAlpha) {
        const auto decode=[](double v){return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4);};
        const auto encode=[](double v){return v<=0.0031308?v*12.92:1.055*std::pow(v,1.0/2.4)-0.055;};
        const double alpha=128.0/255.0;const int texture[]{160,96,128};
        const auto at=(static_cast<size_t>(height_/2)*width_+width_/2)*4;
        for(size_t c=0;c<3;++c){const int expected=static_cast<int>(std::lround(255*encode(decode(texture[c]/255.0)*alpha*alpha+decode(lastClear_[2-c])*(1-alpha))));if(std::abs(static_cast<int>(pixels[at+c])-expected)>3)throw std::runtime_error(std::format("VFX linear alpha mismatch: channel {}, actual {}, expected {}",c,pixels[at+c],expected));}
        log_.Write("VFX PIXELS PASS: sRGB texture, linear vertex color factor, straight alpha blend and sRGB RTV output");
    }
    if (assetCheck == AssetCaptureCheck::Midtone) {
        // Input sRGB bytes RGB=(128,96,160), linear factors=(0.5,0.25,0.75).
        // Independent IEC sRGB calculation, rounded to UNORM8: RGB=(92,48,140).
        // Missing decode, gamma-space multiply, or missing/double output encode must fail.
        const int expectedBgr[]{140,48,92};
        const auto at = (static_cast<size_t>(height_/2)*width_+width_/2)*4;
        for (size_t c = 0; c < 3; ++c)
            if (std::abs(static_cast<int>(pixels[at+c])-expectedBgr[c]) > 2)
                throw std::runtime_error("Midtone pixels failed: sRGB decode / linear factor / output encode mismatch");
        log_.Write("MIDTONE PIXELS PASS: sRGB texture decode, linear factors and SDR output encoding");
    }
    if (assetCheck == AssetCaptureCheck::Corners || assetCheck == AssetCaptureCheck::UpdatedCorners) {
        // Authored fixture: near textured quad drawn before a farther solid-blue quad.
        // Camera at (0,0,-6), near quad z=-1, world x=0.5; FOV=45 degrees.
        const float focal = static_cast<float>(height_) * 0.5f / std::tan(DirectX::XM_PIDIV4 * 0.5f);
        const float distance = assetCheck == AssetCaptureCheck::UpdatedCorners ? 4.0f : 5.0f;
        const int cx = static_cast<int>(static_cast<float>(width_) * 0.5f + focal * 0.5f / distance);
        const int cy = static_cast<int>(height_ / 2);
        const int offset = static_cast<int>(focal * 0.8f / distance);
        const int colors[4][3]{{0,0,255},{0,255,0},{255,0,0},{0,255,255}}; // BMP BGR
        for (int q = 0; q < 4; ++q) {
            const int x = cx + (q % 2 ? offset : -offset), y = cy + (q / 2 ? offset : -offset);
            const auto at = (static_cast<size_t>(y)*width_+x)*4;
            for (size_t c = 0; c < 3; ++c)
                if (std::abs(static_cast<int>(pixels[at+c])-colors[q][c]) > 3)
                    throw std::runtime_error("Asset pixels failed: UV/culling/depth/camera/world transform mismatch");
        }
        // A point outside the translated quad rejects accidentally omitted world translation.
        const auto outside = (static_cast<size_t>(cy)*width_+static_cast<UINT>(static_cast<float>(width_)*0.5f-focal*0.75f/distance))*4;
        for (size_t c = 0; c < 3; ++c)
            if (std::abs(static_cast<int>(pixels[outside+c])-static_cast<int>(std::lround(lastClear_[2-c]*255))) > 2)
                throw std::runtime_error("Asset world translation/background check failed");
        log_.Write("ASSET PIXELS PASS: four UV quadrants, front-face culling, depth occlusion, camera projection and world translation");
    }
}
void Renderer::DrainDiagnostics() {
    if (!infoQueue_) return;
    const auto count = infoQueue_->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T length = 0;
        Check(infoQueue_->GetMessage(i, nullptr, &length), "Get debug message size");
        std::vector<std::byte> storage(length);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Check(infoQueue_->GetMessage(i, message, &length), "Read debug message");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) ++diagnosticErrors_;
        log_.Write(std::format("D3D12 severity={} id={}: {}", static_cast<int>(message->Severity),
            static_cast<int>(message->ID), message->pDescription));
    }
    infoQueue_->ClearStoredMessages();
}
}
