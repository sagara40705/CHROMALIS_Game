#pragma once
#include "runtime/Renderer.h"
#include <array>
#include <memory>

namespace chromalis {
struct InputState {
    std::array<bool, 256> down{};
    std::array<bool, 256> pressed{};
    POINT mouse{};
    bool leftMouse = false;
};
struct FrameStats {
    double frameMs = 0, cpuWorkMs = 0, fenceWaitMs = 0;
    UINT64 frames = 0;
};
// The only host extension needed now: Editor owns its overlay; Runtime never includes ImGui.
class Overlay {
public:
    virtual ~Overlay() = default;
    virtual void Initialize(HWND window, Renderer& renderer, Log& log) = 0;
    virtual void Shutdown() noexcept = 0;
    virtual bool HandleMessage(HWND window, UINT message, WPARAM w, LPARAM l) = 0;
    virtual void Build(const FrameStats& stats, const InputState& input, RenderSettings& settings) = 0;
    virtual bool CapturesKeyboard() const = 0;
    virtual void Draw(ID3D12GraphicsCommandList* commands) = 0;
};
int RunApplication(HINSTANCE instance, const wchar_t* name, Overlay* overlay);
}
