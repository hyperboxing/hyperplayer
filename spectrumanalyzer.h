#ifndef SPECTRUMANALYZER_H
#define SPECTRUMANALYZER_H

#include "app.h"

void spectrumanalyzer_update(AppState *app, double dt);
void spectrumanalyzer_draw(AppState *app, HDC hdc);

#endif