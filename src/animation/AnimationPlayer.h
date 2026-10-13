#pragma once
#include "assets/SkeletalAsset.h"
#include <memory>

namespace chromalis {
// 不変のSkeletonと圧縮Clipを共有し、再生時刻・姿勢は個体が所有する。
class AnimationRig final {
public:
    explicit AnimationRig(AnimationLibrary library);
    explicit AnimationRig(const SkeletalAsset& model);
    ~AnimationRig();
    const AnimationLibrary& Source() const;
private:
    friend class AnimationPlayer;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct Playback {
    int clip=0;
    float time=0, speed=1;
    bool playing=true, loop=true, restPose=false, bindPose=false;
};
// 時刻と姿勢はこのインスタンスが所有する。モデルのWorld位置には触れない。
class AnimationPlayer final {
public:
    explicit AnimationPlayer(SkeletalAsset asset);
    AnimationPlayer(std::shared_ptr<const SkeletalAsset> asset, std::shared_ptr<const AnimationRig> rig);
    ~AnimationPlayer();
    AnimationPlayer(const AnimationPlayer&)=delete;
    AnimationPlayer& operator=(const AnimationPlayer&)=delete;
    void Update(float seconds);
    void Evaluate();
    void Play(int clip, float startTime = 0);
    void CrossFade(int clip, float duration, float startTime = 0);
    void Pause();
    void Resume();
    void Seek(float time);
    void Stop();
    bool Transitioning() const;
    float TransitionProgress() const;
    Playback playback;
    const SkeletalAsset& Asset() const;
    const std::vector<AnimationClip>& Clips() const;
    const std::shared_ptr<const AnimationRig>& Rig() const;
    const std::vector<DirectX::XMFLOAT4X4>& Palette() const;
    const std::vector<DirectX::XMFLOAT4X4>& Normals() const;
    DirectX::XMFLOAT3 Socket(size_t joint, const DirectX::XMFLOAT4X4& world) const;
    DirectX::XMFLOAT4X4 SocketTransform(size_t joint, const DirectX::XMFLOAT4X4& world) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
