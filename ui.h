#ifndef UI_H
#define UI_H

#include "app.h"

bool ui_load_assets(AppState *app);
void ui_release_assets(AppState *app);
void ui_draw(AppState *app, HDC hdc, const RECT *clientRect);

void ui_draw_shadowed_text(
    HDC hdc,
    HFONT font,
    const wchar_t *text,
    int x,
    int y,
    COLORREF color,
    COLORREF shadowColor,
    int shadowDx,
    int shadowDy,
    const RECT *clipRect,
    UINT format
);

#endif