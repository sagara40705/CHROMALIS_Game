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
struct ModelPose {
    std::array<float, 3> position{0, 0, 0};
    float yaw = 0, scale = 1;
};
struct LegacyPreview {
    std::array<ModelPose, 2> models{ModelPose{{-1.25f,0,0},0,0.7f}, ModelPose{{1.25f,0,0.6f},0.5f,0.7f}};
    ModelPose animated{{0,-1,0},0,1};
    struct {
        std::array<float, 3> position{0, 0, 0};
        float scale = 0.3f;
        int count = 1, joint = 0;
        bool followSocket = false;
    } vfx;
};
struct Options {
    std::filesystem::path scene;
    RendererOptions renderer;
    UINT64 frameLimit = 0;
    bool smoke = false;
    std::filesystem::path capture, log;
    std::array<std::filesystem::path,2> assets;
    bool assetSmoke = false;
    bool assetCheck = false;
    bool colorCheck = false;
    std::filesystem::path assetUpdate;
    std::filesystem::path animation;
    std::filesystem::path vfx;
    std::filesystem::path vfxUpdate;
    bool integrationSmoke=false,vfxCheck=false;
    bool animationCheck=false,vfxDepthCheck=false;
    bool bindPose=false,noVsync=false;
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
            else if (arg == L"--scene") result.scene=value();
            else if (arg == L"--asset") result.assets[0] = value();
            else if (arg == L"--asset2") result.assets[1] = value();
            else if (arg == L"--asset-smoke") result.assetSmoke = true;
            else if (arg == L"--asset-check") result.assetCheck = true;
            else if (arg == L"--color-check") { result.assetCheck = true; result.colorCheck = true; }
            else if (arg == L"--asset-update") result.assetUpdate = value();
            else if (arg == L"--animation") result.animation=value();
            else if (arg == L"--bind-pose") result.bindPose=true;
            else if (arg == L"--no-vsync") result.noVsync=true;
            else if (arg == L"--vfx") result.vfx=value();
            else if (arg == L"--vfx-update") result.vfxUpdate=value();
            else if (arg == L"--vfx-check") result.vfxCheck=true;
            else if (arg == L"--integration-smoke") result.integrationSmoke=true;
            else if (arg == L"--animation-check") result.animationCheck=true;
            else if (arg == L"--vfx-depth-check") result.vfxDepthCheck=true;
            else if (arg == L"--frames") {
                const auto text = value();
                if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring_view::npos)
                    throw std::runtime_error("--frames must be an integer from 1 to 1000000");
                result.frameLimit = std::stoull(std::wstring(text));
                if (!result.frameLimit || result.frameLimit > 1000000) throw std::runtime_error("--frames out of range");
            } else throw std::runtime_error("Unknown argument: " + Utf8(arg));
        }
        if (!result.scene.empty() && (!result.assets[0].empty() || !result.assets[1].empty() || !result.animation.empty() || !result.vfx.empty() || result.smoke || result.integrationSmoke))
            throw std::runtime_error("--scene cannot be combined with legacy asset/test setup");
        if (result.smoke) result.frameLimit = 32;
        if(result.vfxCheck){if(result.vfx.empty())throw std::runtime_error("--vfx-check requires AlphaProbe.efkefc");result.frameLimit=4;}
        if(result.animationCheck){if(result.animation.empty())throw std::runtime_error("--animation-check requires skinning.cskin");result.frameLimit=4;}
        if(result.vfxDepthCheck){if(result.vfx.empty()||result.assets[0].empty())throw std::runtime_error("--vfx-depth-check requires effect and midtone mesh");result.frameLimit=4;}
        if(result.integrationSmoke){if(result.animation.empty()||result.vfx.empty()||result.assets[0].empty())throw std::runtime_error("--integration-smoke requires static, animated and VFX assets");result.frameLimit=120;}
        if (result.assetCheck) {
            if (result.assets[0].empty()) throw std::runtime_error("--asset-check requires the corners-depth test asset");
            result.frameLimit = 4;
        }
        if (!result.assetUpdate.empty() && !result.assetCheck) throw std::runtime_error("--asset-update requires --asset-check");
        if (result.colorCheck && !result.assetUpdate.empty()) throw std::runtime_error("--color-check does not accept --asset-update");
        if (int(result.assetCheck)+int(result.assetSmoke)+int(result.smoke)+int(result.vfxCheck)+int(result.animationCheck)+int(result.vfxDepthCheck)+int(result.integrationSmoke)>1)
            throw std::runtime_error("Select only one smoke/pixel test mode");
        if(result.bindPose && result.animation.empty())throw std::runtime_error("--bind-pose requires --animation");
        if(!result.vfxUpdate.empty() && !result.vfxCheck)throw std::runtime_error("--vfx-update requires --vfx-check and AlphaProbe as the update");
        if (result.assetSmoke) {
            if (result.assets[0].empty()) throw std::runtime_error("--asset-smoke requires --asset");
            result.frameLimit = 32;
        }
        if (!result.capture.empty() && !result.frameLimit) throw std::runtime_error("--capture requires --frames or --smoke-test");
        if (result.renderer.gpuValidation && !result.renderer.debug) throw std::runtime_error("GPU validation requires Debug Layer");
    } catch (...) { LocalFree(args); throw; }
    LocalFree(args);
    return result;
}

class Application final {
public:
    Application(HINSTANCE instance, const wchar_t* name, Overlay* overlay, Log& log, Options options)
        : instance_(instance), name_(name), overlay_(overlay), log_(log), options_(std::move(options)), renderer_(log), scene_(renderer_, log) {}
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
        if(!options_.animation.empty() && !ReloadAnimation(options_.animation))throw std::runtime_error(scene_.Status());
        if(options_.bindPose){scene_.Animation(3)->playback.bindPose=true;scene_.Animation(3)->playback.playing=false;}
        settings_.vsync=!options_.noVsync;
        if(!options_.vfx.empty()) {if(!ReloadVfx(options_.vfx))throw std::runtime_error(scene_.Status());scene_.Effect(4)->Play(legacy_.vfx.count);}
        for (size_t slot = 0; slot < options_.assets.size(); ++slot)
            if (!options_.assets[slot].empty() && !ReloadModel(slot,options_.assets[slot]))
                throw std::runtime_error(scene_.Status());
        if (options_.assetSmoke && options_.assets[1].empty()) {
            options_.assets[1] = options_.assets[0];
            if (!ReloadModel(1,options_.assets[1])) throw std::runtime_error(scene_.Status());
        }
        if (options_.smoke) settings_.showModels = false;
        if(options_.vfxCheck){settings_.cameraYaw=settings_.cameraPitch=0;settings_.cameraDistance=6;legacy_.vfx.scale=1;settings_.clear={0.2f,0.3f,0.4f,1};}
        if(options_.integrationSmoke){legacy_.vfx.followSocket=true;legacy_.vfx.joint=5;scene_.Effect(4)->Play(4);}
        if(options_.animationCheck){settings_.cameraYaw=settings_.cameraPitch=0;settings_.cameraDistance=6;settings_.unlit=true;legacy_.animated={{0,0,0},0,1};auto& p=scene_.Animation(3)->playback;p.time=0.5f;p.playing=false;}
        if(options_.vfxDepthCheck){settings_.cameraYaw=settings_.cameraPitch=0;settings_.cameraDistance=6;settings_.unlit=true;legacy_.models[0]={{0.5f,0,0},0,1};legacy_.vfx.scale=1;}
        if (options_.assetCheck) {
            settings_.cameraYaw = settings_.cameraPitch = 0;
            settings_.cameraDistance = 6; settings_.unlit = true;
            legacy_.models[0] = ModelPose{{0.5f,0,0},0,1};
        }
        if (!options_.scene.empty()) {
            if (!scene_.Open(options_.scene)) throw std::runtime_error(scene_.Status());
        } else {
            ConfigureLegacyScene();
            settings_.diagnosticTriangle = scene_.Document().assets.empty() || options_.smoke;
        }
        if (overlay_) {
            overlay_->Initialize(window_, renderer_, scene_, log_);
            overlayReady_ = true;
        }
        ShowWindow(window_, SW_SHOW);
        log_.Write("Window shown; initialization complete (not yet proof of rendering)");
        auto previous = Clock::now();
        const auto smokeDeadline = previous + std::chrono::seconds(30);
        double titleTime = 0;
        double cpuTotal=0,cpuActiveTotal=0,gpuTotal=0;UINT64 measuredFrames=0;
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
            if ((options_.smoke || options_.assetSmoke || options_.integrationSmoke) && Clock::now() > smokeDeadline) throw std::runtime_error("Smoke test stalled for 30 seconds");
            if (options_.smoke || options_.assetSmoke || options_.integrationSmoke) {
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
            if(!options_.vfxUpdate.empty()) {
                if(stats_.frames==1){if(!ReloadVfx(options_.vfxUpdate))throw std::runtime_error("Changed VFX reload failed");scene_.Effect(4)->Play(1);}
                // ディレクトリの読込を失敗させ、正常な更新後の表示を画素比較で確認する。
                if(stats_.frames==2 && (ReloadVfx(options_.vfxUpdate.parent_path()) || scene_.Effect(4)->Instances()!=1))
                    throw std::runtime_error("Failed VFX reload changed retained instance");
            }
            if(options_.integrationSmoke) {
                if(stats_.frames==16 && !ReloadAnimation(options_.animation))throw std::runtime_error("Animation reload failed");
                if(stats_.frames==17){auto* old=scene_.Animation(3);if(ReloadAnimation(options_.animation.parent_path()/"invalid.cskin") || old!=scene_.Animation(3))throw std::runtime_error("Failed animation reload replaced old asset");}
                if(stats_.frames==32){if(!ReloadVfx(options_.vfx))throw std::runtime_error("Effect reload failed");scene_.Effect(4)->Play(4);}
                if(stats_.frames==33){const int count=scene_.Effect(4)->Instances();if(ReloadVfx(options_.vfx.parent_path()/"missing.efkefc") || count!=scene_.Effect(4)->Instances())throw std::runtime_error("Failed VFX reload changed playback");}
                if(stats_.frames==64){scene_.Effect(4)->Stop();if(scene_.Effect(4)->Instances()!=0)throw std::runtime_error("VFX stop failed");}
                if(stats_.frames==65)scene_.Effect(4)->Play(16);
                if(stats_.frames>=70)settings_.cameraYaw+=0.002f;
            }
            if (options_.assetCheck && !options_.assetUpdate.empty()) {
                if (stats_.frames == 1 && !ReloadModel(0,options_.assetUpdate))
                    throw std::runtime_error("Changed fixture reload failed");
                if (stats_.frames == 2) {
                    const auto generation = legacyGenerations_.at(0);
                    if (ReloadModel(0,options_.assetUpdate.parent_path()/L"invalid.cmesh") || legacyGenerations_.at(0) != generation)
                        throw std::runtime_error("Corrupt update replaced live model");
                }
            }
            if (options_.assetSmoke && stats_.frames >= 16 && stats_.frames < 24) {
                const auto generation = legacyGenerations_.at(0);
                if (stats_.frames % 2 == 0) {
                    if (!ReloadModel(0,options_.assets[0]) || legacyGenerations_.at(0) != generation+1)
                        throw std::runtime_error("Valid model reload failed");
                } else {
                    // A directory cannot be a valid cooked package. No user files are altered.
                    if (ReloadModel(0,options_.assets[0].parent_path()) || legacyGenerations_.at(0) != generation)
                        throw std::runtime_error("Failed reload changed live model generation");
                }
            }
            const auto start = Clock::now();
            const double dt = std::chrono::duration<double>(start - previous).count();
            previous = start;
            stats_.frameMs = dt * 1000;
            if (overlay_) overlay_->Build(stats_, input_, settings_);
            if (!overlay_ || !overlay_->CapturesKeyboard()) {
                if (input_.pressed[VK_ESCAPE] && (!overlay_ || overlay_->CanClose())) { running_ = false; continue; }
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
            const auto sceneSeconds = (options_.integrationSmoke||options_.vfxCheck)?1.0f/60.0f:static_cast<float>(std::min(dt,0.1));
            auto& activeScene = overlay_ ? overlay_->UpdateScene(scene_, sceneSeconds) : scene_;
            if (!overlay_) scene_.Update(sceneSeconds);
            if (!options_.integrationSmoke) {
                const auto& view = activeScene.Document().view;
                settings_.cameraYaw = view.yaw; settings_.cameraPitch = view.pitch; settings_.cameraDistance = view.distance;
                settings_.clear = view.background; settings_.unlit = view.unlit;
            }
            if (overlay_) overlay_->ConfigureView(settings_);
            auto* list = renderer_.BeginFrame(settings_, activeScene.DrawItems(), settings_.diagnosticTriangle);
            if (!settings_.diagnosticTriangle) activeScene.DrawEffects(list);
            renderer_.BeginOverlay();
            if (overlay_) overlay_->Draw(list);
            const bool last = options_.frameLimit && stats_.frames + 1 >= options_.frameLimit;
            const bool capture = last && (options_.smoke || options_.assetSmoke || options_.assetCheck || !options_.capture.empty());
            const auto beforePresent = Clock::now();
            stats_.cpuWorkMs = std::chrono::duration<double, std::milli>(beforePresent - start).count();
            stats_.fenceWaitMs = renderer_.FenceWaitMs();
            if(stats_.frames>=70){cpuTotal+=stats_.cpuWorkMs;cpuActiveTotal+=std::max(0.0,stats_.cpuWorkMs-renderer_.FenceWaitMs());gpuTotal+=renderer_.GpuFrameMs();++measuredFrames;}
            visible = renderer_.EndFrame(settings_.vsync, capture);
            ++stats_.frames;
            input_.pressed.fill(false);
            if (stats_.frames == 1) log_.Write("First command list submitted and Present returned successfully");
            titleTime += dt;
            if (titleTime > 0.5) {
                const auto title = std::format(L"{} | {:.2f} ms | {}", name_, stats_.frameMs,
                    settings_.diagnosticTriangle ? L"WASD/arrows: move | Space: reset | Esc: exit" : L"Scene | Esc: exit");
                CheckWin32(SetWindowTextW(window_, title.c_str()), "Update diagnostic title");
                titleTime = 0;
            }
            if (capture) {
                const auto check = options_.animationCheck?AssetCaptureCheck::Corners:options_.vfxDepthCheck?AssetCaptureCheck::Midtone:options_.vfxCheck?AssetCaptureCheck::VfxAlpha:options_.colorCheck ? AssetCaptureCheck::Midtone :
                    !options_.assetCheck ? AssetCaptureCheck::None :
                    options_.assetUpdate.empty() ? AssetCaptureCheck::Corners : AssetCaptureCheck::UpdatedCorners;
                renderer_.SaveCapture(options_.capture,options_.smoke,check);
            }
            if (last) SendMessageW(window_, WM_CLOSE, 0, 0);
        }
        if (windowError_) std::rethrow_exception(windowError_);
        renderer_.WaitIdle();
        if(options_.integrationSmoke && (!smokeResized_||!smokeRestoredSize_||!sawMinimize_||!sawRestore_||scene_.Effect(4)->Instances()!=16))throw std::runtime_error("Integration lifecycle/instances failed");
        if(measuredFrames)log_.Write(std::format("PROFILE frames={} CPU record incl fence wait avg={:.3f} ms CPU excl fence wait avg={:.3f} ms GPU render avg={:.3f} ms vsync={} adapter={}",measuredFrames,cpuTotal/static_cast<double>(measuredFrames),cpuActiveTotal/static_cast<double>(measuredFrames),gpuTotal/static_cast<double>(measuredFrames),settings_.vsync,renderer_.AdapterName()));
        if (overlay_) overlay_->Shutdown();
        overlayReady_ = false;
        // Include destruction-time backend messages in the result.
        renderer_.WaitIdle();
        if (options_.smoke && (!smokeResized_ || !smokeRestoredSize_ || !sawMinimize_ || !sawRestore_ || stats_.frames != 32))
            throw std::runtime_error("Smoke test lifecycle assertions failed");
        if (options_.assetSmoke && (!smokeResized_ || !smokeRestoredSize_ || !sawMinimize_ || !sawRestore_ || stats_.frames != 32 || legacyGenerations_.at(0) != 5))
            throw std::runtime_error("Asset smoke lifecycle/reload assertions failed");
        scene_.Clear();
        log_.Write(std::format("Shutdown: frames={}, Debug Layer errors={}, resize/minimize/restore={}/{}/{}",
            stats_.frames, renderer_.DiagnosticErrors(), smokeResized_, sawMinimize_, sawRestore_));
        if (renderer_.DiagnosticErrors()) return 1;
        if (options_.smoke) log_.Write("SMOKE PASS (pixel readback + window lifecycle + clean GPU drain)");
        if (options_.assetSmoke) log_.Write("ASSET SMOKE PASS (4 replacements, 4 rejected updates, window lifecycle, GPU drain; inspect capture separately)");
        return 0;
    }
private:
    void EnsureLegacyEntities() {
        if (!scene_.Document().entities.empty()) return;
        SceneDocument document;
        document.Add("Static 1"); document.Add("Static 2");
        document.Add("Character"); document.Add("Effect");
        if (!scene_.Apply(std::move(document))) throw std::runtime_error(scene_.Status());
    }
    bool ReloadModel(size_t slot, const std::filesystem::path& path) {
        EnsureLegacyEntities();
        const bool success = scene_.AssignAsset(slot + 1, path, SceneAssetKind::Static);
        if (success) ++legacyGenerations_.at(slot);
        return success;
    }
    bool ReloadAnimation(const std::filesystem::path& path) {
        EnsureLegacyEntities();
        return scene_.AssignAsset(3, path, SceneAssetKind::Skeletal);
    }
    bool ReloadVfx(const std::filesystem::path& path) {
        EnsureLegacyEntities();
        return scene_.AssignAsset(4, path, SceneAssetKind::Effect);
    }
    void ConfigureLegacyScene() {
        // 旧CLIの配置をSceneへ一度だけ変換する。以後の編集と描画は共通経路を通る。
        EnsureLegacyEntities();
        auto document = scene_.Document();
        const auto pose = [](const ModelPose& value) {
            Transform transform;
            transform.position = value.position;
            transform.scale.fill(value.scale);
            DirectX::XMFLOAT4 rotation;
            DirectX::XMStoreFloat4(&rotation, DirectX::XMQuaternionRotationAxis(DirectX::XMVectorSet(0,1,0,0), value.yaw));
            transform.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
            return transform;
        };
        document.Entity(1).local = pose(legacy_.models[0]);
        document.Entity(2).local = pose(legacy_.models[1]);
        document.Entity(3).local = pose(legacy_.animated);
        if (auto* animation = scene_.Animation(3)) {
            const auto& playback = animation->playback;
            document.Entity(3).animation = {animation->Asset().clips[playback.clip].name, playback.time, playback.speed,
                playback.loop, playback.playing, playback.restPose, playback.bindPose};
        }
        auto& effect = document.Entity(4);
        effect.local.position = legacy_.vfx.position;
        effect.local.scale.fill(legacy_.vfx.scale);
        effect.effect.count = options_.integrationSmoke ? 4 : legacy_.vfx.count;
        if (legacy_.vfx.followSocket && scene_.Animation(3)) {
            effect.effect.follow = 3;
            effect.effect.joint = scene_.Animation(3)->Asset().joints.at(legacy_.vfx.joint).name;
        }
        document.view = {settings_.clear, settings_.cameraYaw, settings_.cameraPitch, settings_.cameraDistance, settings_.unlit};
        if (!scene_.Apply(std::move(document))) throw std::runtime_error(scene_.Status());
        scene_.MarkClean();
    }
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
            if (w == 1 && (options_.smoke || options_.assetSmoke || options_.integrationSmoke)) { KillTimer(window, 1); ShowWindow(window, SW_RESTORE); log_.Write("Smoke timer restored window"); }
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
        case WM_CLOSE: if (overlayReady_ && !options_.frameLimit && !overlay_->CanClose()) return 0; running_ = false; log_.Write("WM_CLOSE requested orderly shutdown"); return 0;
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
    SceneRuntime scene_;
    LegacyPreview legacy_;
    std::array<uint64_t, 2> legacyGenerations_{};
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
        log->Write("Starting " + Utf8(name) + " | C++20 | Phase 0C");
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
