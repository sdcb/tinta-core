#include <tinta_core.h>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;
    if (FAILED(TintaCoreInitialize())) return 1;
    TintaCoreUninitialize();
    return 0;
}
