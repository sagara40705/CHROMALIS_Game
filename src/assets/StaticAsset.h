#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <span>

namespace chromalis {
// CPU-only cooked contract; no glTF, ImGui or GPU object identifiers.
struct MeshVertex { float position[3]; float normal[3]; float uv[2]; };
struct MeshPart { uint32_t firstIndex = 0, indexCount = 0, material = 0; };
// マップごとにSamplerを保持し、画像のIDと用途を分離する。
struct TextureBinding {
    uint32_t texture = 0, wrapU = 0, wrapV = 0, nearest = 0;
};
struct MeshMaterial {
    std::array<float, 4> baseColor{1, 1, 1, 1};
    uint32_t texture = 0, wrapU = 0, wrapV = 0, nearest = 0, doubleSided = 0, alphaMask = 0, unlit = 0;
    float cutoff = 0.5f, metallic = 1, roughness = 1;
    uint32_t pbr = 0, alphaBlend = 0;
    TextureBinding metallicRoughnessMap, normalMap, occlusionMap, emissiveMap;
    std::array<float, 3> emissive{0, 0, 0};
    float normalScale = 1, occlusionStrength = 1;
};
struct MeshTexture { uint32_t width = 0, height = 0; std::vector<uint8_t> rgba; };
struct AssetDependency { std::string relativePath, sha256; };
struct StaticAsset {
    static constexpr uint32_t FormatVersion = 2;
    std::string id;
    std::vector<AssetDependency> dependencies;
    std::vector<MeshVertex> vertices;
    std::vector<std::array<float, 4>> tangents;
    std::vector<uint32_t> indices;
    std::vector<MeshPart> parts;
    std::vector<MeshMaterial> materials;
    std::vector<MeshTexture> textures;
};
std::vector<uint8_t> ReadAssetBytes(const std::filesystem::path& path);
void ValidateAsset(const StaticAsset& asset);
StaticAsset LoadStaticAsset(const std::filesystem::path& path);
void SaveStaticAsset(const StaticAsset& asset, const std::filesystem::path& path);
std::vector<uint8_t> EncodeStaticAsset(const StaticAsset& asset);
StaticAsset DecodeStaticAsset(std::span<const uint8_t> bytes);
}
