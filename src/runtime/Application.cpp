#include "runtime/Application.h"
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <stdexcept>
#include <string_view>

namespace chromalis {
namespace {
using Clock = std::chrono::steady_clock;
struct Options {
    RendererOptions renderer;
    UINT64 frameLimit = 0;
    bool smoke = false;
    std::filesystem::path capture, log;
};
Options ParseOptions() {
    Options result;
#ifdef _DEBUG
    result.renderer.debug = true;
#endif
    int count = 0;
    auto** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) throw std::runtime_error("CommandLineToArgvW failed");
    try {
        for (int i = 1; i < count; ++i) {
            const std::wstring_view arg(args[i]);
            const auto value = [&]() -> std::wstring_view {
                if (++i >= count) throw std::runtime_error("Missing value for " + Utf8(arg));
                return args[i];
            };
            if (arg == L"--warp") result.renderer.warp = true;
            else if (arg == L"--require-debug") result.renderer.debug = true;
            else if (arg == L"--no-debug") result.renderer.debug = false;
            else if (arg == L"--gpu-validation") { result.renderer.debug = true; result.renderer.gpuValidation = true; }
            else if (arg == L"--smoke-test") result.smoke = true;
            else if (arg == L"--capture") result.capture = value();
            else if (arg == L"--log") result.log = value();
            else if (arg == L"--frames") {
                const auto text = value();
                if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring_view::npos)
                    throw std::runtime_error("--frames must be an integer from 1 to 1000000");
                result.frameLimit = std::stoull(std::wstring(text));
                if (!result.frameLimit || result.frameLimit > 1000000) throw std::runtime_error("--frames out of range");
            } else throw std::runtime_error("Unknown argument: " + Utf8(arg));
        }
        if (result.smoke) result.frameLimit = 32;
        if (!result.capture.empty() && !result.frameLimit) throw std::runtime_error("--capture requires --frames or --smoke-test");
        if (result.renderer.gpuValidation && !result.renderer.debug) throw std::runtime_error("GPU validation requires Debug Layer");
    } catch (...) { LocalFree(args); throw; }
    LocalFree(args);
    return result;
}

class Application final {
public:
    Application(HINSTANCE instance, const wchar_t* name, Overlay* overlay, Log& log, Options options)
        : instance_(instance), name_(name), overlay_(overlay), log_(log), options_(std::move(options)), renderer_(log) {}
    ~Application() {
        try { renderer_.WaitIdle(); } catch (const std::exception& e) { log_.Write(e.what()); }
        if (overlay_) overlay_->Shutdown();
        overlayReady_ = false;
        if (window_) DestroyWindow(window_);
        if (registered_) UnregisterClassW(name_, instance_);
    }
    int Run() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance_; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = name_;
        CheckWin32(RegisterClassExW(&wc) != 0, "Register window class");
        registered_ = true;
        RECT bounds{0, 0, 1280, 720};
        CheckWin32(AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem()), "Adjust initial window bounds");
        window_ = CreateWindowExW(0, name_, name_, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance_, this);
        CheckWin32(window_ != nullptr, "Create Win32 window");
        RECT client{};
        CheckWin32(GetClientRect(window_, &client), "Get initial client bounds");
        pendingWidth_ = static_cast<UINT>(client.right); pendingHeight_ = static_cast<UINT>(client.bottom);
        renderer_.Initialize(window_, pendingWidth_, pendingHeight_, options_.renderer);
        if (overlay_) {
            overlay_->Initialize(window_, renderer_, log_);
            overlayReady_ = true;
        }
        ShowWindow(window_, SW_SHOW);
        log_.Write("Window shown; initialization complete (not yet proof of rendering)");
        auto previous = Clock::now();
        const auto smokeDeadline = previous + std::chrono::seconds(30);
        double titleTime = 0;
        bool minimizedTest = false;
        bool visible = true;
        while (running_) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) { running_ = false; break; }
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (windowError_) std::rethrow_exception(windowError_);
            if (!running_) break;
            if (options_.smoke && Clock::now() > smokeDeadline) throw std::runtime_error("Smoke test stalled for 30 seconds");
            if (options_.smoke) {
                if (stats_.frames == 4 && !smokeResized_) { SetClientSize(960, 600); smokeResized_ = true; }
                if (stats_.frames == 8 && !minimizedTest) {
                    minimizedTest = true;
                    if (!SetTimer(window_, 1, 150, nullptr)) CheckWin32(FALSE, "Create restore timer");
                    ShowWindow(window_, SW_MINIMIZE);
                }
                if (stats_.frames == 12 && !smokeRestoredSize_) { SetClientSize(1280, 720); smokeRestoredSize_ = true; }
            }
            if (minimized_ || sizing_ || !pendingWidth_ || !pendingHeight_) {
                // Message-aware wait: no rendering or zero-sized ResizeBuffers while minimized.
                MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                previous = Clock::now(); input_.pressed.fill(false);
                continue;
            }
            if (!visible) {
                visible = renderer_.TestVisible();
                if (!visible) {
                    MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    previous = Clock::now(); input_.pressed.fill(false);
                    continue;
                }
            }
            renderer_.Resize(pendingWidth_, pendingHeight_);
            const auto start = Clock::now();
            const double dt = std::chrono::duration<double>(start - previous).count();
            previous = start;
            stats_.frameMs = dt * 1000;
            if (overlay_) overlay_->Build(stats_, input_, settings_);
            if (!overlay_ || !overlay_->CapturesKeyboard()) {
                if (input_.pressed[VK_ESCAPE]) { running_ = false; continue; }
                if (input_.pressed[VK_SPACE]) {
                    settings_.x = 0; settings_.y = 0;
                    log_.Write("Input: Space reset triangle");
                }
                const auto step = static_cast<float>(std::min(dt, 0.1)) * 0.7f;
                settings_.x += step * static_cast<float>(static_cast<int>(input_.down['D'] || input_.down[VK_RIGHT]) - static_cast<int>(input_.down['A'] || input_.down[VK_LEFT]));
                settings_.y += step * static_cast<float>(static_cast<int>(input_.down['W'] || input_.down[VK_UP]) - static_cast<int>(input_.down['S'] || input_.down[VK_DOWN]));
                settings_.x = std::clamp(settings_.x, -1.2f, 0.6f);
                settings_.y = std::clamp(settings_.y, -0.7f, 0.7f);
            }
            auto* list = renderer_.BeginFrame(settings_);
            if (overlay_) overlay_->Draw(list);
            const bool last = options_.frameLimit && stats_.frames + 1 >= options_.frameLimit;
            const bool capture = last && (options_.smoke || !options_.capture.empty());
            const auto beforePresent = Clock::now();
            stats_.cpuWorkMs = std::chrono::duration<double, std::milli>(beforePresent - start).count();
            stats_.fenceWaitMs = renderer_.FenceWaitMs();
            visible = renderer_.EndFrame(settings_.vsync, capture);
            ++stats_.frames;
            input_.pressed.fill(false);
            if (stats_.frames == 1) log_.Write("First command list submitted and Present returned successfully");
            titleTime += dt;
            if (titleTime > 0.5) {
                const auto title = std::format(L"{} | {:.2f} ms | WASD/arrows: move | Space: reset | Esc: exit", name_, stats_.frameMs);
                CheckWin32(SetWindowTextW(window_, title.c_str()), "Update diagnostic title");
                titleTime = 0;
            }
            if (capture) renderer_.SaveCapture(options_.capture, options_.smoke);
            if (last) SendMessageW(window_, WM_CLOSE, 0, 0);
        }
        if (windowError_) std::rethrow_exception(windowError_);
        renderer_.WaitIdle();
        if (overlay_) overlay_->Shutdown();
        overlayReady_ = false;
        // Include destruction-time backend messages in the result.
        renderer_.WaitIdle();
        if (options_.smoke && (!smokeResized_ || !smokeRestoredSize_ || !sawMinimize_ || !sawRestore_ || stats_.frames != 32))
            throw std::runtime_error("Smoke test lifecycle assertions failed");
        log_.Write(std::format("Shutdown: frames={}, Debug Layer errors={}, resize/minimize/restore={}/{}/{}",
            stats_.frames, renderer_.DiagnosticErrors(), smokeResized_, sawMinimize_, sawRestore_));
        if (renderer_.DiagnosticErrors()) return 1;
        if (options_.smoke) log_.Write("SMOKE PASS (pixel readback + window lifecycle + clean GPU drain)");
        return 0;
    }
private:
    void SetClientSize(UINT width, UINT height) {
        RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        CheckWin32(AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(window_)), "Adjust smoke-test bounds");
        CheckWin32(SetWindowPos(window_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), "Resize smoke-test window");
    }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) noexcept {
        auto* self = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, w, l);
        try { return self->OnMessage(window, message, w, l); }
        catch (...) { self->windowError_ = std::current_exception(); self->running_ = false; return 0; }
    }
    LRESULT OnMessage(HWND window, UINT message, WPARAM w, LPARAM l) {
        const bool handled = overlayReady_ && overlay_->HandleMessage(window, message, w, l);
        switch (message) {
        case WM_SIZE:
            minimized_ = w == SIZE_MINIMIZED;
            if (minimized_) { sawMinimize_ = true; log_.Write("Window minimized; rendering suspended"); }
            else {
                if (sawMinimize_) sawRestore_ = true;
                pendingWidth_ = LOWORD(l); pendingHeight_ = HIWORD(l);
            }
            return 0;
        case WM_ENTERSIZEMOVE: sizing_ = true; return 0;
        case WM_EXITSIZEMOVE: sizing_ = false; return 0;
        case WM_DPICHANGED: {
            const auto& rect = *reinterpret_cast<RECT*>(l);
            CheckWin32(SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                SWP_NOZORDER | SWP_NOACTIVATE), "Apply DPI window bounds");
            return 0;
        }
        case WM_TIMER:
            if (w == 1 && options_.smoke) { KillTimer(window, 1); ShowWindow(window, SW_RESTORE); log_.Write("Smoke timer restored window"); }
            return 0;
        case WM_KILLFOCUS:
            input_.down.fill(false); input_.pressed.fill(false); input_.leftMouse = false;
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if (w < input_.down.size()) {
                if (!input_.down[w]) input_.pressed[w] = true;
                input_.down[w] = true;
            }
            break;
        case WM_KEYUP: case WM_SYSKEYUP:
            if (w < input_.down.size()) input_.down[w] = false;
            break;
        case WM_MOUSEMOVE: input_.mouse = {GET_X_LPARAM(l), GET_Y_LPARAM(l)}; break;
        case WM_LBUTTONDOWN:
            input_.leftMouse = true;
            if (!overlay_) SetCapture(window);
            break;
        case WM_LBUTTONUP:
            input_.leftMouse = false;
            if (!overlay_ && GetCapture() == window) ReleaseCapture();
            break;
        case WM_CAPTURECHANGED: input_.leftMouse = false; break;
        case WM_CLOSE: running_ = false; log_.Write("WM_CLOSE requested orderly shutdown"); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return handled ? 1 : DefWindowProcW(window, message, w, l);
    }
    HINSTANCE instance_;
    const wchar_t* name_;
    Overlay* overlay_;
    Log& log_;
    Options options_;
    Renderer renderer_;
    HWND window_ = nullptr;
    bool registered_ = false, overlayReady_ = false, running_ = true, minimized_ = false, sizing_ = false;
    bool smokeResized_ = false, smokeRestoredSize_ = false, sawMinimize_ = false, sawRestore_ = false;
    UINT pendingWidth_ = 0, pendingHeight_ = 0;
    std::exception_ptr windowError_;
    InputState input_;
    RenderSettings settings_;
    FrameStats stats_;
};
}

int RunApplication(HINSTANCE instance, const wchar_t* name, Overlay* overlay) {
    std::unique_ptr<Log> log;
    bool unattended = false;
    try {
        auto options = ParseOptions();
        unattended = options.frameLimit != 0;
        if (options.log.empty()) {
            wchar_t executable[32768]{};
            const auto length = GetModuleFileNameW(nullptr, executable, 32768);
            if (!length || length >= 32768) throw std::runtime_error("Cannot resolve executable path for logs");
            options.log = std::filesystem::path(executable).parent_path() / L"logs" /
                std::format(L"{}-{}.log", name, GetCurrentProcessId());
        }
        log = std::make_unique<Log>(options.log);
        log->Write("Starting " + Utf8(name) + " | C++20 | Phase 0A");
        Application application(instance, name, overlay, *log, std::move(options));
        return application.Run();
    } catch (const std::exception& e) {
        const auto message = std::string("Fatal: ") + e.what();
        if (log) log->Write(message); else OutputDebugStringA(message.c_str());
        if (!unattended) MessageBoxA(nullptr, message.c_str(), "CHROMALIS startup/runtime error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
}
