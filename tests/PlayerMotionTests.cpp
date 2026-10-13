#include "gameplay/PlayerMotion.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace chromalis;
namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Near(float actual, float expected) { Check(std::abs(actual - expected) < 0.0002f, "Motion result mismatch"); }
}
int main() {
    try {
        PlayerMotion straight, diagonal, run;
        straight.cameraYaw = diagonal.cameraYaw = run.cameraYaw = 0;
        for (int i = 0; i < 60; ++i) {
            straight.Update({0, 1}, 1.0f / 60);
            diagonal.Update({1, 1}, 1.0f / 60);
            run.Update({0, 1, 0, 0, true}, 1.0f / 60);
        }
        Near(straight.position[2], ScenePlaySettings{}.walkSpeed);
        Near(std::hypot(diagonal.position[0], diagonal.position[2]), ScenePlaySettings{}.walkSpeed);
        Near(run.position[2], ScenePlaySettings{}.runSpeed);
        Check(straight.state == Locomotion::Walk && run.state == Locomotion::Run, "Walk/run selection");
        const auto stopped = run.position;
        run.Update({}, 0.1f);
        Check(run.state == Locomotion::Idle && run.position == stopped, "Stop must not drift");
        PlayerMotion lowRate; lowRate.cameraYaw = 0;
        for (int i = 0; i < 30; ++i) lowRate.Update({0, 1}, 1.0f / 30);
        Near(lowRate.position[2], straight.position[2]);
        PlayerMotion rotated; rotated.cameraYaw = 1.57079632679f;
        rotated.Update({0, 1}, 0.1f);
        Near(rotated.position[0], -ScenePlaySettings{}.walkSpeed * 0.1f); Near(rotated.position[2], 0);
        PlayerMotion boundary; boundary.cameraYaw = 0;
        boundary.position = {11.5f, 0, 11.5f};
        boundary.Update({1, 1}, 0.1f);
        Check(boundary.state == Locomotion::Idle, "Blocked input should idle");
        boundary.Update({-1, 0}, 0.1f);
        Check(boundary.state == Locomotion::Walk, "Move away from edge");
        PlayerMotion longFrame; longFrame.cameraYaw = 0;
        longFrame.Update({0, 1}, 100);
        Near(longFrame.position[2], ScenePlaySettings{}.walkSpeed * 0.1f);
        for (int i = 0; i < 60; ++i) run.Update({}, 1.0f / 60);
        Near(run.cameraTarget[2], run.position[2]);
        run.Update({0, 0, 0, 0, false, true}, 0);
        Near(run.position[2], 0); Near(run.cameraDistance, 7);
        for (int i = 0; i < 60; ++i) run.Update({0, 0, 1, 1}, 0.1f);
        Near(run.cameraDistance, 37);
        run.cameraDistance = 22; run.Update({}, 0.1f); Near(run.cameraDistance, 22);
        PlayerMotion placed;
        placed.spawn = placed.position = {2,3,4}; placed.settings.groundHeight = 3;
        placed.settings.walkSpeed = 2; placed.settings.runSpeed = 5;
        placed.cameraYaw = placed.initialCameraYaw = 0;
        placed.Update({0,1},0.1f); Near(placed.position[2],4.2f); Near(placed.position[1],3);
        placed.Update({0,0,0,0,false,true},0); Near(placed.position[0],2); Near(placed.position[2],4);
        bool rejected = false;
        try { run.Update({}, std::numeric_limits<float>::quiet_NaN()); } catch (...) { rejected = true; }
        Check(rejected, "Invalid timestep accepted");
        std::cout << "Player motion PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
