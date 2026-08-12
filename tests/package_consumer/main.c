#include <tinta_core.h>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;
    WNDCLASSEXW editor_class;
    if (FAILED(TintaCoreInitialize())) return 1;
    ZeroMemory(&editor_class, sizeof(editor_class));
    editor_class.cbSize = sizeof(editor_class);
    if (!GetClassInfoExW(NULL, TINTA_TEXT_EDITOR_CLASSW, &editor_class) ||
        TINTA_CORE_VERSION_MINOR < 3) {
        TintaCoreUninitialize();
        return 1;
    }
    TintaCoreUninitialize();
    return 0;
}
