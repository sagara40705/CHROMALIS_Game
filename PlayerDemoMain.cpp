#include "gameplay/PlayerDemo.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    chromalis::PlayerDemo demo;
    return chromalis::RunApplication(instance, L"CHROMALIS Player Demo", nullptr, &demo);
}
