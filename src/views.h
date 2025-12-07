#pragma once

#include "app_state.h"
#include "../sh1106.h"

// Render the appropriate view based on current state
void views_render(sh1106_t* display, const AppState* state);
