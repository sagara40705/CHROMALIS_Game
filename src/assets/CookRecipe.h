#pragma once
#include <string_view>

namespace chromalis {
// 変換規則を変更したら版を進める。Editorの再生成判定とCookerで共有する。
inline constexpr std::string_view StaticCookRecipe = "CHRMESH1/cgltf1.15/WIC-RGBA8/static-LH/no-mips/recipe2";
inline constexpr std::string_view SkeletalCookRecipe = "CHRS2/cgltf1.15/LH/LINEAR/recipe2";
inline constexpr std::string_view AnimationCookRecipe = "CHAN1/cgltf1.15/LH/shared-rig/LINEAR/recipe1";
}
