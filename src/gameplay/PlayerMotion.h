#pragma once
#include "scene/Scene.h"
#include <array>

namespace chromalis {
enum class Locomotion { Idle, Walk, Run };
struct PlayerCommand {
    float right = 0, forward = 0, orbit = 0, zoom = 0;
    bool run = false, reset = false;
};
// 平面移動の状態。配置・速度・範囲はSceneから初期化する。
struct PlayerMotion {
    ScenePlaySettings settings;
    std::array<float, 3> position{}, spawn{}, cameraTarget{0, 0.9f, 0};
    float yaw = 0, spawnYaw = 0, cameraYaw = 0.35f, cameraDistance = 7;
    float initialCameraYaw = 0.35f, initialCameraDistance = 7;
    float speed = 0;
    Locomotion state = Locomotion::Idle;
    void Update(const PlayerCommand& command, float seconds);
};
}
