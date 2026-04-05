#ifndef SAMPLE_DISPLAY_H
#define SAMPLE_DISPLAY_H

#include "app.h"

void sample_display_update(AppState *app, double dt);
void sample_display_draw(AppState *app, HDC hdc);

#endif