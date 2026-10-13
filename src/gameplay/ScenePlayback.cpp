#include "gameplay/ScenePlayback.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chromalis {
void ScenePlayback::Initialize(SceneRuntime& scene) {
    const auto& document = scene.Document();
    motion_ = {}; motion_.settings = document.play;
    motion_.cameraYaw = motion_.initialCameraYaw = document.view.yaw;
    motion_.cameraDistance = motion_.initialCameraDistance = document.view.distance;
    player_ = document.play.player;
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
    const auto& names = document.play.clips;
    for (size_t i = 0; i < names.size(); ++i) {
        const auto clip = std::find_if(player.Clips().begin(), player.Clips().end(), [&](const auto& value) { return value.name == names[i]; });
        if (clip == player.Clips().end()) throw std::runtime_error(std::string("Missing player clip: ") + names[i]);
        clips_[i] = static_cast<int>(clip - player.Clips().begin());
    }
    player.playback = {clips_[0], 0, 1, true, true, false, false};
    previous_ = Locomotion::Idle;
}
void ScenePlayback::Update(SceneRuntime& scene, const InputState& input, float seconds) {
    if (!player_) return;
    const auto axis = [&](int positive, int negative) { return float(input.down[positive]) - float(input.down[negative]); };
    PlayerCommand command;
    command.right = float(input.down['D'] || input.down[VK_RIGHT]) - float(input.down['A'] || input.down[VK_LEFT]);
    command.forward = float(input.down['W'] || input.down[VK_UP]) - float(input.down['S'] || input.down[VK_DOWN]);
    command.run = input.down[VK_SHIFT]; command.orbit = axis('E', 'Q');
    command.zoom = axis(VK_NEXT, VK_PRIOR); command.reset = input.pressed['R'];
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
        // 歩行と走行の周期だけを引き継ぐ。姿勢のクロスフェードは別の機能。
        const bool moving = previous_ != Locomotion::Idle && motion_.state != Locomotion::Idle && !command.reset;
        const float phase = moving ? player.playback.time / player.Clips()[player.playback.clip].duration : 0;
        player.playback.clip = nextClip;
        player.playback.time = phase * player.Clips()[nextClip].duration;
        previous_ = motion_.state;
    }
    player.playback.speed = motion_.state == Locomotion::Idle ? 1 : std::clamp(motion_.speed /
        (motion_.state == Locomotion::Walk ? motion_.settings.walkSpeed : motion_.settings.runSpeed), 0.1f, 1.0f);
}
void ScenePlayback::ConfigureView(const SceneRuntime& scene, RenderSettings& settings) const {
    const auto& document = scene.Document();
    settings.cameraYaw = motion_.cameraYaw;
    settings.cameraPitch = document.view.pitch;
    settings.cameraDistance = motion_.cameraDistance;
    settings.cameraTarget = document.play.fixedTarget;
    if (document.play.cameraTarget == player_ && player_) settings.cameraTarget = motion_.cameraTarget;
    else if (document.play.cameraTarget) {
        const auto& world = scene.World(document.play.cameraTarget);
        settings.cameraTarget = {world._41, world._42 + document.play.cameraHeight, world._43};
    }
}
}
