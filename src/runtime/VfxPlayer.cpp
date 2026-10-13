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
struct VfxPlayer::Impl {
    EffekseerRenderer::RendererRef renderer;
    Effekseer::ManagerRef manager;
    Effekseer::EffectRef effect;
    std::array<Effekseer::RefPtr<EffekseerRenderer::SingleFrameMemoryPool>,2> pools;
    std::array<Effekseer::RefPtr<EffekseerRenderer::CommandList>,2> lists;
    std::vector<Effekseer::Handle> handles;
    std::string status="No VFX loaded";
    float time=0;
};
VfxPlayer::VfxPlayer(ID3D12Device* device,ID3D12CommandQueue* queue,DXGI_FORMAT format):impl_(std::make_unique<Impl>()) {
    auto& p=*impl_;auto graphics=EffekseerRendererDX12::CreateGraphicsDevice(device,queue,2);Need(bool(graphics),"Effekseer DX12 device creation failed");
    p.renderer=EffekseerRendererDX12::Create(graphics,&format,1,DXGI_FORMAT_D32_FLOAT,false,4096);Need(bool(p.renderer),"Effekseer DX12 renderer creation failed");
    // 色テクスチャだけsRGBデコードし、法線・歪みなどのデータは線形で読む。
    p.manager=MakeManager(p.renderer);
    p.renderer->SetMaintainGammaColorInLinearColorSpace(false);
    for(size_t i=0;i<p.pools.size();++i){p.pools[i]=EffekseerRenderer::CreateSingleFrameMemoryPool(graphics);p.lists[i]=EffekseerRenderer::CreateCommandList(graphics,p.pools[i]);Need(bool(p.pools[i])&&bool(p.lists[i]),"Effekseer frame resource creation failed");}
}
VfxPlayer::~VfxPlayer()=default;
bool VfxPlayer::Reload(const std::filesystem::path& path) {
    auto& p=*impl_;
    try {
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
    auto& p=*impl_;Need(count>=1&&count<=64,"VFX preview count must be 1..64");if(!p.effect){p.status="Load an effect first";return;}
    Stop();for(int i=0;i<count;++i){auto h=p.manager->Play(p.effect,0,0,0);Need(h>=0,"VFX instance budget exhausted");p.handles.push_back(h);}
}
void VfxPlayer::Stop(){auto& p=*impl_;p.manager->StopAllEffects();p.manager->Update(0);p.handles.clear();}
void VfxPlayer::Update(float seconds,const VfxSettings& settings) {
    auto& p=*impl_;Need(std::isfinite(seconds)&&seconds>=0 && std::isfinite(settings.scale)&&settings.scale>0,"Invalid VFX time/scale");
    for(float v:settings.position)Need(std::isfinite(v),"Invalid VFX position");for(float v:settings.rotation)Need(std::isfinite(v),"Invalid VFX rotation");
    for(size_t i=0;i<p.handles.size();++i){auto h=p.handles[i];p.manager->SetLocation(h,settings.position[0]+static_cast<float>(i)*0.35f,settings.position[1],settings.position[2]);p.manager->SetRotation(h,settings.rotation[0],settings.rotation[1],settings.rotation[2]);p.manager->SetScale(h,settings.scale,settings.scale,settings.scale);}
    p.time+=seconds;p.manager->Update(seconds*60);std::erase_if(p.handles,[&](auto h){return !p.manager->Exists(h);});
}
void VfxPlayer::Draw(UINT frame,ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection) {
    auto& p=*impl_;if(p.handles.empty())return;
    // 呼び出し元が対応するフレームのFenceを待機済み。別フレームの領域を再利用しない。
    p.pools[frame]->NewFrame();EffekseerRendererDX12::BeginCommandList(p.lists[frame],commands);p.renderer->SetCommandList(p.lists[frame]);
    Effekseer::Matrix44 v,proj;std::memcpy(v.Values,&view,sizeof(view));std::memcpy(proj.Values,&projection,sizeof(projection));
    p.renderer->SetCameraMatrix(v);p.renderer->SetProjectionMatrix(proj);p.renderer->SetTime(p.time);
    p.renderer->ResetDrawCallCount();p.renderer->ResetDrawVertexCount();
    Need(p.renderer->BeginRendering(),"Effekseer BeginRendering failed");
    Effekseer::Manager::DrawParameter draw;draw.ZNear=0;draw.ZFar=1;draw.ViewProjectionMatrix=p.renderer->GetCameraProjectionMatrix();p.manager->Draw(draw);
    Need(p.renderer->EndRendering(),"Effekseer EndRendering failed");p.renderer->SetCommandList(nullptr);EffekseerRendererDX12::EndCommandList(p.lists[frame]);
}
const std::string& VfxPlayer::Status() const{return impl_->status;}
int VfxPlayer::Instances() const{return static_cast<int>(impl_->handles.size());}
int VfxPlayer::Particles() const{return impl_->manager->GetTotalInstanceCount();}
}
