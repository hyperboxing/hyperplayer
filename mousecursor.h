#ifndef MOUSECURSOR_H
#define MOUSECURSOR_H

#include "app.h"
#include <stdbool.h>

bool mousecursor_load(AppState *app);
void mousecursor_unload(void);
HCURSOR mousecursor_get(void);
void mousecursor_apply(HWND hwnd);

#endif