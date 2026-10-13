#include "animation/AnimationPlayer.h"
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/maths/soa_transform.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace chromalis {
using namespace DirectX;
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
ozz::math::Transform Transform(const JointTransform& t) {
    return {{t.translation[0],t.translation[1],t.translation[2]},
        {t.rotation[0],t.rotation[1],t.rotation[2],t.rotation[3]},
        {t.scale[0],t.scale[1],t.scale[2]}};
}
}
struct AnimationPlayer::Impl {
    SkeletalAsset asset;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<ozz::unique_ptr<ozz::animation::Animation>> clips;
    ozz::animation::SamplingJob::Context context;
    std::vector<ozz::math::SoaTransform> locals;
    std::vector<ozz::math::Float4x4> models;
    std::vector<ozz::math::Float4x4> bindModels;
    std::vector<XMFLOAT4X4> palette,normals;
};
AnimationPlayer::AnimationPlayer(SkeletalAsset asset):impl_(std::make_unique<Impl>()) {
    ValidateSkeletalAsset(asset);impl_->asset=std::move(asset);auto& p=*impl_;const auto& a=p.asset;
    ozz::animation::offline::RawSkeleton raw;
    std::function<void(int32_t,ozz::animation::offline::RawSkeleton::Joint::Children&)> build;
    build=[&](int32_t parent,auto& children){for(size_t i=0;i<a.joints.size();++i)if(a.joints[i].parent==parent){children.emplace_back();auto& j=children.back();j.name=a.joints[i].name.c_str();j.transform=Transform(a.joints[i].rest);build(static_cast<int32_t>(i),j.children);}};
    build(-1,raw.roots);p.skeleton=ozz::animation::offline::SkeletonBuilder{}(raw);Need(bool(p.skeleton),"ozz skeleton build failed");
    for(size_t i=0;i<a.joints.size();++i)Need(p.skeleton->joint_parents()[i]==a.joints[i].parent,"ozz joint ordering mismatch");
    for(const auto& c:a.clips) {
        ozz::animation::offline::RawAnimation animation;animation.name=c.name.c_str();animation.duration=c.duration;animation.tracks.resize(a.joints.size());
        for(const auto& channel:c.channels) {
            auto& track=animation.tracks[channel.joint];
            for(const auto& k:channel.keys) {
                if(channel.property==0)track.translations.push_back({k.time,{k.value[0],k.value[1],k.value[2]}});
                if(channel.property==1)track.rotations.push_back({k.time,{k.value[0],k.value[1],k.value[2],k.value[3]}});
                if(channel.property==2)track.scales.push_back({k.time,{k.value[0],k.value[1],k.value[2]}});
            }
        }
        for(size_t i=0;i<a.joints.size();++i) {
            auto& t=animation.tracks[i];const auto rest=Transform(a.joints[i].rest);
            if(t.translations.empty())t.translations.push_back({0,rest.translation});
            if(t.rotations.empty())t.rotations.push_back({0,rest.rotation});
            if(t.scales.empty())t.scales.push_back({0,rest.scale});
        }
        auto clip=ozz::animation::offline::AnimationBuilder{}(animation);Need(bool(clip),"ozz animation build failed");p.clips.push_back(std::move(clip));
    }
    p.context.Resize(p.skeleton->num_joints());p.locals.resize(p.skeleton->num_soa_joints());p.models.resize(a.joints.size());p.palette.resize(a.bindings.size());p.normals.resize(a.bindings.size());
    p.bindModels.resize(a.joints.size());
    // Skin JointはInverse Bindから復元する。それ以外の補助ノードは親とRest TRSから補完する。
    for(size_t i=0;i<a.joints.size();++i) {
        const auto binding=std::find_if(a.bindings.begin(),a.bindings.end(),[&](const auto& b){return b.joint==i;});
        XMMATRIX model;
        if(binding!=a.bindings.end())model=XMMatrixInverse(nullptr,XMLoadFloat4x4(&binding->inverseBind));
        else {
            const auto& j=a.joints[i];const auto& t=j.rest;
            model=XMMatrixScaling(t.scale[0],t.scale[1],t.scale[2])*XMMatrixRotationQuaternion(XMVectorSet(t.rotation[0],t.rotation[1],t.rotation[2],t.rotation[3]))*XMMatrixTranslation(t.translation[0],t.translation[1],t.translation[2]);
            if(j.parent>=0){XMFLOAT4X4 parent;std::memcpy(&parent,&p.bindModels[j.parent],sizeof(parent));model*=XMLoadFloat4x4(&parent);}
        }
        XMFLOAT4X4 stored;XMStoreFloat4x4(&stored,model);std::memcpy(&p.bindModels[i],&stored,sizeof(stored));
    }
    Evaluate();
}
AnimationPlayer::~AnimationPlayer()=default;
const SkeletalAsset& AnimationPlayer::Asset() const{return impl_->asset;}
const std::vector<XMFLOAT4X4>& AnimationPlayer::Palette() const{return impl_->palette;}
const std::vector<XMFLOAT4X4>& AnimationPlayer::Normals() const{return impl_->normals;}
void AnimationPlayer::Stop(){playback.playing=false;playback.time=0;Evaluate();}
void AnimationPlayer::Update(float seconds) {
    Need(std::isfinite(seconds) && seconds>=0 && std::isfinite(playback.speed),"Invalid animation delta/speed");
    Need(playback.clip>=0 && static_cast<size_t>(playback.clip)<impl_->clips.size(),"Animation clip out of range");
    if(playback.playing)playback.time+=seconds*playback.speed;
    const float duration=impl_->asset.clips[playback.clip].duration;
    if(playback.loop){playback.time=std::fmod(playback.time,duration);if(playback.time<0)playback.time+=duration;}
    else {if(playback.time>=duration || playback.time<0)playback.playing=false;playback.time=std::clamp(playback.time,0.0f,duration);}
    Evaluate();
}
void AnimationPlayer::Evaluate() {
    auto& p=*impl_;Need(playback.clip>=0 && static_cast<size_t>(playback.clip)<p.clips.size() && std::isfinite(playback.time),"Invalid playback state");
    if(playback.bindPose)p.models=p.bindModels;
    else {
    if(playback.restPose)std::copy(p.skeleton->joint_rest_poses().begin(),p.skeleton->joint_rest_poses().end(),p.locals.begin());
    else {
        ozz::animation::SamplingJob sample;sample.animation=p.clips[playback.clip].get();sample.context=&p.context;
        sample.ratio=std::clamp(playback.time/p.asset.clips[playback.clip].duration,0.0f,1.0f);sample.output=ozz::make_span(p.locals);Need(sample.Run(),"ozz sampling failed");
    }
    ozz::animation::LocalToModelJob hierarchy;hierarchy.skeleton=p.skeleton.get();hierarchy.input=ozz::make_span(p.locals);hierarchy.output=ozz::make_span(p.models);Need(hierarchy.Run(),"ozz hierarchy evaluation failed");
    }
    for(size_t i=0;i<p.palette.size();++i) {
        XMFLOAT4X4 model;std::memcpy(&model,&p.models[p.asset.bindings[i].joint],sizeof(model));
        const auto skin=XMLoadFloat4x4(&p.asset.bindings[i].inverseBind)*XMLoadFloat4x4(&model);
        XMStoreFloat4x4(&p.palette[i],skin);XMStoreFloat4x4(&p.normals[i],XMMatrixTranspose(XMMatrixInverse(nullptr,skin)));
        for(const auto& row:p.palette[i].m)for(float f:row)Need(std::isfinite(f),"Nonfinite evaluated skin matrix");
        for(const auto& row:p.normals[i].m)for(float f:row)Need(std::isfinite(f),"Singular evaluated normal matrix");
    }
}
XMFLOAT3 AnimationPlayer::Socket(size_t joint,const XMFLOAT4X4& world) const {
    const auto& m=impl_->models.at(joint);XMFLOAT4X4 model;std::memcpy(&model,&m,sizeof(model));XMFLOAT3 result;
    XMStoreFloat3(&result,XMVector3TransformCoord(XMVectorZero(),XMLoadFloat4x4(&model)*XMLoadFloat4x4(&world)));return result;
}
}
