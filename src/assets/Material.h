#pragma once
#include "assets/StaticAsset.h"
#include <cmath>
#include <stdexcept>

namespace chromalis {
enum class ShadingModel { Lit, Unlit, Toon, Pbr };
// テクスチャとSamplerはモデル側に残し、共有可能な見た目の値だけを保持する。
struct MaterialSurface {
    std::array<float, 4> baseColor{1, 1, 1, 1};
    std::array<float, 3> emission{0, 0, 0}, shadow{0.25f, 0.25f, 0.25f};
    ShadingModel shading = ShadingModel::Lit;
    float cutoff = 0.5f, threshold = 0.5f, softness = 0.05f;
    bool alphaMask = false, doubleSided = false;
    bool alphaBlend = false;
    float metallic = 1, roughness = 1, normalScale = 1, occlusionStrength = 1;
    bool castShadow = true, receiveShadow = true;
    bool operator==(const MaterialSurface&) const = default;
};
struct DirectionalShadow {
    bool enabled = false;
    uint32_t resolution = 1024;
    std::array<float, 3> center{0, 0, 0}, tint{0, 0, 0};
    float extent = 20, depth = 40, strength = 1, bias = 0.001f, normalBias = 0.02f, filterRadius = 1;
};
struct SceneLighting {
    // サーフェスから光源へ向かうWorld方向。色と強度は線形値。
    std::array<float, 3> direction{-0.4f, 0.8f, -0.6f};
    std::array<float, 3> color{0.7f, 0.7f, 0.7f}, ambient{0.3f, 0.3f, 0.3f};
    DirectionalShadow shadow;
};
inline MaterialSurface ImportedSurface(const MeshMaterial& source) {
    MaterialSurface result;
    result.baseColor = source.baseColor;
    result.shading = source.unlit ? ShadingModel::Unlit : source.pbr ? ShadingModel::Pbr : ShadingModel::Lit;
    result.cutoff = source.cutoff;
    result.alphaBlend = source.alphaBlend != 0;
    result.metallic = source.metallic; result.roughness = source.roughness;
    result.normalScale = source.normalScale; result.occlusionStrength = source.occlusionStrength;
    result.emission = source.emissive;
    result.alphaMask = source.alphaMask != 0;
    result.doubleSided = source.doubleSided != 0;
    return result;
}
template<size_t N> inline void ValidateColor(const std::array<float, N>& value, float maximum) {
    for (float channel : value)
        if (!std::isfinite(channel) || channel < 0 || channel > maximum) throw std::runtime_error("Invalid linear material/light color");
}
inline void ValidateMaterial(const MaterialSurface& value) {
    if (value.alphaMask && value.alphaBlend) throw std::runtime_error("Conflicting alpha modes");
    for (float f : {value.metallic, value.roughness, value.occlusionStrength})
        if (!std::isfinite(f) || f < 0 || f > 1) throw std::runtime_error("Invalid PBR factor");
    if (!std::isfinite(value.normalScale) || value.normalScale < 0 || value.normalScale > 16) throw std::runtime_error("Invalid normal scale");
    ValidateColor(value.baseColor, 1); ValidateColor(value.emission, 64); ValidateColor(value.shadow, 1);
    if (value.shading != ShadingModel::Lit && value.shading != ShadingModel::Unlit && value.shading != ShadingModel::Toon && value.shading != ShadingModel::Pbr)
        throw std::runtime_error("Unknown shading model");
    if (!std::isfinite(value.cutoff) || value.cutoff < 0 || !std::isfinite(value.threshold) || value.threshold < 0 || value.threshold > 1 ||
        !std::isfinite(value.softness) || value.softness < 0.0001f || value.softness > 0.5f)
        throw std::runtime_error("Invalid material cutoff/toon parameters");
}
inline void ValidateLighting(const SceneLighting& value) {
    ValidateColor(value.color, 64); ValidateColor(value.ambient, 64);
    double length = 0;
    for (float axis : value.direction) {
        if (!std::isfinite(axis) || std::abs(axis) > 10000) throw std::runtime_error("Invalid light direction");
        length += double(axis) * axis;
    }
    if (length < 1e-12) throw std::runtime_error("Light direction cannot be zero");
    const auto& shadow = value.shadow;
    ValidateColor(shadow.tint, 1);
    for (float axis : shadow.center) if (!std::isfinite(axis) || std::abs(axis) > 100000) throw std::runtime_error("Invalid shadow center");
    if (shadow.resolution != 256 && shadow.resolution != 512 && shadow.resolution != 1024 && shadow.resolution != 2048 && shadow.resolution != 4096)
        throw std::runtime_error("Unsupported shadow resolution");
    const auto range = [](float number, float minimum, float maximum) { return std::isfinite(number) && number >= minimum && number <= maximum; };
    if (!range(shadow.extent, 0.1f, 10000) || !range(shadow.depth, 0.1f, 10000) || !range(shadow.strength, 0, 1) ||
        !range(shadow.bias, 0, 0.05f) || !range(shadow.normalBias, 0, 1) || !range(shadow.filterRadius, 0, 4))
        throw std::runtime_error("Invalid directional shadow settings");
}
}
