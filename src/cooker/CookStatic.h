#pragma once
#include "assets/SkeletalAsset.h"
#include "assets/StaticAsset.h"
namespace chromalis {
SkeletalAsset CookSkeletal(const std::filesystem::path& source, const std::string& id);
StaticAsset CookStatic(const std::filesystem::path& source, const std::string& id);
}
