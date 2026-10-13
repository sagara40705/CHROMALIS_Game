#include "cooker/CookStatic.h"
#include "animation/AnimationPlayer.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace chromalis;
using namespace DirectX;
void Check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F> void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected,"Invalid animation accepted");}
bool Near(float a,float b){return std::abs(a-b)<0.005f;}
void PlaybackTransitions(SkeletalAsset asset) {
    // 移動値が分かる三つのClipで、両側の時計と割り込み時の連続性を確認する。
    asset.clips.clear();
    for (int i = 0; i < 3; ++i) {
        const float offset = static_cast<float>(i) * 10;
        asset.clips.push_back({"clip" + std::to_string(i), 1, {{0, 0, {{0, {offset,0,0,0}}, {1, {offset+2,0,0,0}}}}}});
    }
    AnimationPlayer player(asset), independent(asset);
    XMFLOAT4X4 identity; XMStoreFloat4x4(&identity, XMMatrixIdentity());
    const auto x = [&] { return player.Socket(0, identity).x; };
    player.Play(0, 0.2f); player.CrossFade(1, 1, 0.1f);
    Check(Near(x(), 0.4f) && player.Transitioning(), "Crossfade start jumped");
    player.Update(0.25f);
    Check(Near(x(), 3.35f) && Near(player.TransitionProgress(), 0.25f), "Crossfade did not advance both clips");
    const auto before = player.Palette();
    player.CrossFade(2, 0.4f);
    for (size_t i = 0; i < before.size(); ++i) for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
        Check(Near(before[i].m[r][c], player.Palette()[i].m[r][c]), "Interrupted blend discontinuity");
    player.Update(0.2f); Check(Near(x(), 11.875f), "Interrupted blend target incorrect");
    player.Pause(); player.Update(1); Check(Near(x(), 11.875f) && Near(player.TransitionProgress(), 0.5f), "Pause advanced transition");
    player.Resume(); player.Update(0.2f); Check(!player.Transitioning() && Near(x(),20.8f), "Transition did not complete");
    Check(independent.playback.clip == 0 && independent.playback.time == 0 && !independent.Transitioning(), "Transition state shared between instances");
    player.CrossFade(0, 1); player.Seek(0.3f);
    Check(!player.Transitioning() && Near(x(), 0.6f), "Seek did not cancel transition");
    player.CrossFade(1, 1); player.Stop();
    Check(!player.Transitioning() && !player.playback.playing && player.playback.time == 0 && Near(x(),10), "Stop did not reset target");
    player.CrossFade(2, 0); Check(!player.Transitioning() && Near(x(),20), "Zero-duration switch failed");
    const auto saved = x();
    Reject([&] { player.CrossFade(-1, 0.2f); });
    Reject([&] { player.CrossFade(0, -1); });
    Reject([&] { player.CrossFade(0, std::numeric_limits<float>::quiet_NaN()); });
    Reject([&] { player.Play(0, 2); });
    Reject([&] { player.Seek(std::numeric_limits<float>::infinity()); });
    Check(Near(x(), saved) && player.playback.clip == 2, "Invalid request changed playback");
    player.playback.loop = false; player.playback.speed = -1; player.Play(0, 1); player.Update(1);
    Check(!player.playback.playing && player.playback.time == 0, "Reverse terminal did not stop");
    player.playback.loop = true; player.Play(0, 0.1f); player.Update(0.2f);
    Check(Near(player.playback.time, 0.9f), "Reverse loop failed");
    player.playback.speed = 0; player.Update(1); Check(Near(player.playback.time,0.9f), "Zero speed moved clip");
    player.playback.loop = false; player.playback.speed = 1; player.CrossFade(1, 2, 0.9f); player.Update(0.2f);
    Check(!player.playback.playing && !player.Transitioning() && Near(x(),12), "Nonloop end did not settle target");
    player.playback.restPose = true; player.Evaluate(); const auto restX = x(); player.CrossFade(0, 1);
    Check(Near(x(),restX), "Rest-pose blend jumped");
    player.playback.bindPose = true; player.Evaluate(); player.CrossFade(1, 1);
    Check(!player.playback.bindPose && !player.Transitioning(), "Bind preview did not exit safely");
    player.playback.loop = true; player.playback.speed = std::numeric_limits<float>::max(); player.Update(std::numeric_limits<float>::max());
    Check(std::isfinite(player.playback.time), "Large time advance overflowed");
    XMFLOAT4X4 world; XMStoreFloat4x4(&world, XMMatrixScaling(2,3,4) * XMMatrixRotationY(0.7f) * XMMatrixTranslation(5,6,7));
    const auto matrix = player.SocketTransform(1, world); const auto position = player.Socket(1, world);
    Check(Near(matrix._41,position.x) && Near(matrix._42,position.y) && Near(matrix._43,position.z), "Socket matrix/position mismatch");
    Check(std::abs(matrix._13) > 0.1f && Near(matrix._22,3), "Socket orientation/scale missing");
    asset.clips[1].channels.push_back({0, 1, {{0, {0,0,0.70710678f,0.70710678f}}}});
    asset.clips[1].channels.push_back({0, 2, {{0, {2,2,2,0}}}});
    AnimationPlayer trs(asset); trs.CrossFade(1, 1); trs.Update(0.5f);
    const auto mixed = trs.SocketTransform(0, identity);
    Check(Near(mixed._11,1.5f*std::sqrt(0.5f)) && Near(mixed._12,1.5f*std::sqrt(0.5f)), "Quaternion/scale blend incorrect");
}
void SharedAnimation(const std::filesystem::path& source, const std::filesystem::path& output, const SkeletalAsset& model) {
    const auto path = output.parent_path() / "common.canim";
    auto library = CookAnimation(source, "characters/common");
    SaveAnimationLibrary(library, path);
    auto rig = std::make_shared<const AnimationRig>(LoadAnimationLibrary(path));
    auto firstModel = std::make_shared<const SkeletalAsset>(model);
    auto variant = model;
    variant.mesh.id = "characters/variant";
    variant.clips.clear();
    for (auto& vertex : variant.mesh.vertices) vertex.position[0] *= 1.1f;
    SaveSkeletalAsset(variant, output.parent_path() / "variant.cskin");
    auto secondModel = std::make_shared<const SkeletalAsset>(LoadSkeletalAsset(output.parent_path() / "variant.cskin"));
    AnimationPlayer first(firstModel, rig), second(secondModel, rig);
    Check(first.Rig() == second.Rig(), "Common rig was duplicated");
    second.playback.time = library.clips.front().duration * 0.5f;
    second.Evaluate();
    Check(first.playback.time == 0, "Playback time leaked between characters");
    bool differs = false;
    for (size_t i = 0; i < first.Palette().size(); ++i)
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
            differs |= !Near(first.Palette()[i].m[r][c], second.Palette()[i].m[r][c]);
    Check(differs, "Shared rig did not produce independent poses");
    second.playback.playing = false;
    second.playback.time = second.Clips()[0].duration;
    second.Update(0);
    Check(second.playback.time == second.Clips()[0].duration, "Paused endpoint wrapped to start");
    AnimationPlayer rest(secondModel, {});
    Check(rest.Clips()[0].name == "Rest", "Model-only rest preview failed");

    // 共通Skeletonの互換性を骨名・親・基準姿勢で確認する。
    auto incompatible = variant;
    incompatible.joints[0].name += "-changed";
    Reject([&] { AnimationPlayer rejected(std::make_shared<const SkeletalAsset>(incompatible), rig); });
    incompatible = variant;
    incompatible.joints[0].rest.translation[0] += 1;
    Reject([&] { AnimationPlayer rejected(std::make_shared<const SkeletalAsset>(incompatible), rig); });
    auto bad = library;
    bad.joints.back().name = bad.joints.front().name;
    Reject([&] { ValidateAnimationLibrary(bad); });
    bad = library; bad.clips.push_back(bad.clips[0]);
    Reject([&] { ValidateAnimationLibrary(bad); });
    const auto good = ReadAssetBytes(path);
    bad = library; bad.joints[0].parent = 0;
    Reject([&] { SaveAnimationLibrary(bad, path); });
    Check(ReadAssetBytes(path) == good, "Invalid library replaced good output");
    const auto malformed = output.parent_path() / "invalid.canim";
    auto bytes = good; bytes[4] = 99;
    { std::ofstream file(malformed, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    Reject([&] { (void)LoadAnimationLibrary(malformed); });
    { std::ofstream file(malformed, std::ios::binary); file.write(reinterpret_cast<const char*>(good.data()), static_cast<std::streamsize>(good.size() - 1)); }
    Reject([&] { (void)LoadAnimationLibrary(malformed); });

    // メッシュを含まないDCC出力も、同じ変換経路で取り込む。
    const auto animationOnly = output.parent_path() / "animation-only.gltf";
    const float keys[] = {0, 1, 0, 0, 0, 2, 0, 0};
    { std::ofstream file(output.parent_path() / "animation-only.bin", std::ios::binary); file.write(reinterpret_cast<const char*>(keys), sizeof(keys)); }
    { std::ofstream file(animationOnly); file << R"({"asset":{"version":"2.0"},"nodes":[{"name":"root"}],"buffers":[{"uri":"animation-only.bin","byteLength":32}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"}],"animations":[{"name":"move","samplers":[{"input":0,"output":1,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}]})"; }
    const auto animation = CookAnimation(animationOnly, "characters/animation-only");
    Check(animation.joints.size() == 1 && animation.clips.size() == 1 && Near(animation.clips[0].channels[0].keys[1].value[0], 2), "Animation-only glTF failed");
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==3,"Expected source and output");const std::filesystem::path output=argv[2];
        auto asset=CookSkeletal(argv[1],"validation/rigged-figure");SaveSkeletalAsset(asset,output);
        SharedAnimation(argv[1], output, asset);
        AnimationPlayer external(LoadSkeletalAsset(output));external.playback.bindPose=true;external.Evaluate();
        const auto rest=external.Palette();
        // Rest Poseとは区別し、Inverse Bindから復元した姿勢を検証する。
        for(const auto& matrix:rest)for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c)
            Check(Near(matrix.m[r][c],r==c?1.0f:0.0f),"Bind palette is not identity");
        external.playback.bindPose=false;external.playback.time=asset.clips[0].duration*0.5f;external.Evaluate();
        bool differs=false;for(size_t i=0;i<rest.size();++i)for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c)differs|=!Near(rest[i].m[r][c],external.Palette()[i].m[r][c]);
        Check(differs,"External animation does not deform skeleton");
        auto test=asset;test.joints={{"root",-1,{}},{"socket",0,{}}};test.joints[0].rest.translation={1,0,0};test.joints[1].rest.translation={0,2,0};
        test.bindings.resize(1);test.bindings[0].joint=1;XMStoreFloat4x4(&test.bindings[0].inverseBind,XMMatrixIdentity());
        for(auto& v:test.skin)v=SkinVertex{};
        AnimationClip clip;clip.name="move";clip.duration=1;
        clip.channels.push_back({0,0,{{0,{1,0,0,0}},{1,{3,0,0,0}}}});
        clip.channels.push_back({1,1,{{0,{0,0,0,1}},{1,{0,0,0.70710678f,0.70710678f}}}});
        clip.channels.push_back({1,2,{{0,{1,1,1,0}},{1,{2,2,2,0}}}});
        test.clips={clip};
        test.clips.push_back(test.clips[0]);test.clips[1].name="second";
        PlaybackTransitions(test);
        AnimationPlayer player(test);XMFLOAT4X4 world;XMStoreFloat4x4(&world,XMMatrixScaling(2,2,2)*XMMatrixTranslation(10,0,0));
        player.playback.restPose=true;player.Evaluate();auto socket=player.Socket(1,world);Check(Near(socket.x,12)&&Near(socket.y,4),"Rest/socket/world mismatch");
        player.playback.restPose=false;player.playback.time=0.5f;player.Evaluate();socket=player.Socket(1,world);Check(Near(socket.x,14)&&Near(socket.y,4),"Sampled hierarchy/socket mismatch");
        const auto& m=player.Palette()[0];Check(Near(m._11,1.5f*std::sqrt(0.5f)) && Near(m._12,1.5f*std::sqrt(0.5f)),"Rotation/scale sample mismatch");
        player.playback.time=0.9f;player.Update(0.2f);Check(Near(player.playback.time,0.1f),"Loop failed");
        player.playback.playing=false;player.Update(0.5f);Check(Near(player.playback.time,0.1f),"Pause failed");
        player.playback.playing=true;player.playback.speed=2;player.Update(0.2f);Check(Near(player.playback.time,0.5f),"Speed/resume failed");
        player.Stop();Check(!player.playback.playing&&player.playback.time==0,"Stop failed");
        player.playback.loop=false;player.playback.playing=true;player.Update(1);Check(!player.playback.playing&&player.playback.time==1,"Nonloop clamp failed");
        player.playback.clip=1;player.Evaluate();
        const auto good=ReadAssetBytes(output);
        // 画素テスト用。骨の移動だけで四隅の位置を変え、VSのスキニング漏れを検出する。
        SkeletalAsset gpu;gpu.mesh.id="test/skinning";gpu.joints={{"root",-1,{}}};gpu.bindings.resize(1);
        XMStoreFloat4x4(&gpu.bindings[0].inverseBind,XMMatrixIdentity());
        gpu.mesh.vertices={{{-1,1,-1},{0,0,-1},{0,0}},{{1,1,-1},{0,0,-1},{1,0}},{{1,-1,-1},{0,0,-1},{1,1}},{{-1,-1,-1},{0,0,-1},{0,1}}};
        gpu.mesh.indices={0,1,2,0,2,3};gpu.mesh.parts={{0,6,0}};gpu.mesh.materials.emplace_back();gpu.mesh.materials[0].nearest=1;gpu.mesh.materials[0].unlit=1;
        gpu.mesh.textures={{2,2,{255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255}}};gpu.skin.resize(4);
        gpu.clips={{"translate",1,{{0,0,{{0,{0,0,0,0}},{1,{1,0,0,0}}}}}}};
        SaveSkeletalAsset(gpu,output.parent_path()/"skinning.cskin");
        // 骨番号が異なっても、同じ名前と親子関係なら共通Clipを再利用する。
        auto ordered = gpu;
        ordered.joints = {{"root", -1, {}}, {"left", 0, {}}, {"right", 0, {}}};
        ordered.bindings[0].joint = 1;
        ordered.clips[0].channels[0].joint = 1;
        auto sharedRig = std::make_shared<const AnimationRig>(ordered);
        auto reordered = ordered;
        std::swap(reordered.joints[1], reordered.joints[2]);
        reordered.bindings[0].joint = 2; reordered.clips.clear();
        AnimationPlayer mapped(std::make_shared<const SkeletalAsset>(reordered), sharedRig);
        mapped.playback.time = 0.5f; mapped.Evaluate();
        Check(Near(mapped.Palette()[0]._41, 0.5f), "Joint name remapping failed");
        reordered.joints[2].parent = 1;
        Reject([&] { AnimationPlayer invalid(std::make_shared<const SkeletalAsset>(reordered), sharedRig); });
        auto bad=test;bad.joints[0].parent=1;Reject([&]{SaveSkeletalAsset(bad,output);});Check(good==ReadAssetBytes(output),"Failed save changed good package");
        bad=test;bad.skin[0].joints[0]=99;Reject([&]{ValidateSkeletalAsset(bad);});
        bad=test;bad.skin[0].weights[0]=0;Reject([&]{ValidateSkeletalAsset(bad);});
        bad=test;bad.clips[0].channels[0].keys[1].time=0;Reject([&]{ValidateSkeletalAsset(bad);});
        bad=test;bad.joints[1].rest.scale[0]=0;Reject([&]{ValidateSkeletalAsset(bad);});
        const auto malformed=output.parent_path()/"invalid.cskin";
        auto unknownVersion=good;unknownVersion[4]=99;
        {std::ofstream f(malformed,std::ios::binary);f.write(reinterpret_cast<const char*>(unknownVersion.data()),static_cast<std::streamsize>(unknownVersion.size()));}
        Reject([&]{(void)LoadSkeletalAsset(malformed);});
        for(size_t n:{size_t(4),good.size()-1}){{std::ofstream f(malformed,std::ios::binary);f.write(reinterpret_cast<const char*>(good.data()),static_cast<std::streamsize>(n));}Reject([&]{(void)LoadSkeletalAsset(malformed);});}
        std::cout<<"PASS: external glTF, versioned roundtrip, rest/sample/TRS/socket, playback, invalid assets, retained output\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
