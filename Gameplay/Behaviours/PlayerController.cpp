#include "PlayerController.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chromalis {
void PlayerController::Start(BehaviourContext& context) {
    auto& scene = context.scene;
    const auto& document = scene.Document();
    motion_ = {}; motion_.settings = document.play;
    auto& configured = motion_.settings;
    configured.walkSpeed = settings_.at("walkSpeed"); configured.runSpeed = settings_.at("runSpeed");
    configured.turnSpeed = settings_.at("turnSpeed"); configured.forwardYaw = settings_.at("forwardYaw");
    configured.groundHeight = settings_.at("groundHeight");
    configured.minimum = {settings_.at("minX"), settings_.at("minZ")};
    configured.maximum = {settings_.at("maxX"), settings_.at("maxZ")};
    configured.clips = {settings_.at("idleClip"), settings_.at("walkClip"), settings_.at("runClip")};
    configured.cameraTarget = std::stoull(settings_.at("cameraTarget").get<std::string>());
    configured.cameraHeight = settings_.at("cameraHeight"); configured.cameraFollow = settings_.at("cameraFollow");
    if (configured.runSpeed < configured.walkSpeed || configured.minimum[0] >= configured.maximum[0] || configured.minimum[1] >= configured.maximum[1])
        throw std::runtime_error("Invalid PlayerController speed/bounds");
    motion_.cameraYaw = motion_.initialCameraYaw = document.view.yaw;
    motion_.cameraDistance = motion_.initialCameraDistance = document.view.distance;
    player_ = context.entity;
    if (!player_) return;
    const auto& entity = document.Entity(player_);
    if (entity.parent || !scene.Animation(player_)) throw std::runtime_error("Player must reference a root skeletal entity");
    initial_ = entity.local;
    motion_.position = motion_.spawn = initial_.position;
    const auto& p = motion_.settings;
    if (std::abs(initial_.position[1] - p.groundHeight) > 0.0001f ||
        initial_.position[0] < p.minimum[0] || initial_.position[0] > p.maximum[0] ||
        initial_.position[2] < p.minimum[1] || initial_.position[2] > p.maximum[1])
        throw std::runtime_error("Player spawn is outside the configured movement plane/bounds");
    // 配置時の向きを初期状態に保ち、初回の移動だけで勝手に半回転しない。
    const auto& q = initial_.rotation;
    if (std::abs(q[0]) > 0.0001f || std::abs(q[2]) > 0.0001f)
        throw std::runtime_error("Flat-plane player must be upright (Y-axis rotation only)");
    motion_.yaw = motion_.spawnYaw = std::atan2(2 * (q[3]*q[1] + q[0]*q[2]), 1 - 2 * (q[1]*q[1] + q[0]*q[0])) - p.forwardYaw;
    motion_.cameraTarget = {motion_.position[0], motion_.position[1] + p.cameraHeight, motion_.position[2]};
    auto& player = *scene.Animation(player_);
    const auto& names = motion_.settings.clips;
    for (size_t i = 0; i < names.size(); ++i) {
        const auto clip = std::find_if(player.Clips().begin(), player.Clips().end(), [&](const auto& value) { return value.name == names[i]; });
        if (clip == player.Clips().end()) throw std::runtime_error(std::string("Missing player clip: ") + names[i]);
        clips_[i] = static_cast<int>(clip - player.Clips().begin());
    }
    player.playback = {clips_[0], 0, 1, true, true, false, false};
    previous_ = Locomotion::Idle;
}
void PlayerController::Update(BehaviourContext& context, float seconds) {
    auto& scene = context.scene;
    const auto& input = context.input;
    if (!player_) return;
    const auto axis = [&](int positive, int negative) { return float(input.down[positive]) - float(input.down[negative]); };
    PlayerCommand command;
    command.right = float(input.down['D'] || input.down[VK_RIGHT]) - float(input.down['A'] || input.down[VK_LEFT]);
    command.forward = float(input.down['W'] || input.down[VK_UP]) - float(input.down['S'] || input.down[VK_DOWN]);
    command.run = input.down[VK_SHIFT]; command.orbit = axis('E', 'Q');
    command.zoom = axis(VK_NEXT, VK_PRIOR); command.reset = input.pressed['R'];
    motion_.position = scene.RuntimeTransform(player_).position;
    motion_.Update(command, seconds);
    auto pose = initial_;
    pose.position = motion_.position;
    // 素材ごとの前方補正はScene設定で与える。
    const float angle = motion_.yaw + motion_.settings.forwardYaw;
    pose.rotation = {0, std::sin(angle * 0.5f), 0, std::cos(angle * 0.5f)};
    scene.SetRuntimeTransform(player_, pose);
    auto& player = *scene.Animation(player_);
    const auto nextClip = clips_[static_cast<size_t>(motion_.state)];
    if (previous_ != motion_.state || command.reset) {
        // 歩行と走行の周期を引き継ぎ、姿勢は共通のクロスフェードで接続する。
        const bool moving = previous_ != Locomotion::Idle && motion_.state != Locomotion::Idle && !command.reset;
        const float phase = moving ? player.playback.time / player.Clips()[player.playback.clip].duration : 0;
        player.CrossFade(nextClip, command.reset ? 0.0f : settings_.at("transitionDuration").get<float>(), phase * player.Clips()[nextClip].duration);
        previous_ = motion_.state;
    }
    player.playback.speed = motion_.state == Locomotion::Idle ? 1 : std::clamp(motion_.speed /
        (motion_.state == Locomotion::Walk ? motion_.settings.walkSpeed : motion_.settings.runSpeed), 0.1f, 1.0f);
}
void PlayerController::ConfigureView(const SceneRuntime& scene, RenderSettings& settings) const {
    const auto& document = scene.Document();
    settings.cameraYaw = motion_.cameraYaw;
    settings.cameraPitch = document.view.pitch;
    settings.cameraDistance = motion_.cameraDistance;
    settings.cameraTarget = motion_.settings.fixedTarget;
    if (motion_.settings.cameraTarget == player_ && player_) settings.cameraTarget = motion_.cameraTarget;
    else if (motion_.settings.cameraTarget) {
        const auto& world = scene.World(motion_.settings.cameraTarget);
        settings.cameraTarget = {world._41, world._42 + motion_.settings.cameraHeight, world._43};
    }
}
BehaviourType PlayerController::Describe() {
    const ScenePlaySettings p;
    return {"PlayerController", {
        {"walkSpeed",p.walkSpeed,0.001,100}, {"runSpeed",p.runSpeed,0.001,100}, {"turnSpeed",p.turnSpeed,0.001,100},
        {"forwardYaw",p.forwardYaw}, {"groundHeight",0.0f},
        {"minX",-11.5f}, {"minZ",-11.5f}, {"maxX",11.5f}, {"maxZ",11.5f},
        {"idleClip","Idle"}, {"walkClip","Walk"}, {"runClip","Run"},
        {"transitionDuration",0.18f,0,2},
        {"cameraTarget","0",0,0,BehaviourFieldKind::Entity}, {"cameraHeight",0.9f}, {"cameraFollow",10.0f,0.001,100}},
        [](const BehaviourSettings& settings) { return std::make_unique<PlayerController>(settings); }, true, true, 1, {}};
}
}
