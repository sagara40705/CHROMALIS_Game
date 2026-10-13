#pragma once
#include <windows.h>
#include <array>

namespace chromalis {
struct InputState {
    std::array<bool, 256> down{};
    std::array<bool, 256> pressed{};
    POINT mouse{};
    bool leftMouse = false;
};
}
