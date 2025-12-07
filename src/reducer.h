#pragma once

#include "app_state.h"
#include "action.h"

// Pure reducer function - NO SIDE EFFECTS
// Takes current state and action, returns new state
// If state is unchanged, returns state with same version
AppState reduce(const AppState* state, const Action* action);
