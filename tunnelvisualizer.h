#ifndef TUNNELVISUALIZER_H
#define TUNNELVISUALIZER_H

#include "app.h"
#include <stdbool.h>

void tunnelvisualizer_update(AppState *app, double dt);
bool tunnelvisualizer_mouse_down(AppState *app, int x, int y);
void tunnelvisualizer_draw(AppState *app, HDC hdc);

#endif