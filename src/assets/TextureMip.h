#pragma once
#include "assets/StaticAsset.h"
#include <algorithm>
#include <cmath>

namespace chromalis {
// sRGBのRGBだけを線形化して縮小する。AlphaとData Mapは常に線形値。
inline std::vector<MeshTexture> BuildMipChain(const MeshTexture& source, bool srgb) {
    std::vector<MeshTexture> levels{source};
    const auto decode = [srgb](uint8_t byte, size_t channel) {
        const float value = byte / 255.0f;
        return !srgb || channel == 3 ? value : value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    while (levels.back().width > 1 || levels.back().height > 1) {
        const auto& input = levels.back();
        MeshTexture output; output.width = std::max(1u, input.width / 2); output.height = std::max(1u, input.height / 2);
        output.rgba.resize(size_t(output.width) * output.height * 4);
        for (uint32_t y = 0; y < output.height; ++y) for (uint32_t x = 0; x < output.width; ++x) {
            // 奇数寸法の端も含む面積平均。中央の画素を偏って重複させない。
            const double left = double(x) * input.width / output.width, right = double(x + 1) * input.width / output.width;
            const double top = double(y) * input.height / output.height, bottom = double(y + 1) * input.height / output.height;
            for (size_t channel = 0; channel < 4; ++channel) {
                double sum = 0;
                for (uint32_t sy = static_cast<uint32_t>(top); sy < static_cast<uint32_t>(std::ceil(bottom)); ++sy)
                    for (uint32_t sx = static_cast<uint32_t>(left); sx < static_cast<uint32_t>(std::ceil(right)); ++sx) {
                        const double area = (std::min(right, double(sx + 1)) - std::max(left, double(sx))) *
                            (std::min(bottom, double(sy + 1)) - std::max(top, double(sy)));
                        sum += decode(input.rgba[(size_t(sy) * input.width + sx) * 4 + channel], channel) * area;
                    }
                double value = sum / ((right - left) * (bottom - top));
                if (srgb && channel != 3) value = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - 0.055;
                output.rgba[(size_t(y) * output.width + x) * 4 + channel] = static_cast<uint8_t>(std::clamp(std::lround(value * 255), 0L, 255L));
            }
        }
        levels.push_back(std::move(output));
    }
    return levels;
}
}
