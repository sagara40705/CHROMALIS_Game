#include "assets/SkeletalAsset.h"
#include <windows.h>
#include <bit>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace chromalis {
namespace {
void Need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<size_t N> void Finite(const std::array<float,N>& values) { for (float f : values) Need(std::isfinite(f),"Nonfinite skeletal value"); }
void Rotation(const std::array<float,4>& v) { Finite(v); float n=0; for(float f:v) n+=f*f; Need(std::abs(n-1)<0.001f,"Invalid unit quaternion"); }
struct Writer {
    std::vector<uint8_t> b;
    void U(uint32_t v) { for(int n=0;n<4;++n)b.push_back(static_cast<uint8_t>(v>>(8*n))); }
    void F(float v) { U(std::bit_cast<uint32_t>(v)); }
    void S(const std::string& s) { U(static_cast<uint32_t>(s.size())); b.insert(b.end(),s.begin(),s.end()); }
    template<size_t N> void A(const std::array<float,N>& a) { for(float f:a)F(f); }
};
struct Reader {
    std::span<const uint8_t> b; size_t p=0;
    std::span<const uint8_t> Take(size_t n) { Need(n<=b.size()-p,"Truncated skeletal asset"); auto s=b.subspan(p,n); p+=n; return s; }
    uint32_t U() { auto s=Take(4); return s[0]|uint32_t(s[1])<<8|uint32_t(s[2])<<16|uint32_t(s[3])<<24; }
    float F() { return std::bit_cast<float>(U()); }
    uint32_t Count(uint32_t max,size_t stride) { auto n=U(); Need(n<=max && n<=(b.size()-p)/stride,"Skeletal count exceeds bounds"); return n; }
    std::string S() { auto s=Take(Count(256,1)); return {reinterpret_cast<const char*>(s.data()),s.size()}; }
    template<size_t N> void A(std::array<float,N>& a) { for(float& f:a)f=F(); }
};
}
struct AnimationDataView {
    const std::vector<SkeletonJoint>& joints;
    const std::vector<AnimationClip>& clips;
};
static void ValidateAnimationData(AnimationDataView a) {
    Need(!a.joints.empty() && a.joints.size()<=256,"Skeleton requires 1..256 nodes");
    std::vector<int32_t> ancestors;
    for(size_t i=0;i<a.joints.size();++i) {
        const auto& j=a.joints[i];
        Need(j.parent>=-1 && j.parent<static_cast<int32_t>(i),"Skeleton must be parent-first and acyclic");
        // ozzの深さ優先順と一致させ、メッシュの参照番号を安定させる。
        while(!ancestors.empty() && ancestors.back()!=j.parent)ancestors.pop_back();
        Need(j.parent==-1 || !ancestors.empty(),"Skeleton must be depth-first ordered"); ancestors.push_back(static_cast<int32_t>(i));
        Need(!j.name.empty() && j.name.size()<=256,"Invalid joint name"); Finite(j.rest.translation); Rotation(j.rest.rotation); Finite(j.rest.scale);
        for(float s:j.rest.scale)Need(s>0,"Only positive joint scales supported in 0C");
    }
    Need(a.clips.size()<=64,"Expected at most 64 clips");
    size_t totalKeys=0;
    for(const auto& c:a.clips) {
        Need(!c.name.empty() && c.name.size()<=256 && std::isfinite(c.duration) && c.duration>0 && c.duration<=3600,"Invalid clip duration/name");
        Need(!c.channels.empty() && c.channels.size()<=a.joints.size()*3,"Invalid channel count");
        std::set<std::pair<uint32_t,uint32_t>> seen;
        for(const auto& ch:c.channels) {
            Need(ch.joint<a.joints.size() && ch.property<3 && seen.emplace(ch.joint,ch.property).second,"Invalid/duplicate animation target");
            Need(!ch.keys.empty(),"Empty channel"); totalKeys+=ch.keys.size(); Need(totalKeys<=1000000,"Animation key budget exceeded");
            float previous=-1;
            for(const auto& key:ch.keys) {
                Need(std::isfinite(key.time) && key.time>=0 && key.time>previous && key.time<=c.duration,"Invalid key time"); previous=key.time; Finite(key.value);
                if(ch.property==1)Rotation(key.value);
                if(ch.property==2)for(size_t k=0;k<3;++k)Need(key.value[k]>0,"Nonpositive animated scale unsupported");
            }
        }
    }
}
void ValidateAnimationLibrary(const AnimationLibrary& a) {
    Need(!a.id.empty() && a.id.size() <= 256, "Invalid animation library ID");
    Need(a.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_/.") == std::string::npos, "Invalid animation library ID characters");
    ValidateAnimationData({a.joints, a.clips});
    Need(!a.clips.empty(), "Animation library has no clips");
    std::set<std::string> joints, clips, dependencies;
    for (const auto& joint : a.joints) Need(joints.insert(joint.name).second, "Shared Skeleton requires unique joint names");
    for (const auto& clip : a.clips) Need(clips.insert(clip.name).second, "Shared animation requires unique clip names");
    Need(a.dependencies.size() <= 4096, "Too many animation dependencies");
    for (const auto& d : a.dependencies) {
        Need(!d.relativePath.empty() && d.relativePath.size() <= 256 && dependencies.insert(d.relativePath).second, "Invalid animation dependency path");
        Need(d.relativePath.find('\0') == std::string::npos, "Dependency path contains NUL");
        const auto relative = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(d.relativePath.data()), d.relativePath.size())).lexically_normal();
        Need(!relative.has_root_path() && *relative.begin() != L"..", "Animation dependency escapes source directory");
        Need(d.sha256.size() == 64 && d.sha256.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos, "Invalid animation dependency hash");
    }
}
AnimationLibrary LoadAnimationLibrary(const std::filesystem::path& path) {
    auto bytes = ReadAssetBytes(path); Reader r{bytes};
    Need(r.U() == 0x4e414843 && r.U() == 1, "Unsupported CHAN animation format/version");
    AnimationLibrary a; a.id = r.S();
    a.dependencies.resize(r.Count(4096, 8));
    for (auto& d : a.dependencies) { d.relativePath = r.S(); d.sha256 = r.S(); }
    a.joints.resize(r.Count(256, 48));
    for (auto& j : a.joints) { j.name = r.S(); j.parent = std::bit_cast<int32_t>(r.U()); r.A(j.rest.translation); r.A(j.rest.rotation); r.A(j.rest.scale); }
    a.clips.resize(r.Count(64, 12));
    size_t keys = 0;
    for (auto& c : a.clips) {
        c.name = r.S(); c.duration = r.F(); c.channels.resize(r.Count(768, 12));
        for (auto& ch : c.channels) {
            ch.joint = r.U(); ch.property = r.U(); const auto count = r.Count(1000000, 20);
            keys += count; Need(keys <= 1000000, "Animation key budget exceeded"); ch.keys.resize(count);
            for (auto& k : ch.keys) { k.time = r.F(); r.A(k.value); }
        }
    }
    Need(r.p == bytes.size(), "Trailing animation data"); ValidateAnimationLibrary(a); return a;
}
void SaveAnimationLibrary(const AnimationLibrary& a, const std::filesystem::path& path) {
    ValidateAnimationLibrary(a); Writer w; w.U(0x4e414843); w.U(1); w.S(a.id);
    w.U(static_cast<uint32_t>(a.dependencies.size()));
    for (const auto& d : a.dependencies) { w.S(d.relativePath); w.S(d.sha256); }
    w.U(static_cast<uint32_t>(a.joints.size()));
    for (const auto& j : a.joints) { w.S(j.name); w.U(std::bit_cast<uint32_t>(j.parent)); w.A(j.rest.translation); w.A(j.rest.rotation); w.A(j.rest.scale); }
    w.U(static_cast<uint32_t>(a.clips.size()));
    for (const auto& c : a.clips) {
        w.S(c.name); w.F(c.duration); w.U(static_cast<uint32_t>(c.channels.size()));
        for (const auto& ch : c.channels) { w.U(ch.joint); w.U(ch.property); w.U(static_cast<uint32_t>(ch.keys.size())); for (const auto& k : ch.keys) { w.F(k.time); w.A(k.value); } }
    }
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    wchar_t temporaryName[MAX_PATH]{};
    const auto directory = std::filesystem::absolute(path).parent_path();
    Need(GetTempFileNameW(directory.c_str(), L"can", 0, temporaryName) != 0, "Cannot reserve animation temporary file");
    const std::filesystem::path temporary = temporaryName;
    // 候補の再読込に成功するまで旧パッケージを置き換えない。
    try {
        { std::ofstream out(temporary, std::ios::binary | std::ios::trunc); out.write(reinterpret_cast<const char*>(w.b.data()), static_cast<std::streamsize>(w.b.size())); out.flush(); Need(bool(out), "Animation write failed"); }
        (void)LoadAnimationLibrary(temporary);
        Need(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0, "Animation publish failed");
    } catch (...) { std::error_code ignored; std::filesystem::remove(temporary, ignored); throw; }
}
void ValidateSkeletalAsset(const SkeletalAsset& a) {
    ValidateAsset(a.mesh);
    ValidateAnimationData({a.joints, a.clips});
    Need(!a.bindings.empty() && a.bindings.size()<=256,"Invalid skin binding count");
    std::set<uint32_t> boundJoints;
    for(const auto& b:a.bindings) {
        Need(b.joint<a.joints.size(),"Skin joint reference out of range");
        Need(boundJoints.insert(b.joint).second,"Duplicate skin joint");
        for(const auto& row:b.inverseBind.m)for(float f:row)Need(std::isfinite(f),"Nonfinite inverse bind");
        Need(b.inverseBind._14==0 && b.inverseBind._24==0 && b.inverseBind._34==0 && b.inverseBind._44==1,"Inverse bind must be affine");
        Need(DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&b.inverseBind)))!=0,"Singular inverse bind");
    }
    Need(a.skin.size()==a.mesh.vertices.size(),"Skin vertex count mismatch");
    for(const auto& v:a.skin) {
        float sum=0; Finite(v.weights);
        for(size_t k=0;k<4;++k){Need(v.joints[k]<a.bindings.size() && v.weights[k]>=0,"Invalid skin influence");sum+=v.weights[k];}
        Need(std::abs(sum-1)<0.001f,"Skin weights must sum to one");
    }
}

SkeletalAsset LoadSkeletalAsset(const std::filesystem::path& path) {
    auto bytes=ReadAssetBytes(path); Reader r{bytes};
    Need(r.U()==0x53524843,"Unsupported CHRS skeletal format");
    const auto version = r.U(); Need(version == 1 || version == 2,"Unsupported CHRS skeletal version");
    SkeletalAsset a; a.mesh=DecodeStaticAsset(r.Take(r.Count(512u*1024*1024,1)));
    a.joints.resize(r.Count(256,48));
    for(auto& j:a.joints){j.name=r.S();j.parent=std::bit_cast<int32_t>(r.U());r.A(j.rest.translation);r.A(j.rest.rotation);r.A(j.rest.scale);}
    a.bindings.resize(r.Count(256,68));
    for(auto& b:a.bindings){b.joint=r.U();for(auto& row:b.inverseBind.m)for(float& f:row)f=r.F();}
    a.skin.resize(r.Count(2000000,32));for(auto& v:a.skin){for(auto& j:v.joints)j=r.U();r.A(v.weights);}
    a.clips.resize(r.Count(64,12));
    for(auto& c:a.clips){c.name=r.S();c.duration=r.F();c.channels.resize(r.Count(768,12));
        for(auto& ch:c.channels){ch.joint=r.U();ch.property=r.U();ch.keys.resize(r.Count(1000000,20));for(auto& k:ch.keys){k.time=r.F();r.A(k.value);}}}
    Need(r.p==bytes.size(),"Trailing skeletal data"); ValidateSkeletalAsset(a);return a;
}
void SaveSkeletalAsset(const SkeletalAsset& a,const std::filesystem::path& path) {
    ValidateSkeletalAsset(a); Writer w; w.U(0x53524843);w.U(2);
    auto mesh=EncodeStaticAsset(a.mesh);w.U(static_cast<uint32_t>(mesh.size()));w.b.insert(w.b.end(),mesh.begin(),mesh.end());
    w.U(static_cast<uint32_t>(a.joints.size()));for(const auto& j:a.joints){w.S(j.name);w.U(std::bit_cast<uint32_t>(j.parent));w.A(j.rest.translation);w.A(j.rest.rotation);w.A(j.rest.scale);}
    w.U(static_cast<uint32_t>(a.bindings.size()));for(const auto& b:a.bindings){w.U(b.joint);for(const auto& row:b.inverseBind.m)for(float f:row)w.F(f);}
    w.U(static_cast<uint32_t>(a.skin.size()));for(const auto& v:a.skin){for(auto j:v.joints)w.U(j);w.A(v.weights);}
    w.U(static_cast<uint32_t>(a.clips.size()));for(const auto& c:a.clips){w.S(c.name);w.F(c.duration);w.U(static_cast<uint32_t>(c.channels.size()));for(const auto& ch:c.channels){w.U(ch.joint);w.U(ch.property);w.U(static_cast<uint32_t>(ch.keys.size()));for(const auto& k:ch.keys){w.F(k.time);w.A(k.value);}}}
    Need(w.b.size()<=512u*1024*1024,"Skeletal file exceeds 512 MiB");
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
    try {
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(w.b.data()),static_cast<std::streamsize>(w.b.size()));out.flush();Need(bool(out),"Skeletal write failed");}
        (void)LoadSkeletalAsset(temp);Need(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Skeletal publish failed");
    }catch(...){std::error_code ignored;std::filesystem::remove(temp,ignored);throw;}
}
}
