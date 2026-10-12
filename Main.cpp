#include "runtime/Application.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    return chromalis::RunApplication(instance, L"CHROMALIS Game", nullptr);
}
