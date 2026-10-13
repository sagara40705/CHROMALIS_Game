#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace chromalis {
struct VfxSettings {
    std::array<float,3> position{0,0,0}, rotation{0,0,0};
    float scale=0.3f;
    int count=1, joint=0;
    bool followSocket=false;
};
// 演出の時刻とハンドルを管理する。Gameplayの命中・ダメージには依存しない。
class VfxPlayer final {
public:
    VfxPlayer(ID3D12Device* device,ID3D12CommandQueue* queue,DXGI_FORMAT format);
    ~VfxPlayer();
    bool Reload(const std::filesystem::path& path);
    void Play(int count);
    void Stop();
    void Update(float seconds,const VfxSettings& settings);
    void Draw(UINT frame,ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection);
    const std::string& Status() const;
    int Instances() const;
    int Particles() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
