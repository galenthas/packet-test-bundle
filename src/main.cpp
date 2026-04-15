#include "app.h"

#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();
    App app;
    return app.run();
}
