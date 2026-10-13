#include "runtime/VfxPlayer.h"
#include "assets/StaticAsset.h"
#include <Effekseer.h>
#include <EffekseerRendererDX12.h>
#include <EffekseerRendererCommon/TextureLoader.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace chromalis {
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void ValidateNodes(Effekseer::EffectNode* node) {
    const auto parameters=node->GetBasicRenderParameter();
    Need(!parameters.Distortion && parameters.MaterialType!=Effekseer::RendererMaterialType::BackDistortion,
        "Background distortion is not connected in 0C");
    Need(parameters.MaterialType!=Effekseer::RendererMaterialType::File,"Custom VFX materials are not connected in 0C");
    Need(parameters.SoftParticleDistanceFar==0 && parameters.SoftParticleDistanceNear==0 && parameters.SoftParticleDistanceNearOffset==0,
        "Soft particles require a scene depth connection; unavailable in 0C");
    for(int i=0;i<node->GetChildrenCount();++i)ValidateNodes(node->GetChild(i));
}
Effekseer::ManagerRef MakeManager(const EffekseerRenderer::RendererRef& renderer) {
    auto manager=Effekseer::Manager::Create(4096);Need(bool(manager),"Effekseer manager creation failed");
    manager->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);
    manager->SetSpriteRenderer(renderer->CreateSpriteRenderer());manager->SetRibbonRenderer(renderer->CreateRibbonRenderer());
    manager->SetRingRenderer(renderer->CreateRingRenderer());manager->SetTrackRenderer(renderer->CreateTrackRenderer());
    manager->SetModelRenderer(renderer->CreateModelRenderer());manager->SetModelLoader(renderer->CreateModelLoader());
    manager->SetTextureLoader(EffekseerRenderer::CreateTextureLoader(renderer->GetGraphicsDevice(),nullptr,Effekseer::ColorSpaceType::Linear));
    return manager;
}
}
struct VfxResources {
    EffekseerRenderer::RendererRef renderer;
    Effekseer::ManagerRef manager;
    Effekseer::EffectRef effect;
    std::array<Effekseer::RefPtr<EffekseerRenderer::SingleFrameMemoryPool>,2> pools;
    std::array<Effekseer::RefPtr<EffekseerRenderer::CommandList>,2> lists;
    int drawCalls = 0;
    std::string status="No VFX loaded";
    float time=0;
};
struct VfxPlayer::Impl {
    std::shared_ptr<VfxResources> resources = std::make_shared<VfxResources>();
    std::vector<Effekseer::Handle> handles;
    DirectX::XMFLOAT4X4 world{};
    bool worldValid = false;
};
VfxPlayer::VfxPlayer(ID3D12Device* device,ID3D12CommandQueue* queue,DXGI_FORMAT format):impl_(std::make_unique<Impl>()) {
    auto& p=*impl_->resources;auto graphics=EffekseerRendererDX12::CreateGraphicsDevice(device,queue,2);Need(bool(graphics),"Effekseer DX12 device creation failed");
    p.renderer=EffekseerRendererDX12::Create(graphics,&format,1,DXGI_FORMAT_D32_FLOAT,false,4096);Need(bool(p.renderer),"Effekseer DX12 renderer creation failed");
    // 色テクスチャだけsRGBデコードし、法線・歪みなどのデータは線形で読む。
    p.manager=MakeManager(p.renderer);
    p.renderer->SetMaintainGammaColorInLinearColorSpace(false);
    for(size_t i=0;i<p.pools.size();++i){p.pools[i]=EffekseerRenderer::CreateSingleFrameMemoryPool(graphics);p.lists[i]=EffekseerRenderer::CreateCommandList(graphics,p.pools[i]);Need(bool(p.pools[i])&&bool(p.lists[i]),"Effekseer frame resource creation failed");}
}
VfxPlayer::VfxPlayer(VfxPlayer& resource) : impl_(std::make_unique<Impl>()) {
    impl_->resources = resource.impl_->resources;
}
VfxPlayer::~VfxPlayer() { Stop(); }
bool VfxPlayer::Reload(const std::filesystem::path& path) {
    auto& p=*impl_->resources;
    try {
        Need(impl_->resources.use_count() == 1, "Reload shared VFX through SceneRuntime");
        const auto bytes=ReadAssetBytes(path);Need(bytes.size()>=16 && bytes.size()<=32u*1024*1024 && std::memcmp(bytes.data(),"EFKE",4)==0,"Expected compiled .efkefc (up to 32 MiB)");
        const auto base=std::filesystem::absolute(path).parent_path().u16string();
        // 同名PNGを更新しても旧リソースキャッシュを再使用しない。
        auto manager=MakeManager(p.renderer);
        auto candidate=Effekseer::Effect::Create(manager,bytes.data(),static_cast<int32_t>(bytes.size()),1,base.c_str());Need(bool(candidate),"Effekseer effect parse failed");
        for(int i=0;i<candidate->GetColorImageCount();++i)Need(bool(candidate->GetColorImage(i)),"Missing VFX color texture");
        for(int i=0;i<candidate->GetNormalImageCount();++i)Need(bool(candidate->GetNormalImage(i)),"Missing VFX data texture");
        Need(candidate->GetDistortionImageCount()==0,"Background distortion is not connected in 0C");
        Need(candidate->GetWaveCount()==0,"VFX audio is not connected in 0C");
        Need(candidate->GetMaterialCount()==0,"Custom VFX materials are not connected in 0C");
        for(int i=0;i<candidate->GetModelCount();++i)Need(bool(candidate->GetModel(i)),"Missing VFX model");
        auto* root=candidate->GetRoot();Need(root!=nullptr,"Missing VFX root");
        for(int i=0;i<root->GetChildrenCount();++i)ValidateNodes(root->GetChild(i));
        EffekseerRenderer::FlushAndWait(p.renderer->GetGraphicsDevice());
        Stop();p.effect=candidate;p.manager=manager;p.status="VFX loaded: "+path.filename().string();return true;
    }catch(const std::exception& e){p.status=std::string("VFX reload failed; retained: ")+e.what();return false;}
}
void VfxPlayer::Play(int count) {
    auto& p = *impl_->resources;
    Need(count >= 1 && count <= 64, "VFX preview count must be 1..64");
    if (!p.effect) { p.status = "Load an effect first"; return; }
    // 予算不足でも途中まで生成したハンドルを残さず、元の再生を維持する。
    std::vector<Effekseer::Handle> candidate;
    candidate.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto handle = p.manager->Play(p.effect, 0, 0, 0);
        if (handle < 0) {
            for (auto created : candidate) p.manager->StopEffect(created);
            p.status = "VFX instance budget exhausted; previous playback retained";
            return;
        }
        candidate.push_back(handle);
    }
    Stop();
    impl_->handles = std::move(candidate);
    const auto world = impl_->world;
    const bool valid = impl_->worldValid;
    impl_->worldValid = false;
    if (valid) SetWorld(world);
}
void VfxPlayer::Stop() {
    // 同じアセットの他の配置を停止しない。回収は共有Managerの次の更新で行う。
    for (auto handle : impl_->handles) impl_->resources->manager->StopEffect(handle);
    impl_->handles.clear();
}
void VfxPlayer::SetWorld(const DirectX::XMFLOAT4X4& world) {
    auto& player = *impl_;
    std::erase_if(player.handles, [&](auto handle) { return !player.resources->manager->Exists(handle); });
    if (player.worldValid && std::memcmp(&player.world, &world, sizeof(world)) == 0) return;
    player.world = world;
    player.worldValid = true;
    Effekseer::Matrix43 matrix;
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 3; ++column) matrix.Value[row][column] = world.m[row][column];
    for (size_t i = 0; i < player.handles.size(); ++i) {
        // 個数プレビューの間隔は従来通りWorld X方向。配置用のローカル変換とは分離する。
        matrix.Value[3][0] = world._41 + static_cast<float>(i) * 0.35f;
        player.resources->manager->SetMatrix(player.handles[i], matrix);
    }
}
void VfxPlayer::Update(float seconds) {
    Need(std::isfinite(seconds) && seconds >= 0, "Invalid VFX delta");
    auto& p = *impl_->resources;
    p.time += seconds;
    p.manager->Update(seconds * 60);
}
void VfxPlayer::UpdateMatrix(float seconds, const DirectX::XMFLOAT4X4& world) {
    SetWorld(world);
    Update(seconds);
}
void VfxPlayer::Draw(UINT frame,ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection) {
    auto& p=*impl_->resources; p.drawCalls = 0; if(p.manager->GetTotalInstanceCount() == 0)return;
    // 呼び出し元が対応するフレームのFenceを待機済み。別フレームの領域を再利用しない。
    p.pools[frame]->NewFrame();EffekseerRendererDX12::BeginCommandList(p.lists[frame],commands);p.renderer->SetCommandList(p.lists[frame]);
    Effekseer::Matrix44 v,proj;std::memcpy(v.Values,&view,sizeof(view));std::memcpy(proj.Values,&projection,sizeof(projection));
    p.renderer->SetCameraMatrix(v);p.renderer->SetProjectionMatrix(proj);p.renderer->SetTime(p.time);
    p.renderer->ResetDrawCallCount();p.renderer->ResetDrawVertexCount();
    Need(p.renderer->BeginRendering(),"Effekseer BeginRendering failed");
    Effekseer::Manager::DrawParameter draw;draw.ZNear=0;draw.ZFar=1;draw.ViewProjectionMatrix=p.renderer->GetCameraProjectionMatrix();p.manager->Draw(draw);
    Need(p.renderer->EndRendering(),"Effekseer EndRendering failed");p.drawCalls = p.renderer->GetDrawCallCount();p.renderer->SetCommandList(nullptr);EffekseerRendererDX12::EndCommandList(p.lists[frame]);
}
const std::string& VfxPlayer::Status() const { return impl_->resources->status; }
int VfxPlayer::Instances() const {
    return static_cast<int>(std::count_if(impl_->handles.begin(), impl_->handles.end(),
        [&](auto handle) { return impl_->resources->manager->Exists(handle); }));
}
int VfxPlayer::Particles() const {
    int count = 0;
    for (auto handle : impl_->handles) count += impl_->resources->manager->GetInstanceCount(handle);
    return count;
}
int VfxPlayer::SharedParticles() const { return impl_->resources->manager->GetTotalInstanceCount(); }
int VfxPlayer::AvailableParticles() const { return impl_->resources->manager->GetRestInstancesCount(); }
int VfxPlayer::DrawCalls() const { return impl_->resources->drawCalls; }
}
