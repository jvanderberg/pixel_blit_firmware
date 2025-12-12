#pragma once

#include "app_state.h"
#include "../string_test.h"
#include "../toggle_test.h"
#include "../rainbow_test.h"
#include "../string_length_test.h"
#include "../fseq_player.h"
#include "../sh1106.h"

// Hardware context passed to side effects
typedef struct {
    sh1106_t* display;
    string_test_t* string_test;
    toggle_test_t* toggle_test;
    rainbow_test_t* rainbow_test;
    string_length_test_t* string_length_test;
    fseq_player_t* fseq_player;
} HardwareContext;

// Initialize hardware context
void side_effects_init(HardwareContext* hw);

// Apply side effects based on state change
// Called when state.version changes
void side_effects_apply(const HardwareContext* hw,
                        const AppState* old_state,
                        const AppState* new_state);

// Called every loop iteration for running tests
// Returns true if rainbow frame was output (for FPS tracking)
bool side_effects_tick(const HardwareContext* hw, const AppState* state);

// Get current rainbow FPS
uint16_t side_effects_get_rainbow_fps(const HardwareContext* hw);
