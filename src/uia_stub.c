#include "uia_provider.h"

LRESULT tinta_uia_get_object(TintaApp *app, WPARAM wparam, LPARAM lparam) {
    (void)app;
    (void)wparam;
    (void)lparam;
    return 0;
}

void tinta_uia_disconnect(TintaApp *app) { (void)app; }
void tinta_uia_raise_text_changed(TintaApp *app) { (void)app; }
void tinta_uia_raise_selection_changed(TintaApp *app) { (void)app; }
