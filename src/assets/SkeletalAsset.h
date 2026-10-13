#pragma once
#include "assets/StaticAsset.h"
#include <DirectXMath.h>

namespace chromalis {
// 静的形式は変更せず、階層とローカル空間のメッシュを別のコンテナーに格納する。
struct JointTransform {
    std::array<float,3> translation{0,0,0}, scale{1,1,1};
    std::array<float,4> rotation{0,0,0,1};
};
struct SkeletonJoint { std::string name; int32_t parent = -1; JointTransform rest; };
struct SkinVertex { std::array<uint32_t,4> joints{}; std::array<float,4> weights{1,0,0,0}; };
struct SkinBinding { uint32_t joint = 0; DirectX::XMFLOAT4X4 inverseBind{}; };
struct AnimationKey { float time = 0; std::array<float,4> value{}; };
// property: 0=位置、1=回転、2=スケール。初期経路はLINEARのみを受理する。
struct AnimationChannel { uint32_t joint = 0, property = 0; std::vector<AnimationKey> keys; };
struct AnimationClip { std::string name; float duration = 0; std::vector<AnimationChannel> channels; };
// 共通SkeletonとClip群をモデルから分離する。描画用メッシュやInverse Bindは含めない。
struct AnimationLibrary {
    std::string id;
    std::vector<SkeletonJoint> joints;
    std::vector<AnimationClip> clips;
    std::vector<AssetDependency> dependencies;
};
void ValidateAnimationLibrary(const AnimationLibrary& library);
AnimationLibrary LoadAnimationLibrary(const std::filesystem::path& path);
void SaveAnimationLibrary(const AnimationLibrary& library, const std::filesystem::path& path);
struct SkeletalAsset {
    StaticAsset mesh;
    std::vector<SkeletonJoint> joints;
    std::vector<SkinBinding> bindings;
    std::vector<SkinVertex> skin;
    std::vector<AnimationClip> clips;
};
void ValidateSkeletalAsset(const SkeletalAsset& asset);
SkeletalAsset LoadSkeletalAsset(const std::filesystem::path& path);
void SaveSkeletalAsset(const SkeletalAsset& asset, const std::filesystem::path& path);
}
