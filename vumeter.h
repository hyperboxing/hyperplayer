#ifndef VUMETER_H
#define VUMETER_H

#include "app.h"

void vumeter_update(AppState *app, double dt);
void vumeter_draw(AppState *app, HDC hdc);

#endif