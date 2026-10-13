#include "runtime/Renderer.h"
#include "Mesh_vs.h"
#include "Mesh_ps.h"
#include "Skin_vs.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace chromalis {
namespace {
D3D12_HEAP_PROPERTIES HeapType(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{}; h.Type = type; h.CreationNodeMask = h.VisibleNodeMask = 1; return h;
}
D3D12_RESOURCE_DESC Buffer(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes;
    d.Height = d.DepthOrArraySize = d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; return d;
}
void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1, &b);
}
}
void Renderer::CreateDepth() {
    if (!dsv_) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; heap.NumDescriptors = 1;
        Check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&dsv_)), "Create depth descriptor heap");
    }
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width_; desc.Height = height_; desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT; desc.SampleDesc.Count = 1; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    const auto heap = HeapType(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.DepthStencil.Depth = 1;
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear, IID_PPV_ARGS(&depth_)), "Create depth buffer");
    device_->CreateDepthStencilView(depth_.Get(), nullptr, dsv_->GetCPUDescriptorHandleForHeapStart());
}
void Renderer::SetSceneSurface(UINT width, UINT height, D3D12_CPU_DESCRIPTOR_HANDLE srv, const std::array<float, 4>& background) {
    if (!width || !height || width > 8192 || height > 8192 || !srv.ptr) throw std::runtime_error("Invalid Scene View extent/descriptor");
    if (sceneColor_ && sceneWidth_ == width && sceneHeight_ == height && sceneClear_ == background && sceneSrv_.ptr == srv.ptr) return;
    WaitIdle();
    // 候補が完成するまで旧Surfaceを保持し、SRV更新前に全フレームを待つ。
    ComPtr<ID3D12Resource> color, depth;
    ComPtr<ID3D12DescriptorHeap> rtv, dsv;
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{}; descriptors.NumDescriptors = 1;
    descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Check(device_->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&rtv)), "Create Scene View RTV");
    descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    Check(device_->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&dsv)), "Create Scene View DSV");
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const auto heap = HeapType(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE colorClear{}; colorClear.Format = SceneFormat;
    for (size_t i = 0; i < 3; ++i) colorClear.Color[i] = background[i] <= 0.04045f ? background[i] / 12.92f : std::pow((background[i] + 0.055f) / 1.055f, 2.4f);
    colorClear.Color[3] = background[3];
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &colorClear, IID_PPV_ARGS(&color)), "Create Scene View color");
    desc.Format = DXGI_FORMAT_D32_FLOAT; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.DepthStencil.Depth = 1;
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear, IID_PPV_ARGS(&depth)), "Create Scene View depth");
    D3D12_RENDER_TARGET_VIEW_DESC target{}; target.Format = SceneFormat; target.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(color.Get(), &target, rtv->GetCPUDescriptorHandleForHeapStart());
    device_->CreateDepthStencilView(depth.Get(), nullptr, dsv->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = OutputFormat; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
    // Scene RTVで符号化済みの表示色をImGuiへ渡し、二重変換を避ける。
    device_->CreateShaderResourceView(color.Get(), &view, srv);
    sceneColor_ = std::move(color); sceneDepth_ = std::move(depth);
    sceneRtv_ = std::move(rtv); sceneDsv_ = std::move(dsv);
    sceneWidth_ = width; sceneHeight_ = height;
    sceneClear_ = background; sceneSrv_ = srv;
}
void Renderer::DisableSceneSurface() {
    if (!sceneColor_) return;
    WaitIdle(); sceneColor_.Reset(); sceneDepth_.Reset(); sceneRtv_.Reset(); sceneDsv_.Reset();
    sceneWidth_ = sceneHeight_ = 0;
}
void Renderer::CreateMeshPipeline() {
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_DESCRIPTOR_RANGE sampler{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,1,0,0,0};
    D3D12_ROOT_PARAMETER parameters[4]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor = {0,0}; parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {1,&srv}; parameters[2].DescriptorTable = {1,&sampler};
    parameters[1].ShaderVisibility = parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[3].Descriptor={1,0};parameters[3].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC root{}; root.NumParameters = 4; root.pParameters = parameters;
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, errors;
    auto result = D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors);
    if (errors) log_.Write(std::string_view(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));
    Check(result,"Serialize mesh root signature");
    Check(device_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&meshRoot_)),"Create mesh root signature");
    const D3D12_INPUT_ELEMENT_DESC input[]{
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,1,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{}; p.pRootSignature = meshRoot_.Get();
    p.VS = {Mesh_vs,sizeof(Mesh_vs)}; p.PS = {Mesh_ps,sizeof(Mesh_ps)}; p.InputLayout = {input,3};
    auto& blend = p.BlendState.RenderTarget[0];
    blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP; blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    p.SampleMask = UINT_MAX; p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // Cooker reflects Z and reverses glTF triangle winding for the LH camera.
    p.RasterizerState.FrontCounterClockwise = FALSE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.DepthStencilState.DepthEnable = TRUE; p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 1; p.RTVFormats[0] = SceneFormat; p.DSVFormat = DXGI_FORMAT_D32_FLOAT; p.SampleDesc.Count = 1;
    for (size_t i = 0; i < meshPso_.size(); ++i) {
        p.RasterizerState.CullMode = i == 0 ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
        Check(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&meshPso_[i])),"Create mesh pipeline");
    }
    p.VS={Skin_vs,sizeof(Skin_vs)};p.InputLayout={input,5};
    for(size_t i=0;i<skinPso_.size();++i){p.RasterizerState.CullMode=i==0?D3D12_CULL_MODE_BACK:D3D12_CULL_MODE_NONE;Check(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&skinPso_[i])),"Create skin pipeline");}
}
MeshHandle Renderer::CreateMesh(const StaticAsset& asset, const std::vector<SkinVertex>* skin) {
    auto model = UploadModel(asset, skin);
    for (size_t i = 0; i < meshes_.size(); ++i) {
        if (!meshes_[i].model && meshes_[i].generation != UINT32_MAX) {
            meshes_[i].model = std::move(model);
            return {static_cast<uint32_t>(i), meshes_[i].generation};
        }
    }
    meshes_.push_back({std::move(model), 1});
    return {static_cast<uint32_t>(meshes_.size() - 1), 1};
}
void Renderer::ReleaseMesh(MeshHandle handle) {
    auto& entry = meshes_.at(handle.slot);
    if (!entry.model || entry.generation != handle.generation) throw std::runtime_error("Stale mesh handle");
    WaitIdle();
    entry.model.reset();
    ++entry.generation;
}
std::unique_ptr<Renderer::GpuModel> Renderer::UploadModel(const StaticAsset& asset,const std::vector<SkinVertex>* skin) {
        auto candidate = std::make_unique<GpuModel>();
        candidate->id = asset.id; candidate->parts = asset.parts; candidate->materials = asset.materials;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create asset upload allocator");
        Check(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create asset upload list");
        std::vector<ComPtr<ID3D12Resource>> uploads;
        const auto gpuHeap = HeapType(D3D12_HEAP_TYPE_DEFAULT), cpuHeap = HeapType(D3D12_HEAP_TYPE_UPLOAD);
        const auto uploadBuffer = [&](const void* source, size_t size, ComPtr<ID3D12Resource>& destination, D3D12_RESOURCE_STATES target) {
            const auto desc = Buffer(size);
            Check(device_->CreateCommittedResource(&gpuHeap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&destination)),"Create mesh GPU buffer");
            ComPtr<ID3D12Resource> upload;
            Check(device_->CreateCommittedResource(&cpuHeap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"Create mesh upload buffer");
            void* mapped = nullptr; D3D12_RANGE noRead{0,0};
            Check(upload->Map(0,&noRead,&mapped),"Map mesh upload"); std::memcpy(mapped,source,size); upload->Unmap(0,nullptr);
            Barrier(commands.Get(),destination.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
            commands->CopyBufferRegion(destination.Get(),0,upload.Get(),0,size);
            Barrier(commands.Get(),destination.Get(),D3D12_RESOURCE_STATE_COPY_DEST,target);
            uploads.push_back(std::move(upload));
        };
        static_assert(sizeof(MeshVertex) == 32);
        const auto vbSize = asset.vertices.size()*sizeof(MeshVertex), ibSize = asset.indices.size()*sizeof(uint32_t);
        uploadBuffer(asset.vertices.data(),vbSize,candidate->vertex,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        uploadBuffer(asset.indices.data(),ibSize,candidate->index,D3D12_RESOURCE_STATE_INDEX_BUFFER);
        candidate->vb = {candidate->vertex->GetGPUVirtualAddress(),static_cast<UINT>(vbSize),sizeof(MeshVertex)};
        candidate->ib = {candidate->index->GetGPUVirtualAddress(),static_cast<UINT>(ibSize),DXGI_FORMAT_R32_UINT};
        if(skin) {
            static_assert(sizeof(SkinVertex)==32);
            uploadBuffer(skin->data(),skin->size()*sizeof(SkinVertex),candidate->skin,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            candidate->skinView={candidate->skin->GetGPUVirtualAddress(),static_cast<UINT>(skin->size()*sizeof(SkinVertex)),sizeof(SkinVertex)};
        }
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; heap.NumDescriptors = static_cast<UINT>(asset.textures.size());
        Check(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&candidate->srv)),"Create asset texture heap");
        auto cpu = candidate->srv->GetCPUDescriptorHandleForHeapStart();
        const auto srvStride = device_->GetDescriptorHandleIncrementSize(heap.Type);
        for (const auto& t : asset.textures) {
            D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = t.width; desc.Height = t.height; desc.MipLevels = desc.DepthOrArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; desc.SampleDesc.Count = 1;
            ComPtr<ID3D12Resource> texture, upload;
            Check(device_->CreateCommittedResource(&gpuHeap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)),"Create asset texture");
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; UINT64 bytes = 0;
            device_->GetCopyableFootprints(&desc,0,1,0,&layout,nullptr,nullptr,&bytes);
            const auto buffer = Buffer(bytes);
            Check(device_->CreateCommittedResource(&cpuHeap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"Create texture upload");
            uint8_t* mapped = nullptr; D3D12_RANGE noRead{0,0};
            Check(upload->Map(0,&noRead,reinterpret_cast<void**>(&mapped)),"Map texture upload");
            for (UINT y = 0; y < t.height; ++y) std::memcpy(mapped + layout.Offset + size_t(y)*layout.Footprint.RowPitch,t.rgba.data()+size_t(y)*t.width*4,size_t(t.width)*4);
            upload->Unmap(0,nullptr);
            D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint = layout;
            D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = texture.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            Barrier(commands.Get(),texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = desc.Format; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
            device_->CreateShaderResourceView(texture.Get(),&view,cpu); cpu.ptr += srvStride;
            candidate->textures.push_back(std::move(texture)); uploads.push_back(std::move(upload));
        }
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; heap.NumDescriptors = static_cast<UINT>(asset.materials.size());
        Check(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&candidate->samplers)),"Create asset sampler heap");
        cpu = candidate->samplers->GetCPUDescriptorHandleForHeapStart();
        const auto samplerStride = device_->GetDescriptorHandleIncrementSize(heap.Type);
        constexpr D3D12_TEXTURE_ADDRESS_MODE wrap[]{D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_MIRROR};
        for (const auto& material : asset.materials) {
            D3D12_SAMPLER_DESC sampler{}; sampler.Filter = material.nearest ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = wrap[material.wrapU]; sampler.AddressV = wrap[material.wrapV]; sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.MaxAnisotropy = 1; sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; sampler.MaxLOD = D3D12_FLOAT32_MAX;
            device_->CreateSampler(&sampler,cpu); cpu.ptr += samplerStride;
        }
        Check(commands->Close(),"Close asset upload list");
        ID3D12CommandList* lists[]{commands.Get()}; queue_->ExecuteCommandLists(1,lists);
        // 提出後の待機失敗はGpuFailureで終了へ伝播し、reload成功扱いにしない。
        WaitIdle();
        return candidate;
}
void Renderer::PrepareBoneUpload(std::span<const DrawItem> items) {
    UINT64 required = 0;
    for (const auto& item : items) {
        const auto& entry = meshes_.at(item.mesh.slot);
        if (!entry.model || entry.generation != item.mesh.generation) throw std::runtime_error("Stale draw mesh handle");
        const bool skinned = bool(entry.model->skin);
        if (skinned != !item.bones.empty() || item.bones.size() > 256 || item.normals.size() != item.bones.size())
            throw std::runtime_error("Draw palette does not match mesh");
        if (skinned) required += 256 * 128;
    }
    auto& frame = frames_[frameIndex_];
    if (required <= frame.boneCapacity) return;
    // 対応Fenceを待ったフレームだけ拡張する。各個体は別のCBV領域を使用する。
    const auto desc = Buffer(required);
    const auto heap = HeapType(D3D12_HEAP_TYPE_UPLOAD);
    ComPtr<ID3D12Resource> candidate;
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&candidate)), "Grow frame palettes");
    std::byte* mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    Check(candidate->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "Map frame palettes");
    frame.bones->Unmap(0, nullptr);
    frame.bones = std::move(candidate);
    frame.bonesMapped = mapped;
    frame.boneCapacity = required;
}
void Renderer::DrawModels(const RenderSettings& settings, std::span<const DrawItem> items) {
    using namespace DirectX;
    list_->SetGraphicsRootSignature(meshRoot_.Get());
    const auto srvStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto samplerStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    UINT64 paletteOffset = 0;
    UINT64 materialOffset = 0;
    auto& frame = frames_[frameIndex_];
    for (const auto& item : items) {
        const auto& model = *meshes_[item.mesh.slot].model;
        const bool skinned = !item.bones.empty();
        const auto world = XMLoadFloat4x4(&item.world);
        struct Constants {
            XMFLOAT4X4 wvp, world, normalWorld;
            std::array<float, 4> color, options, emission, toon, shadow, lightDirection, lightColor, ambient;
        } constants{};
        static_assert(sizeof(Constants) == 320);
        const auto vector4 = [](const std::array<float, 3>& v) { return std::array<float, 4>{v[0], v[1], v[2], 0}; };
        constants.lightDirection = vector4(settings.lighting.direction);
        constants.lightColor = vector4(settings.lighting.color);
        constants.ambient = vector4(settings.lighting.ambient);
        XMStoreFloat4x4(&constants.wvp, world * XMLoadFloat4x4(&view_) * XMLoadFloat4x4(&projection_));
        constants.world = item.world;
        XMStoreFloat4x4(&constants.normalWorld, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
        ID3D12DescriptorHeap* heaps[]{model.srv.Get(), model.samplers.Get()};
        list_->SetDescriptorHeaps(2, heaps);
        list_->IASetVertexBuffers(0, 1, &model.vb);
        list_->IASetIndexBuffer(&model.ib);
        if (skinned) {
            std::memcpy(frame.bonesMapped + paletteOffset, item.bones.data(), item.bones.size_bytes());
            std::memcpy(frame.bonesMapped + paletteOffset + 256 * 64, item.normals.data(), item.normals.size_bytes());
            list_->SetGraphicsRootConstantBufferView(3, frame.bones->GetGPUVirtualAddress() + paletteOffset);
            list_->IASetVertexBuffers(1, 1, &model.skinView);
            paletteOffset += 256 * 128;
        }
        for (const auto& part : model.parts) {
            const auto& material = model.materials[part.material];
            const auto surface = item.materials.empty() ? ImportedSurface(material) : item.materials[part.material];
            constants.color = surface.baseColor;
            constants.options = {surface.cutoff, surface.alphaMask ? 1.0f : 0.0f,
                settings.unlit ? 1.0f : static_cast<float>(surface.shading), 0};
            constants.emission = vector4(surface.emission);
            constants.toon = {surface.threshold, surface.softness, 0, 0};
            constants.shadow = vector4(surface.shadow);
            list_->SetPipelineState(skinned ? skinPso_[surface.doubleSided].Get() : meshPso_[surface.doubleSided].Get());
            std::memcpy(frame.materialsMapped + materialOffset, &constants, sizeof(constants));
            list_->SetGraphicsRootConstantBufferView(0, frame.materialConstants->GetGPUVirtualAddress() + materialOffset);
            materialOffset += 512;
            auto texture = model.srv->GetGPUDescriptorHandleForHeapStart();
            texture.ptr += UINT64(material.texture) * srvStride;
            auto sampler = model.samplers->GetGPUDescriptorHandleForHeapStart();
            sampler.ptr += UINT64(part.material) * samplerStride;
            list_->SetGraphicsRootDescriptorTable(1, texture);
            list_->SetGraphicsRootDescriptorTable(2, sampler);
            list_->DrawIndexedInstanced(part.indexCount, 1, part.firstIndex, 0, 0);
        }
    }
}
std::span<const MeshMaterial> Renderer::MeshMaterials(MeshHandle mesh) const {
    const auto& entry = meshes_.at(mesh.slot);
    if (!entry.model || entry.generation != mesh.generation) throw std::runtime_error("Stale material mesh handle");
    return entry.model->materials;
}
void Renderer::PrepareMaterialUpload(std::span<const DrawItem> items) {
    UINT64 required = 0;
    for (const auto& item : items) {
        const auto& model = *meshes_[item.mesh.slot].model;
        if (!item.materials.empty() && item.materials.size() != model.materials.size())
            throw std::runtime_error("Draw material count does not match mesh");
        required += model.parts.size() * 512;
    }
    auto& frame = frames_[frameIndex_];
    if (required <= frame.materialCapacity) return;
    // フレームFence完了後に確保し、各Primitiveを256バイト境界の独立領域へ書く。
    const auto heap = HeapType(D3D12_HEAP_TYPE_UPLOAD);
    const auto desc = Buffer(required);
    ComPtr<ID3D12Resource> candidate;
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&candidate)), "Grow frame material constants");
    std::byte* mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    Check(candidate->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "Map material constants");
    if (frame.materialsMapped) frame.materialConstants->Unmap(0, nullptr);
    frame.materialConstants = std::move(candidate);
    frame.materialsMapped = mapped;
    frame.materialCapacity = required;
}
size_t Renderer::MaterialUseCount(MeshHandle mesh, uint32_t slot) const {
    (void)MeshMaterials(mesh);
    const auto& parts = meshes_[mesh.slot].model->parts;
    return static_cast<size_t>(std::count_if(parts.begin(), parts.end(), [&](const auto& part) { return part.material == slot; }));
}
}
