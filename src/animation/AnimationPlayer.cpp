#include "animation/AnimationPlayer.h"
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/maths/soa_transform.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace chromalis {
using namespace DirectX;
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
XMMATRIX RestMatrix(const JointTransform& t) {
    return XMMatrixScaling(t.scale[0],t.scale[1],t.scale[2]) *
        XMMatrixRotationQuaternion(XMVectorSet(t.rotation[0],t.rotation[1],t.rotation[2],t.rotation[3])) *
        XMMatrixTranslation(t.translation[0],t.translation[1],t.translation[2]);
}
AnimationLibrary Embedded(const SkeletalAsset& asset) {
    ValidateSkeletalAsset(asset);
    AnimationLibrary library{asset.mesh.id, asset.joints, asset.clips, {}};
    // ClipのないSkinもRest姿勢でプレビューできる。
    if (library.clips.empty()) {
        const auto& t = library.joints.front().rest.translation;
        library.clips.push_back({"Rest", 1, {{0, 0, {{0, {t[0], t[1], t[2], 0}}}}}});
    }
    return library;
}
std::vector<uint32_t> MatchSkeleton(const SkeletalAsset& asset, const AnimationLibrary& library) {
    std::unordered_map<std::string, uint32_t> names;
    for (size_t i = 0; i < asset.joints.size(); ++i)
        if (!names.emplace(asset.joints[i].name, static_cast<uint32_t>(i)).second)
            throw std::runtime_error("Ambiguous model joint: " + asset.joints[i].name);
    std::vector<uint32_t> mapping(asset.joints.size(), UINT32_MAX);
    for (size_t i = 0; i < library.joints.size(); ++i) {
        const auto& joint = library.joints[i]; const auto found = names.find(joint.name);
        if (found == names.end()) throw std::runtime_error("Shared Skeleton joint missing: " + joint.name);
        const auto& model = asset.joints[found->second];
        const auto parent = joint.parent < 0 ? std::string{} : library.joints[joint.parent].name;
        const auto modelParent = model.parent < 0 ? std::string{} : asset.joints[model.parent].name;
        if (parent != modelParent) throw std::runtime_error("Shared Skeleton parent mismatch: " + joint.name);
        XMFLOAT4X4 expected, actual;
        XMStoreFloat4x4(&expected, RestMatrix(joint.rest)); XMStoreFloat4x4(&actual, RestMatrix(model.rest));
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
            if (std::abs(expected.m[r][c] - actual.m[r][c]) > 0.0001f * std::max(1.0f, std::abs(expected.m[r][c])))
                throw std::runtime_error("Shared Skeleton rest transform mismatch: " + joint.name);
        mapping[found->second] = static_cast<uint32_t>(i);
    }
    for (const auto& binding : asset.bindings)
        if (mapping[binding.joint] == UINT32_MAX) throw std::runtime_error("Skin joint absent from shared Skeleton: " + asset.joints[binding.joint].name);
    return mapping;
}
ozz::math::Transform Transform(const JointTransform& t) {
    return {{t.translation[0],t.translation[1],t.translation[2]},
        {t.rotation[0],t.rotation[1],t.rotation[2],t.rotation[3]},
        {t.scale[0],t.scale[1],t.scale[2]}};
}
}
struct AnimationRig::Impl {
    AnimationLibrary asset;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<ozz::unique_ptr<ozz::animation::Animation>> clips;
};
AnimationRig::AnimationRig(AnimationLibrary asset):impl_(std::make_unique<Impl>()) {
    ValidateAnimationLibrary(asset);
    impl_->asset = std::move(asset);
    auto& p = *impl_;
    const auto& a = p.asset;

    // 最初に共通Skeletonを構築し、保存した骨番号とozzの評価順を照合する。
    ozz::animation::offline::RawSkeleton raw;
    std::function<void(int32_t,ozz::animation::offline::RawSkeleton::Joint::Children&)> build;
    build = [&](int32_t parent, auto& children) {
        for (size_t i = 0; i < a.joints.size(); ++i) {
            if (a.joints[i].parent != parent) continue;
            children.emplace_back();
            auto& joint = children.back();
            joint.name = a.joints[i].name.c_str();
            joint.transform = Transform(a.joints[i].rest);
            build(static_cast<int32_t>(i), joint.children);
        }
    };
    build(-1, raw.roots);
    p.skeleton = ozz::animation::offline::SkeletonBuilder{}(raw);
    Need(bool(p.skeleton), "ozz skeleton build failed");
    for (size_t i = 0; i < a.joints.size(); ++i)
        Need(p.skeleton->joint_parents()[i] == a.joints[i].parent, "ozz joint ordering mismatch");

    // 未指定のチャンネルは共通Rest姿勢を使い、Clipごとに一度だけ圧縮する。
    for(const auto& c:a.clips) {
        ozz::animation::offline::RawAnimation animation;
        animation.name = c.name.c_str();
        animation.duration = c.duration;
        animation.tracks.resize(a.joints.size());
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
        auto clip = ozz::animation::offline::AnimationBuilder{}(animation);
        Need(bool(clip), "ozz animation build failed");
        p.clips.push_back(std::move(clip));
    }
}
AnimationRig::~AnimationRig() = default;
AnimationRig::AnimationRig(const SkeletalAsset& model) : AnimationRig(Embedded(model)) {}
const AnimationLibrary& AnimationRig::Source() const { return impl_->asset; }
struct AnimationPlayer::Impl {
    std::shared_ptr<const SkeletalAsset> asset;
    std::shared_ptr<const AnimationRig> rig;
    std::vector<uint32_t> mapping;
    std::vector<ozz::math::Float4x4> sampled;
    ozz::animation::SamplingJob::Context context;
    std::vector<ozz::math::SoaTransform> locals;
    std::vector<ozz::math::SoaTransform> fromLocals, targetLocals;
    ozz::animation::SamplingJob::Context fromContext;
    Playback fromPlayback;
    bool transitioning = false, frozenSource = false;
    int targetClip = 0;
    float elapsed = 0, duration = 0;
    std::vector<ozz::math::Float4x4> models;
    std::vector<ozz::math::Float4x4> bindModels;
    std::vector<XMFLOAT4X4> palette,normals;
};

AnimationPlayer::AnimationPlayer(SkeletalAsset asset) : AnimationPlayer(std::make_shared<const SkeletalAsset>(std::move(asset)), {}) {}
AnimationPlayer::AnimationPlayer(std::shared_ptr<const SkeletalAsset> asset, std::shared_ptr<const AnimationRig> rig) : impl_(std::make_unique<Impl>()) {
    Need(bool(asset), "Missing skeletal model"); ValidateSkeletalAsset(*asset);
    auto& p = *impl_; p.asset = std::move(asset); const auto& a = *p.asset;
    p.rig = rig ? std::move(rig) : std::make_shared<const AnimationRig>(Embedded(a));
    p.mapping = MatchSkeleton(a, p.rig->Source());
    const auto& skeleton = p.rig->impl_->skeleton;
    p.context.Resize(skeleton->num_joints());p.locals.resize(skeleton->num_soa_joints());p.sampled.resize(skeleton->num_joints());p.models.resize(a.joints.size());p.palette.resize(a.bindings.size());p.normals.resize(a.bindings.size());
    p.fromContext.Resize(skeleton->num_joints());
    p.fromLocals.resize(p.locals.size()); p.targetLocals.resize(p.locals.size());
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
const SkeletalAsset& AnimationPlayer::Asset() const{return *impl_->asset;}
const std::vector<AnimationClip>& AnimationPlayer::Clips() const { return impl_->rig->Source().clips; }
const std::shared_ptr<const AnimationRig>& AnimationPlayer::Rig() const { return impl_->rig; }
const std::vector<XMFLOAT4X4>& AnimationPlayer::Palette() const{return impl_->palette;}
const std::vector<XMFLOAT4X4>& AnimationPlayer::Normals() const{return impl_->normals;}
namespace {
void ValidatePlay(const std::vector<AnimationClip>& clips, int clip, float time) {
    Need(clip >= 0 && static_cast<size_t>(clip) < clips.size(), "Animation clip out of range");
    Need(std::isfinite(time) && time >= 0 && time <= clips[clip].duration, "Animation start time out of range");
}
void Advance(Playback& playback, float seconds, float duration) {
    if (!playback.playing) return;
    const double time = static_cast<double>(playback.time) + static_cast<double>(seconds) * playback.speed;
    if (playback.loop) {
        double wrapped = std::fmod(time, duration);
        if (wrapped < 0) wrapped += duration;
        playback.time = static_cast<float>(wrapped);
    } else {
        if ((playback.speed > 0 && time >= duration) || (playback.speed < 0 && time <= 0)) playback.playing = false;
        playback.time = static_cast<float>(std::clamp(time, 0.0, static_cast<double>(duration)));
    }
}
}
void AnimationPlayer::Play(int clip, float startTime) {
    ValidatePlay(Clips(), clip, startTime);
    impl_->transitioning = false;
    playback.clip = clip; playback.time = startTime; playback.playing = true;
    playback.restPose = playback.bindPose = false;
    Evaluate();
}
void AnimationPlayer::CrossFade(int clip, float duration, float startTime) {
    ValidatePlay(Clips(), clip, startTime);
    Need(std::isfinite(duration) && duration >= 0, "Invalid transition duration");
    // BindはInverse Bind由来のモデル空間姿勢なので、通常のLocal TRSブレンドには含めない。
    if (duration == 0 || playback.bindPose) { Play(clip, startTime); return; }
    Evaluate();
    auto& p = *impl_;
    // 割り込み時は直前の混合姿勢を保存し、姿勢を巻き戻さず次の遷移を開始する。
    p.frozenSource = p.transitioning || playback.restPose;
    p.fromLocals = p.locals; p.fromPlayback = playback;
    p.duration = duration; p.elapsed = 0; p.targetClip = clip; p.transitioning = true;
    playback.clip = clip; playback.time = startTime; playback.playing = true;
    playback.restPose = playback.bindPose = false;
    Evaluate();
}
void AnimationPlayer::Pause() { playback.playing = false; }
void AnimationPlayer::Resume() { playback.playing = true; }
void AnimationPlayer::Seek(float time) {
    ValidatePlay(Clips(), playback.clip, time);
    impl_->transitioning = false; playback.time = time; Evaluate();
}
void AnimationPlayer::Stop() { playback.playing = false; Seek(0); }
bool AnimationPlayer::Transitioning() const { return impl_->transitioning; }
float AnimationPlayer::TransitionProgress() const { return impl_->transitioning ? impl_->elapsed / impl_->duration : 1; }
void AnimationPlayer::Update(float seconds) {
    Need(std::isfinite(seconds) && seconds>=0 && std::isfinite(playback.speed),"Invalid animation delta/speed");
    Need(playback.clip>=0 && static_cast<size_t>(playback.clip)<impl_->rig->Source().clips.size(),"Animation clip out of range");
    Need(std::isfinite(playback.time), "Invalid animation time");
    auto& p = *impl_;
    const bool advancing = playback.playing;
    Advance(playback, seconds, Clips()[playback.clip].duration);
    if (p.transitioning && advancing) {
        if (!p.frozenSource) Advance(p.fromPlayback, seconds, Clips()[p.fromPlayback.clip].duration);
        p.elapsed = static_cast<float>(std::min(static_cast<double>(p.duration), static_cast<double>(p.elapsed) + seconds));
        // 非Loopの終端では目標姿勢を確定する。一時停止は遷移時間も停止する。
        if (p.elapsed >= p.duration || !playback.playing) p.transitioning = false;
    }
    Evaluate();
}
void AnimationPlayer::Evaluate() {
    auto& p=*impl_; const auto& rig = *p.rig->impl_; Need(playback.clip>=0 && static_cast<size_t>(playback.clip)<rig.clips.size() && std::isfinite(playback.time),"Invalid playback state");
    if (playback.bindPose || playback.restPose || playback.clip != p.targetClip) p.transitioning = false;
    if(playback.bindPose)p.models=p.bindModels;
    else {
    if(playback.restPose)std::copy(rig.skeleton->joint_rest_poses().begin(),rig.skeleton->joint_rest_poses().end(),p.locals.begin());
    else {
        ozz::animation::SamplingJob sample;sample.animation=rig.clips[playback.clip].get();sample.context=&p.context;
        sample.ratio=std::clamp(playback.time/rig.asset.clips[playback.clip].duration,0.0f,1.0f);sample.output=ozz::make_span(p.locals);Need(sample.Run(),"ozz sampling failed");
        if (p.transitioning) {
            if (!p.frozenSource) {
                ozz::animation::SamplingJob source;
                source.animation = rig.clips[p.fromPlayback.clip].get(); source.context = &p.fromContext;
                source.ratio = std::clamp(p.fromPlayback.time / rig.asset.clips[p.fromPlayback.clip].duration, 0.0f, 1.0f);
                source.output = ozz::make_span(p.fromLocals); Need(source.Run(), "ozz transition sampling failed");
            }
            p.targetLocals = p.locals;
            ozz::animation::BlendingJob::Layer layers[2];
            layers[0].weight = 1 - TransitionProgress(); layers[0].transform = ozz::make_span(p.fromLocals);
            layers[1].weight = TransitionProgress(); layers[1].transform = ozz::make_span(p.targetLocals);
            ozz::animation::BlendingJob blend;
            blend.layers = ozz::make_span(layers); blend.rest_pose = rig.skeleton->joint_rest_poses();
            blend.output = ozz::make_span(p.locals); Need(blend.Run(), "ozz transition blend failed");
        }
    }
    ozz::animation::LocalToModelJob hierarchy;hierarchy.skeleton=rig.skeleton.get();hierarchy.input=ozz::make_span(p.locals);hierarchy.output=ozz::make_span(p.sampled);Need(hierarchy.Run(),"ozz hierarchy evaluation failed");
    for (size_t i = 0; i < p.models.size(); ++i) {
        if (p.mapping[i] != UINT32_MAX) p.models[i] = p.sampled[p.mapping[i]];
        else {
            auto matrix = RestMatrix(p.asset->joints[i].rest);
            const auto parent = p.asset->joints[i].parent;
            if (parent >= 0) { XMFLOAT4X4 m; std::memcpy(&m, &p.models[parent], sizeof(m)); matrix *= XMLoadFloat4x4(&m); }
            XMFLOAT4X4 m; XMStoreFloat4x4(&m, matrix); std::memcpy(&p.models[i], &m, sizeof(m));
        }
    }
    }
    for(size_t i=0;i<p.palette.size();++i) {
        XMFLOAT4X4 model;std::memcpy(&model,&p.models[p.asset->bindings[i].joint],sizeof(model));
        const auto skin=XMLoadFloat4x4(&p.asset->bindings[i].inverseBind)*XMLoadFloat4x4(&model);
        XMStoreFloat4x4(&p.palette[i],skin);XMStoreFloat4x4(&p.normals[i],XMMatrixTranspose(XMMatrixInverse(nullptr,skin)));
        for(const auto& row:p.palette[i].m)for(float f:row)Need(std::isfinite(f),"Nonfinite evaluated skin matrix");
        for(const auto& row:p.normals[i].m)for(float f:row)Need(std::isfinite(f),"Singular evaluated normal matrix");
    }
}
XMFLOAT3 AnimationPlayer::Socket(size_t joint,const XMFLOAT4X4& world) const {
    const auto matrix = SocketTransform(joint, world);
    return {matrix._41, matrix._42, matrix._43};
}
XMFLOAT4X4 AnimationPlayer::SocketTransform(size_t joint, const XMFLOAT4X4& world) const {
    const auto& m = impl_->models.at(joint);
    XMFLOAT4X4 model, result; std::memcpy(&model, &m, sizeof(model));
    XMStoreFloat4x4(&result, XMLoadFloat4x4(&model) * XMLoadFloat4x4(&world));
    return result;
}
}
