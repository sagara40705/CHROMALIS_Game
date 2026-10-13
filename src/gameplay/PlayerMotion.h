#pragma once
#include <array>

namespace chromalis {
enum class Locomotion { Idle, Walk, Run };
struct PlayerCommand {
    float right = 0, forward = 0, orbit = 0, zoom = 0;
    bool run = false, reset = false;
};
// 平面デモの調整値。汎用の物理・キャラクター仕様にはしない。
struct PlayerMotion {
    // HumanoidDemoの接地中の足移動量 / 接地時間から求めた等倍再生時の速度。
    static constexpr float WalkSpeed = 0.34f / 0.60f, RunSpeed = 0.44f / (0.38f * 0.75f), MapLimit = 11.5f;
    std::array<float, 3> position{}, cameraTarget{0, 0.9f, 0};
    float yaw = 0, cameraYaw = 0.35f, cameraDistance = 7;
    float speed = 0;
    Locomotion state = Locomotion::Idle;
    void Update(const PlayerCommand& command, float seconds);
};
}
