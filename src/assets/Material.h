#pragma once
#include "assets/StaticAsset.h"
#include <cmath>
#include <stdexcept>

namespace chromalis {
enum class ShadingModel { Lit, Unlit, Toon };
// テクスチャとSamplerはモデル側に残し、共有可能な見た目の値だけを保持する。
struct MaterialSurface {
    std::array<float, 4> baseColor{1, 1, 1, 1};
    std::array<float, 3> emission{0, 0, 0}, shadow{0.25f, 0.25f, 0.25f};
    ShadingModel shading = ShadingModel::Lit;
    float cutoff = 0.5f, threshold = 0.5f, softness = 0.05f;
    bool alphaMask = false, doubleSided = false;
    bool operator==(const MaterialSurface&) const = default;
};
struct SceneLighting {
    // サーフェスから光源へ向かうWorld方向。色と強度は線形値。
    std::array<float, 3> direction{-0.4f, 0.8f, -0.6f};
    std::array<float, 3> color{0.7f, 0.7f, 0.7f}, ambient{0.3f, 0.3f, 0.3f};
};
inline MaterialSurface ImportedSurface(const MeshMaterial& source) {
    MaterialSurface result;
    result.baseColor = source.baseColor;
    result.shading = source.unlit ? ShadingModel::Unlit : ShadingModel::Lit;
    result.cutoff = source.cutoff;
    result.alphaMask = source.alphaMask != 0;
    result.doubleSided = source.doubleSided != 0;
    return result;
}
template<size_t N> inline void ValidateColor(const std::array<float, N>& value, float maximum) {
    for (float channel : value)
        if (!std::isfinite(channel) || channel < 0 || channel > maximum) throw std::runtime_error("Invalid linear material/light color");
}
inline void ValidateMaterial(const MaterialSurface& value) {
    ValidateColor(value.baseColor, 1); ValidateColor(value.emission, 64); ValidateColor(value.shadow, 1);
    if (value.shading != ShadingModel::Lit && value.shading != ShadingModel::Unlit && value.shading != ShadingModel::Toon)
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
}
}
