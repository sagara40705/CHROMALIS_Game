#pragma once
#include "assets/SkeletalAsset.h"
#include <memory>

namespace chromalis {
struct Playback {
    int clip=0;
    float time=0, speed=1;
    bool playing=true, loop=true, restPose=false, bindPose=false;
};
// 時刻と姿勢はこのインスタンスが所有する。モデルのWorld位置には触れない。
class AnimationPlayer final {
public:
    explicit AnimationPlayer(SkeletalAsset asset);
    ~AnimationPlayer();
    AnimationPlayer(const AnimationPlayer&)=delete;
    AnimationPlayer& operator=(const AnimationPlayer&)=delete;
    void Update(float seconds);
    void Evaluate();
    void Stop();
    Playback playback;
    const SkeletalAsset& Asset() const;
    const std::vector<DirectX::XMFLOAT4X4>& Palette() const;
    const std::vector<DirectX::XMFLOAT4X4>& Normals() const;
    DirectX::XMFLOAT3 Socket(size_t joint, const DirectX::XMFLOAT4X4& world) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
