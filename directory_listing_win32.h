#ifndef DIRECTORY_LISTING_WIN32_H
#define DIRECTORY_LISTING_WIN32_H

#include "app.h"
#include <stdbool.h>

bool directory_listing_init(AppState *app, const wchar_t *rootPath);
void directory_listing_shutdown(AppState *app);
bool directory_listing_open_module(AppState *app, const wchar_t *path);
void directory_listing_draw(AppState *app, HDC hdc);
bool directory_listing_mouse_down(AppState *app, int x, int y);
void directory_listing_mouse_wheel(AppState *app, int wheelDelta);
bool directory_listing_move_to_neighbor(AppState *app, int step);

#endif
