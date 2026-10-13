#include "gameplay/PlayerDemo.h"
#include <cmath>
#include <stdexcept>

using namespace chromalis;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class InputTest final : public GameLoop {
public:
    void Initialize(SceneRuntime& scene) override {
        demo_.Initialize(scene);
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) -> BOOL {
            *reinterpret_cast<HWND*>(value) = window; return FALSE;
        }, reinterpret_cast<LPARAM>(&window_));
        Require(window_ != nullptr, "Player input test window missing");
    }
    void Update(SceneRuntime& scene, const InputState& input, float, RenderSettings& settings) override {
        // 実Win32ハンドラの結果を次フレームで読む。入力だけを直接捏造しない。
        demo_.Update(scene, input, 1.0f/60, settings);
        if (frames == 1) Require(input.down['W'] && demo_.Motion().state == Locomotion::Walk, "Win32 walking key failed");
        if (frames == 21) Require(input.down[VK_SHIFT] && demo_.Motion().state == Locomotion::Run, "Win32 running key failed");
        if (frames == 41) Require(demo_.Motion().cameraYaw < 0.35f, "Win32 orbit key failed");
        if (frames == 51) Require(!input.down['W'] && !input.down[VK_SHIFT] && demo_.Motion().state == Locomotion::Idle, "Focus loss kept movement active");
        if (frames == 61) Require(std::hypot(demo_.Motion().position[0],demo_.Motion().position[2]) == 0, "Win32 reset key failed");
        if (frames == 66) Require(!IsIconic(window_), "Input test did not restore");
        if (frames == 0) PostMessageW(window_, WM_KEYDOWN, 'W', 0);
        if (frames == 20) PostMessageW(window_, WM_KEYDOWN, VK_SHIFT, 0);
        if (frames == 40) PostMessageW(window_, WM_KEYDOWN, 'Q', 0);
        if (frames == 50) PostMessageW(window_, WM_KILLFOCUS, 0, 0);
        if (frames == 60) PostMessageW(window_, WM_KEYDOWN, 'R', 0);
        if (frames == 62) PostMessageW(window_, WM_KEYUP, 'R', 0);
        if (frames == 65) {
            Require(SetTimer(window_, 2, 100, [](HWND window, UINT, UINT_PTR id, DWORD) {
                KillTimer(window, id); ShowWindow(window, SW_RESTORE);
            }) != 0, "Restore timer failed");
            ShowWindow(window_, SW_MINIMIZE);
        }
        ++frames;
    }
    unsigned frames = 0;
private:
    HWND window_{};
    PlayerDemo demo_;
};
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    InputTest test;
    const int result = RunApplication(instance, L"CHROMALIS Player Input Test", nullptr, &test);
    return result == 0 && test.frames >= 80 ? 0 : 1;
}
