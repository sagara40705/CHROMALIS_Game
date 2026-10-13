#include "runtime/Renderer.h"
#include "Mesh_vs.h"
#include "Mesh_ps.h"
#include "Skin_vs.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace chromalis {
bool Renderer::ReloadVfx(const std::filesystem::path& path) {
    WaitIdle();
    if(!vfx_)vfx_=std::make_unique<VfxPlayer>(device_.Get(),queue_.Get(),SceneFormat);
    const bool ok=vfx_->Reload(path);log_.Write(vfx_->Status());return ok;
}
void Renderer::UpdatePresentation(float seconds,const RenderSettings& settings) {
    if(animation_)animation_->Update(seconds);
    if(vfx_) {
        auto v=settings.vfx;
        if(v.followSocket && animation_) {
            using namespace DirectX;const auto& pose=settings.animated;XMFLOAT4X4 world;
            XMStoreFloat4x4(&world,XMMatrixScaling(pose.scale,pose.scale,pose.scale)*XMMatrixRotationY(pose.yaw)*XMMatrixTranslation(pose.position[0],pose.position[1],pose.position[2]));
            const auto joint=std::clamp(v.joint,0,static_cast<int>(animation_->Asset().joints.size())-1);
            const auto socket=animation_->Socket(static_cast<size_t>(joint),world);
            v.position[0]+=socket.x;v.position[1]+=socket.y;v.position[2]+=socket.z;
        }
        vfx_->Update(seconds,v);
    }
}
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
void Renderer::CreateMeshPipeline() {
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_DESCRIPTOR_RANGE sampler{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,1,0,0,0};
    D3D12_ROOT_PARAMETER parameters[4]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants = {0,0,40}; parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
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
bool Renderer::ReloadModel(size_t slot, const std::filesystem::path& path) {
    bool submitted = false;
    try {
        if (slot >= models_.size()) throw std::runtime_error("Model preview slot out of range");
        const auto asset = LoadStaticAsset(path);
        auto candidate=UploadModel(asset,nullptr,submitted);
        models_[slot].swap(candidate); ++modelGenerations_[slot];
        assetStatus_ = std::format("Loaded {} into slot {} (generation {})",asset.id,slot,modelGenerations_[slot]);
        log_.Write(assetStatus_); return true;
    } catch (const std::exception& e) {
        assetStatus_ = std::string("Reload failed; previous model retained: ") + e.what(); log_.Write(assetStatus_);
        if (submitted || FAILED(device_->GetDeviceRemovedReason())) throw;
        return false;
    }
}
bool Renderer::ReloadAnimation(const std::filesystem::path& path) {
    bool submitted=false;
    try {
        auto player=std::make_unique<AnimationPlayer>(LoadSkeletalAsset(path));
        auto candidate=UploadModel(player->Asset().mesh,&player->Asset().skin,submitted);
        animation_.swap(player);animatedModel_.swap(candidate);
        assetStatus_="Loaded skeletal asset: "+animation_->Asset().mesh.id;log_.Write(assetStatus_);return true;
    }catch(const std::exception& e){assetStatus_=std::string("Animation reload failed; retained: ")+e.what();log_.Write(assetStatus_);if(submitted || FAILED(device_->GetDeviceRemovedReason()))throw;return false;}
}
std::unique_ptr<Renderer::GpuModel> Renderer::UploadModel(const StaticAsset& asset,const std::vector<SkinVertex>* skin,bool& submitted) {
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
        ID3D12CommandList* lists[]{commands.Get()}; queue_->ExecuteCommandLists(1,lists); submitted = true;
        // 0B reload is synchronous. This drains both old draws and new uploads before exchanging ownership.
        WaitIdle();
        return candidate;
}
void Renderer::DrawModels(const RenderSettings& settings) {
    using namespace DirectX;
    const float cy = std::cos(settings.cameraYaw), sy = std::sin(settings.cameraYaw);
    const float cp = std::cos(settings.cameraPitch), sp = std::sin(settings.cameraPitch);
    const auto eye = XMVectorSet(sy*cp*settings.cameraDistance,sp*settings.cameraDistance,-cy*cp*settings.cameraDistance,1);
    const auto view = XMMatrixLookAtLH(eye,XMVectorZero(),XMVectorSet(0,1,0,0));
    const auto projection = XMMatrixPerspectiveFovLH(XM_PIDIV4,static_cast<float>(width_)/static_cast<float>(height_),0.05f,500.0f);
    list_->SetGraphicsRootSignature(meshRoot_.Get());
    const auto srvStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto samplerStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    for (size_t i = 0; i < models_.size()+1; ++i) {
        const bool animated=i==models_.size();
        const auto* model=animated?animatedModel_.get():models_[i].get();
        if (!model) continue;
        const auto& m = *model; const auto& pose = animated?settings.animated:settings.models[i];
        const auto world = XMMatrixScaling(pose.scale,pose.scale,pose.scale)*XMMatrixRotationY(pose.yaw)*XMMatrixTranslation(pose.position[0],pose.position[1],pose.position[2]);
        struct Constants { XMFLOAT4X4 wvp, world; std::array<float,4> color, options; } constants;
        static_assert(sizeof(Constants) == 160);
        XMStoreFloat4x4(&constants.wvp,world*view*projection); XMStoreFloat4x4(&constants.world,world);
        ID3D12DescriptorHeap* heaps[]{m.srv.Get(),m.samplers.Get()}; list_->SetDescriptorHeaps(2,heaps);
        list_->IASetVertexBuffers(0,1,&m.vb); list_->IASetIndexBuffer(&m.ib);
        if(animated) {
            auto& frame=frames_[frameIndex_];
            // BeginFrameでこのフレームのFence完了を待ってから上書きする。
            std::memcpy(frame.bonesMapped,animation_->Palette().data(),animation_->Palette().size()*64);
            std::memcpy(frame.bonesMapped+256*64,animation_->Normals().data(),animation_->Normals().size()*64);
            list_->SetGraphicsRootConstantBufferView(3,frame.bones->GetGPUVirtualAddress());
            list_->IASetVertexBuffers(1,1,&m.skinView);
        }
        for (const auto& part : m.parts) {
            const auto& material = m.materials[part.material];
            constants.color = material.baseColor; constants.options = {material.cutoff,static_cast<float>(material.alphaMask),(settings.unlit || material.unlit)?1.0f:0.0f,0};
            list_->SetPipelineState(animated?skinPso_[material.doubleSided].Get():meshPso_[material.doubleSided].Get());
            list_->SetGraphicsRoot32BitConstants(0,40,&constants,0);
            auto texture = m.srv->GetGPUDescriptorHandleForHeapStart(); texture.ptr += UINT64(material.texture)*srvStride;
            auto sampler = m.samplers->GetGPUDescriptorHandleForHeapStart(); sampler.ptr += UINT64(part.material)*samplerStride;
            list_->SetGraphicsRootDescriptorTable(1,texture); list_->SetGraphicsRootDescriptorTable(2,sampler);
            list_->DrawIndexedInstanced(part.indexCount,1,part.firstIndex,0,0);
        }
    }
}
}
