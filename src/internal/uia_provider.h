#ifndef TINTA_UIA_PROVIDER_H
#define TINTA_UIA_PROVIDER_H

#include "app.h"

LRESULT tinta_uia_get_object(TintaApp *app, WPARAM wparam, LPARAM lparam);
void tinta_uia_disconnect(TintaApp *app);
void tinta_uia_raise_text_changed(TintaApp *app);
void tinta_uia_raise_selection_changed(TintaApp *app);

#endif
