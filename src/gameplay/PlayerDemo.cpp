#include "gameplay/PlayerDemo.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chromalis {
void PlayerDemo::Initialize(SceneRuntime& scene) {
    const auto& entities = scene.Document().entities;
    const auto found = std::find_if(entities.begin(), entities.end(), [](const auto& entity) { return entity.name == "Player"; });
    if (found == entities.end() || found->parent || !scene.Animation(found->id))
        throw std::runtime_error("Player demo requires --scene with a root skeletal entity named Player");
    player_ = found->id; initial_ = found->local;
    motion_ = {}; motion_.position = initial_.position;
    if (motion_.position[1] != 0 || std::abs(motion_.position[0]) > PlayerMotion::MapLimit || std::abs(motion_.position[2]) > PlayerMotion::MapLimit)
        throw std::runtime_error("Player spawn must be on the demo map at y=0");
    motion_.cameraTarget = {motion_.position[0], 0.9f, motion_.position[2]};
    auto& player = *scene.Animation(player_);
    const std::array names{"Idle", "Walk", "Run"};
    for (size_t i = 0; i < names.size(); ++i) {
        const auto clip = std::find_if(player.Clips().begin(), player.Clips().end(), [&](const auto& value) { return value.name == names[i]; });
        if (clip == player.Clips().end()) throw std::runtime_error(std::string("Missing player clip: ") + names[i]);
        clips_[i] = static_cast<int>(clip - player.Clips().begin());
    }
    player.playback = {clips_[0], 0, 1, true, true, false, false};
    previous_ = Locomotion::Idle;
}
void PlayerDemo::Update(SceneRuntime& scene, const InputState& input, float seconds, RenderSettings& settings) {
    const auto axis = [&](int positive, int negative) { return float(input.down[positive]) - float(input.down[negative]); };
    PlayerCommand command;
    command.right = float(input.down['D'] || input.down[VK_RIGHT]) - float(input.down['A'] || input.down[VK_LEFT]);
    command.forward = float(input.down['W'] || input.down[VK_UP]) - float(input.down['S'] || input.down[VK_DOWN]);
    command.run = input.down[VK_SHIFT]; command.orbit = axis('E', 'Q');
    command.zoom = axis(VK_NEXT, VK_PRIOR); command.reset = input.pressed['R'];
    motion_.Update(command, seconds);
    auto pose = initial_;
    pose.position = motion_.position;
    // HumanoidDemoの前方は変換後に-Z。移動方向へ向けるため半回転を加える。
    const float angle = motion_.yaw + DirectX::XM_PI;
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
        (motion_.state == Locomotion::Walk ? PlayerMotion::WalkSpeed : PlayerMotion::RunSpeed), 0.1f, 1.0f);
    settings.diagnosticTriangle = false;
    settings.cameraTarget = motion_.cameraTarget;
    settings.cameraYaw = motion_.cameraYaw; settings.cameraPitch = 0.55f; settings.cameraDistance = motion_.cameraDistance;
}
}
