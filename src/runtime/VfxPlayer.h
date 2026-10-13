#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace chromalis {
// 演出の時刻とハンドルを管理する。Gameplayの命中・ダメージには依存しない。
class VfxPlayer final {
public:
    VfxPlayer(ID3D12Device* device,ID3D12CommandQueue* queue,DXGI_FORMAT format);
    ~VfxPlayer();
    bool Reload(const std::filesystem::path& path);
    void Play(int count);
    void Stop();
    void UpdateMatrix(float seconds, const DirectX::XMFLOAT4X4& world);
    void Draw(UINT frame,ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection);
    const std::string& Status() const;
    int Instances() const;
    int Particles() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
