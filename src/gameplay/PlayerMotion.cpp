#include "gameplay/PlayerMotion.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace chromalis {
void PlayerMotion::Update(const PlayerCommand& command, float seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(command.right) ||
        !std::isfinite(command.forward) || !std::isfinite(command.orbit) || !std::isfinite(command.zoom))
        throw std::runtime_error("Invalid player input/time");
    // 復帰直後の長いフレームで飛ばない。最小化中はホストが更新しない。
    const float dt = std::min(seconds, 0.1f);
    if (command.reset) *this = PlayerMotion{};
    cameraYaw = std::remainder(cameraYaw + std::clamp(command.orbit, -1.0f, 1.0f) * dt * 1.5f, 2 * std::numbers::pi_v<float>);
    cameraDistance = std::clamp(cameraDistance + std::clamp(command.zoom, -1.0f, 1.0f) * dt * 5, 4.0f, 12.0f);
    float x = std::clamp(command.right, -1.0f, 1.0f), z = std::clamp(command.forward, -1.0f, 1.0f);
    const float length = std::hypot(x, z);
    if (length > 1) { x /= length; z /= length; }
    // 画面右とカメラの前方をXZ平面へ投影した方向で移動する。
    const float c = std::cos(cameraYaw), s = std::sin(cameraYaw);
    const float dx = x * c - z * s, dz = x * s + z * c;
    const float requestedSpeed = command.run ? RunSpeed : WalkSpeed;
    const auto previous = position;
    position[0] = std::clamp(position[0] + dx * requestedSpeed * dt, -MapLimit, MapLimit);
    position[2] = std::clamp(position[2] + dz * requestedSpeed * dt, -MapLimit, MapLimit);
    position[1] = 0;
    speed = dt > 0 ? std::hypot(position[0] - previous[0], position[2] - previous[2]) / dt : 0;
    state = speed < 0.001f ? Locomotion::Idle : command.run ? Locomotion::Run : Locomotion::Walk;
    if (speed >= 0.001f) {
        const float target = std::atan2(position[0] - previous[0], position[2] - previous[2]);
        const float turn = std::remainder(target - yaw, 2 * std::numbers::pi_v<float>);
        yaw = std::remainder(yaw + std::clamp(turn, -12 * dt, 12 * dt), 2 * std::numbers::pi_v<float>);
    }
    // 描画だけを滑らかに追従させ、プレイヤーの移動には遅延を加えない。
    const float follow = 1 - std::exp(-10 * dt);
    for (size_t i = 0; i < 3; ++i)
        cameraTarget[i] += (position[i] + (i == 1 ? 0.9f : 0) - cameraTarget[i]) * follow;
}
}
