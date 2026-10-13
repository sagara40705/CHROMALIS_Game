#include "gameplay/ScenePlayback.h"
#include <fstream>
#include <iostream>
#include <cmath>

using namespace chromalis;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F&& f) { bool failed = false; try { f(); } catch (...) { failed = true; } Require(failed, "Expected rejection"); }
struct Counts { int start = 0, update = 0, pose = 0, stop = 0; bool fail = false; };
class Probe final : public Behaviour {
public:
    Probe(Counts& counts, float speed) : counts_(counts), speed_(speed) {}
    void Start(BehaviourContext&) override { ++counts_.start; if (counts_.fail) throw std::runtime_error("Start failure"); }
    void Update(BehaviourContext& context, float dt) override {
        ++counts_.update;
        auto transform = context.scene.RuntimeTransform(context.entity);
        transform.position[0] += dt * speed_;
        context.scene.SetRuntimeTransform(context.entity, transform);
    }
    void AfterPose(BehaviourContext& context, float) override {
        ++counts_.pose;
        Require(context.scene.World(context.entity)._41 == context.scene.RuntimeTransform(context.entity).position[0], "AfterPose ran before World evaluation");
    }
    void Stop(BehaviourContext&) noexcept override { ++counts_.stop; }
private:
    Counts& counts_;
    float speed_;
};
void Run(const std::filesystem::path& root, Renderer& renderer, Log& log) {
    Counts counts;
    BehaviourRegistry registry;
    registry.Add({"Probe", {{"speed",2.0f,0,100},{"target","0",0,0,BehaviourFieldKind::Entity}},
        [&](const BehaviourSettings& settings) { return std::make_unique<Probe>(counts, settings.at("speed").get<float>()); },true,false,1,{}}, "test.probe", "RegisteredProbe.cpp");
    // 表示ファイル名を変更しても実行名・Sceneの識別子は変更しない。
    Require(registry.Find("test.probe")->sourceFilename == "RegisteredProbe.cpp" && registry.Find("test.probe")->name == "Probe", "Behaviour file metadata replaced runtime name");
    Require(GameBehaviours().Find("chromalis.player")->sourceFilename == "PlayerController.cpp", "Generated registration lost source filename");
    SceneDocument document; document.legacyBehaviours = false;
    const auto id = document.Add("Object");
    // 移行済みSceneでは旧player参照を実行しない。Behaviourを外せば空オブジェクトにも戻せる。
    document.play.player = id;
    document.Entity(id).behaviours.push_back(MakeBehaviour(*registry.Find("test.probe"), document.Entity(id)));
    SaveScene(document, root / "behaviour.cscene");
    auto loaded = LoadScene(root / "behaviour.cscene");
    Require(loaded.Entity(id).behaviours[0].settings == document.Entity(id).behaviours[0].settings, "Settings roundtrip");
    auto bad = loaded; bad.Entity(id).behaviours[0].settings = R"({"speed":-1})";
    Reject([&] { ResolveBehaviourSettings(*registry.Find("test.probe"), bad.Entity(id).behaviours[0], bad); });
    bad.Entity(id).behaviours[0].settings = R"({"target":"999"})";
    Reject([&] { ResolveBehaviourSettings(*registry.Find("test.probe"), bad.Entity(id).behaviours[0], bad); });
    bad = loaded; bad.Entity(id).behaviours[0].type = "missing.type";
    bad.Entity(id).behaviours[0].settings = R"({"future":{"nested":[1,2,3]},"name":"保持"})";
    SaveScene(bad, root / "missing.cscene");
    const auto missing = LoadScene(root / "missing.cscene");
    Require(BehaviourSettings::parse(missing.Entity(id).behaviours[0].settings) == BehaviourSettings::parse(bad.Entity(id).behaviours[0].settings), "Missing settings lost");
    SceneRuntime scene(renderer, log);
    Require(scene.Apply(loaded), "Apply probe scene"); scene.MarkClean();
    ScenePlayback playback;
    playback.Initialize(scene, registry);
    InputState input;
    playback.Update(scene, input, 0.05f); scene.Update(0.05f); playback.AfterPose(scene,input,0.05f);
    Require(scene.World(id)._41 == 0.1f && !scene.Dirty() && scene.Document().Entity(id).local.position[0] == 0, "Runtime leaked into document");
    Require(counts.start == 1 && counts.update == 1 && counts.pose == 1, "Lifecycle counts");
    playback.Stop(); playback.Stop(); Require(counts.stop == 1, "Stop not exactly once");
    Require(scene.Apply(missing), "Missing definition must remain editable");
    Reject([&] { playback.Initialize(scene, registry); });
    bad = missing; bad.Entity(id).behaviours[0].enabled = false;
    Require(scene.Apply(bad), "Disabled missing apply"); playback.Initialize(scene,registry); playback.Stop();
    bad = loaded; bad.Entity(id).behaviours.push_back(MakeBehaviour(*registry.Find("test.probe"),bad.Entity(id)));
    Require(scene.Apply(bad), "Writer conflict should remain editable");
    Reject([&] { playback.Initialize(scene,registry); });
    Require(counts.start == 1, "Conflict must reject before Start");
    Require(scene.Apply(loaded), "Restore probe"); counts.fail = true;
    Reject([&] { playback.Initialize(scene,registry); });
    Require(counts.stop == 2, "Failed Start not cleaned up"); counts.fail = false;
    playback.Initialize(scene,registry); playback.Stop(); Require(counts.stop == 3, "Restart cleanup");
    // 別ビルドでEditor生成ソースを実装・登録した場合、そのままSceneから実行する。
    for (const auto& type : GameBehaviours().Types()) if (type.name == "AuthoredMotion") {
        scene.Clear();
        document.Entity(id).behaviours = {MakeBehaviour(type, document.Entity(id))};
        Require(scene.Apply(document), "Generated Behaviour apply"); scene.MarkClean();
        playback.Initialize(scene); playback.Update(scene,input,0.1f); scene.Update(0.1f);
        Require(std::abs(scene.World(id)._41 - 0.1f) < 0.0001f, "Generated/edited/compiled Behaviour did not move");
        playback.Stop(); SaveScene(document,root/"AuthoredMotion.cscene");
        std::cout << "GENERATED BEHAVIOUR PLAY PASS\n";
    }
    scene.Clear(); renderer.WaitIdle(); renderer.DrainDiagnostics();
    Require(renderer.DiagnosticErrors() == 0, "D3D diagnostics");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Expected output directory");
        const auto root = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(root);
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"BehaviourTest";
        CheckWin32(RegisterClassW(&wc),"Register Behaviour test");
        const HWND window = CreateWindowW(wc.lpszClassName,L"Behaviour test",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,wc.hInstance,nullptr);
        CheckWin32(window != nullptr,"Create Behaviour test window");
        { Log log(root/"behaviour.log"); Renderer renderer(log); renderer.Initialize(window,640,480,{true,true,false}); Run(root,renderer,log); }
        DestroyWindow(window); UnregisterClassW(wc.lpszClassName,wc.hInstance);
        std::cout << "PASS: settings, missing types, lifecycle, conflicts, retained authoring state, generated runtime\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
