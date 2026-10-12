#include "assets/StaticAsset.h"
#include <windows.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <span>
#include <stdexcept>

namespace chromalis {
namespace {
constexpr size_t MaxFile = 512u * 1024 * 1024;
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Writer {
    std::vector<uint8_t> bytes;
    void U32(uint32_t v) { for (int n = 0; n < 4; ++n) bytes.push_back(static_cast<uint8_t>(v >> (n * 8))); }
    void F32(float v) { U32(std::bit_cast<uint32_t>(v)); }
    void String(const std::string& v) { U32(static_cast<uint32_t>(v.size())); bytes.insert(bytes.end(), v.begin(), v.end()); }
};
struct Reader {
    std::span<const uint8_t> bytes;
    size_t cursor = 0;
    std::span<const uint8_t> Take(size_t size) {
        Require(size <= bytes.size() - cursor, "Cooked asset truncated");
        auto result = bytes.subspan(cursor, size); cursor += size; return result;
    }
    uint32_t U32() { auto b = Take(4); return b[0] | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }
    float F32() { return std::bit_cast<float>(U32()); }
    uint32_t Count(uint32_t limit, size_t minimumBytes) {
        const auto count = U32();
        Require(count <= limit && count <= (bytes.size() - cursor) / minimumBytes, "Cooked count exceeds limits or remaining file");
        return count;
    }
    std::string String() { const auto n = Count(4096, 1); auto b = Take(n); return {reinterpret_cast<const char*>(b.data()), b.size()}; }
};
}
std::vector<uint8_t> ReadAssetBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open asset: " + path.string());
    const auto size = in.tellg();
    Require(size > 0 && static_cast<uint64_t>(size) <= MaxFile, "Asset file empty or exceeds 512 MiB import limit");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.seekg(0); in.read(reinterpret_cast<char*>(bytes.data()), size);
    Require(bool(in), "Asset read failed"); return bytes;
}
void ValidateAsset(const StaticAsset& a) {
    Require(!a.id.empty() && a.id.size() <= 256 && a.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_/.") == std::string::npos,
        "Asset ID must be 1..256 ASCII letters/digits/-_/.");
    Require(!a.vertices.empty() && a.vertices.size() <= 2000000, "Vertex count outside supported range");
    Require(!a.indices.empty() && a.indices.size() <= 6000000 && a.indices.size() % 3 == 0, "Invalid triangle index count");
    Require(!a.parts.empty() && a.parts.size() <= 65536, "Invalid mesh part count");
    Require(!a.materials.empty() && a.materials.size() <= 1024, "Invalid material count");
    Require(!a.textures.empty() && a.textures.size() <= 256, "Invalid texture count");
    Require(a.dependencies.size() <= 2048, "Too many dependencies");
    for (const auto& d : a.dependencies) Require(d.relativePath.size() <= 4096 && d.sha256.size() == 64, "Invalid dependency metadata");
    for (const auto& v : a.vertices) {
        for (float f : v.position) Require(std::isfinite(f), "Nonfinite vertex position");
        for (float f : v.normal) Require(std::isfinite(f), "Nonfinite vertex normal");
        for (float f : v.uv) Require(std::isfinite(f), "Nonfinite vertex UV");
        const float n = v.normal[0]*v.normal[0] + v.normal[1]*v.normal[1] + v.normal[2]*v.normal[2];
        Require(n > 0.9f && n < 1.1f, "Vertex normal must be normalized");
    }
    for (auto i : a.indices) Require(i < a.vertices.size(), "Index out of range");
    uint64_t next = 0;
    for (const auto& p : a.parts) {
        Require(p.firstIndex == next && p.indexCount && p.indexCount % 3 == 0 && p.material < a.materials.size(), "Invalid mesh part");
        next += p.indexCount; Require(next <= a.indices.size(), "Mesh part extends beyond indices");
    }
    Require(next == a.indices.size(), "Mesh parts do not cover index buffer");
    for (const auto& m : a.materials) {
        Require(m.texture < a.textures.size() && m.wrapU <= 2 && m.wrapV <= 2 && m.nearest <= 1 && m.doubleSided <= 1 && m.alphaMask <= 1 && m.unlit <= 1, "Invalid material reference/flags");
        for (float f : m.baseColor) Require(std::isfinite(f) && f >= 0 && f <= 1, "Invalid base color factor");
        for (float f : {m.cutoff, m.metallic, m.roughness}) Require(std::isfinite(f) && f >= 0 && f <= 1, "Invalid material scalar");
    }
    uint64_t total = 0;
    for (const auto& t : a.textures) {
        Require(t.width && t.height && t.width <= 8192 && t.height <= 8192 && uint64_t(t.width)*t.height <= 16777216, "Texture exceeds import dimensions");
        Require(t.rgba.size() == uint64_t(t.width)*t.height*4, "Texture payload size mismatch");
        total += t.rgba.size(); Require(total <= 256u*1024*1024, "Total decoded textures exceed 256 MiB");
    }
}
StaticAsset LoadStaticAsset(const std::filesystem::path& path) {
    const auto bytes = ReadAssetBytes(path); Reader r{bytes};
    Require(r.U32() == 0x4d524843 && r.U32() == 0x485345, "Not a CHRMESH cooked asset");
    Require(r.U32() == StaticAsset::FormatVersion, "Unsupported cooked asset version; recook with matching Cooker");
    StaticAsset a; a.id = r.String();
    a.dependencies.resize(r.Count(2048, 8));
    for (auto& d : a.dependencies) { d.relativePath = r.String(); d.sha256 = r.String(); }
    a.vertices.resize(r.Count(2000000, 32));
    for (auto& v : a.vertices) { for (auto& f : v.position) f = r.F32(); for (auto& f : v.normal) f = r.F32(); for (auto& f : v.uv) f = r.F32(); }
    a.indices.resize(r.Count(6000000, 4)); for (auto& i : a.indices) i = r.U32();
    a.parts.resize(r.Count(65536, 12)); for (auto& p : a.parts) { p.firstIndex = r.U32(); p.indexCount = r.U32(); p.material = r.U32(); }
    a.materials.resize(r.Count(1024, 56));
    for (auto& m : a.materials) {
        for (auto& f : m.baseColor) f = r.F32();
        m.texture = r.U32(); m.wrapU = r.U32(); m.wrapV = r.U32(); m.nearest = r.U32();
        m.doubleSided = r.U32(); m.alphaMask = r.U32(); m.unlit = r.U32(); m.cutoff = r.F32(); m.metallic = r.F32(); m.roughness = r.F32();
    }
    a.textures.resize(r.Count(256, 12));
    for (auto& t : a.textures) {
        t.width = r.U32(); t.height = r.U32(); const auto size = r.Count(67108864, 1);
        auto raw = r.Take(size); t.rgba.assign(raw.begin(), raw.end());
    }
    Require(r.cursor == bytes.size(), "Unexpected trailing cooked data");
    ValidateAsset(a); return a;
}
void SaveStaticAsset(const StaticAsset& a, const std::filesystem::path& path) {
    ValidateAsset(a); Writer w; w.U32(0x4d524843); w.U32(0x485345); w.U32(StaticAsset::FormatVersion); w.String(a.id);
    w.U32(static_cast<uint32_t>(a.dependencies.size())); for (const auto& d : a.dependencies) { w.String(d.relativePath); w.String(d.sha256); }
    w.U32(static_cast<uint32_t>(a.vertices.size()));
    for (const auto& v : a.vertices) { for (auto f : v.position) w.F32(f); for (auto f : v.normal) w.F32(f); for (auto f : v.uv) w.F32(f); }
    w.U32(static_cast<uint32_t>(a.indices.size())); for (auto i : a.indices) w.U32(i);
    w.U32(static_cast<uint32_t>(a.parts.size())); for (const auto& p : a.parts) { w.U32(p.firstIndex); w.U32(p.indexCount); w.U32(p.material); }
    w.U32(static_cast<uint32_t>(a.materials.size()));
    for (const auto& m : a.materials) {
        for (auto f : m.baseColor) w.F32(f);
        w.U32(m.texture); w.U32(m.wrapU); w.U32(m.wrapV); w.U32(m.nearest); w.U32(m.doubleSided); w.U32(m.alphaMask); w.U32(m.unlit);
        w.F32(m.cutoff); w.F32(m.metallic); w.F32(m.roughness);
    }
    w.U32(static_cast<uint32_t>(a.textures.size()));
    for (const auto& t : a.textures) { w.U32(t.width); w.U32(t.height); w.U32(static_cast<uint32_t>(t.rgba.size())); w.bytes.insert(w.bytes.end(), t.rgba.begin(), t.rgba.end()); }
    Require(w.bytes.size() <= MaxFile, "Cooked asset exceeds 512 MiB");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temp = path; temp += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    try {
        { std::ofstream out(temp, std::ios::binary | std::ios::trunc); out.write(reinterpret_cast<const char*>(w.bytes.data()), static_cast<std::streamsize>(w.bytes.size())); out.flush(); Require(bool(out), "Cannot write cooked candidate"); }
        (void)LoadStaticAsset(temp);
        Require(MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0, "Cannot publish cooked candidate; previous file retained");
    } catch (...) { std::error_code ignored; std::filesystem::remove(temp, ignored); throw; }
}
}
