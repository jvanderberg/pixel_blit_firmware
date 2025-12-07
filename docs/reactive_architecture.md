# Reactive Architecture Pattern

This document describes the Redux-inspired unidirectional data flow architecture used in the pixel_blit firmware. This pattern separates state management from side effects, making the code more testable, predictable, and maintainable.

## Overview

The architecture follows a strict unidirectional data flow:

```
Hardware Events → Actions → Pure Reducer → New State
                                    ↓
                              (if dirty)
                                    ↓
                         SideEffects + View Update
```

## Core 0 vs Core 1 (Control Plane vs Data Plane)

To ensure high-performance LED driving without blocking the UI, the system is split between two cores:

*   **Core 0 (Control Plane)**: Runs the Reactive/Redux loop (UI, Buttons, State Management). It is the "Single Source of Truth" for application configuration.
*   **Core 1 (Data Plane)**: Runs high-speed animation loops. It treats the AppState configuration as read-only inputs and drives the hardware directly.

### Communication Strategy
*   **Core 0 -> Core 1**: Core 0 writes configuration (e.g., `current_string`, `run_state`) to the `RainbowTestState` context. These fields are marked `volatile`.
*   **Core 1 -> Core 0**: Core 1 writes telemetry (e.g., `fps`) to the context.
*   **No Shared Logic**: The Reducer on Core 0 does *not* simulate animation physics (like incrementing hue). It only manages the "What" (which test is running), while Core 1 manages the "How" (rendering pixels).

## Core Components

### 1. AppState (Immutable State Container)

A single struct containing all application state. Key principles:

- **Immutable**: Never modify fields directly; always create a new state
- **Version tracking**: A `version` field increments on every change for O(1) dirty detection
- **Complete**: Contains all UI state, hardware state, and configuration

```c
typedef struct {
    uint32_t version;           // Incremented on every state change

    // UI State
    menu_state_t menu_state;
    uint8_t menu_selection;
    bool in_detail_view;

    // Hardware State
    uint8_t current_string;
    bool output_enabled;
    uint8_t brightness;

    // Animation State
    uint8_t hue_offset;
    uint32_t frame_count;
    uint16_t fps;
} AppState;
```

**Dirty Detection:**
```c
static inline bool app_state_dirty(const AppState* old, const AppState* new) {
    return old->version != new->version;
}
```

### 2. Actions (Immutable Event Objects)

Actions represent **what happened**, not what to do. They are created at the point of detection and carry all necessary data.

```c
typedef enum {
    ACTION_BUTTON_SELECT,
    ACTION_BUTTON_NEXT,
    ACTION_TICK,            // Periodic timer tick
    ACTION_ENCODER_ROTATE,  // If encoder is added
} ActionType;

typedef struct {
    ActionType type;
    uint32_t timestamp;
    union {
        int8_t encoder_delta;
        uint16_t adc_value;
    } payload;
} Action;

// Factory functions for type-safe creation
Action action_button_select(void);
Action action_button_next(void);
Action action_tick(void);
```

### 3. StateReducer (Pure Function)

The reducer is a **pure function** that takes the current state and an action, and returns a new state. Critical guarantees:

- **No side effects**: No hardware access, I/O, or timers
- **No mutation**: Input state is never modified
- **Deterministic**: Same inputs always produce same outputs
- **Returns original if unchanged**: Version stays the same if nothing changed

```c
AppState reduce(const AppState* state, const Action* action) {
    switch (action->type) {
        case ACTION_BUTTON_SELECT:
            return handle_select(state);
        case ACTION_BUTTON_NEXT:
            return handle_next(state);
        case ACTION_TICK:
            return handle_tick(state);
        default:
            return *state;  // Return unchanged
    }
}

// Helper that creates new state with incremented version
static AppState new_state_from(const AppState* state) {
    AppState new = *state;
    new.version++;
    return new;
}

static AppState handle_select(const AppState* state) {
    if (state->in_detail_view && state->menu_state == MENU_RAINBOW_TEST) {
        AppState new = new_state_from(state);
        new.current_string = (state->current_string + 1) % 32;
        return new;
    }
    // ... other handling
    return *state;
}
```

### 4. SideEffects (Impure Operations)

All hardware I/O is isolated in the SideEffects module. It receives both old and new state, allowing it to determine what changed and apply only necessary updates.

```c
void side_effects_apply(const AppState* old_state, const AppState* new_state) {
    // Only update display if UI state changed
    if (ui_state_changed(old_state, new_state)) {
        display_render(new_state);
    }

    // Only update LEDs if output state changed
    if (output_state_changed(old_state, new_state)) {
        if (new_state->output_enabled) {
            led_output_start(new_state->current_string);
        } else {
            led_output_stop();
        }
    }

    // Only update string if it changed while running
    if (new_state->output_enabled &&
        old_state->current_string != new_state->current_string) {
        led_switch_string(old_state->current_string, new_state->current_string);
    }
}

// Helper functions for detecting specific changes
static bool ui_state_changed(const AppState* old, const AppState* new) {
    return old->menu_state != new->menu_state ||
           old->menu_selection != new->menu_selection ||
           old->in_detail_view != new->in_detail_view ||
           old->fps != new->fps;
}

static bool output_state_changed(const AppState* old, const AppState* new) {
    return old->output_enabled != new->output_enabled;
}
```

### 5. ViewManager (Display Rendering)

Views are pure functions that render based on state. They have no side effects beyond drawing to the display.

```c
void view_render(const AppState* state, sh1106_t* display) {
    switch (state->menu_state) {
        case MENU_MAIN:
            view_render_main_menu(state, display);
            break;
        case MENU_RAINBOW_TEST:
            if (state->in_detail_view) {
                view_render_rainbow_detail(state, display);
            } else {
                view_render_main_menu(state, display);
            }
            break;
        // ... other views
    }
}
```

## Main Loop / Dispatcher

The main loop polls for events, creates actions, and dispatches them:

```c
static AppState current_state = {0};

void dispatch(Action action) {
    AppState old_state = current_state;

    // Add timestamp
    action.timestamp = time_us_32();

    // Pure reduction
    current_state = reduce(&current_state, &action);

    // Apply side effects only if state changed
    if (app_state_dirty(&old_state, &current_state)) {
        side_effects_apply(&old_state, &current_state);
    }
}

int main() {
    // Initialize hardware
    hardware_init();
    current_state = app_state_initial();

    while (true) {
        // Poll for button presses (with debouncing)
        if (button_select_pressed()) {
            dispatch(action_button_select());
        }
        if (button_next_pressed()) {
            dispatch(action_button_next());
        }

        // Periodic tick for animations
        if (tick_ready()) {
            dispatch(action_tick());
        }

        tight_loop_contents();
    }
}
```

## Benefits

### Testability
The reducer is a pure function with no hardware dependencies. It can be unit tested in isolation:

```c
void test_select_advances_string() {
    AppState state = {
        .menu_state = MENU_RAINBOW_TEST,
        .in_detail_view = true,
        .current_string = 5,
    };
    Action action = action_button_select();

    AppState new_state = reduce(&state, &action);

    assert(new_state.current_string == 6);
    assert(new_state.version == state.version + 1);
}
```

### Predictability
All state changes flow through a single reducer. There are no hidden mutations or race conditions. The state at any point is deterministic based on the sequence of actions.

### Debuggability
State history can be logged or captured for replay:

```c
void dispatch(Action action) {
    printf("Action: %d, State version: %d -> ", action.type, current_state.version);
    current_state = reduce(&current_state, &action);
    printf("%d\n", current_state.version);
    // ...
}
```

### Separation of Concerns

| Component | Responsibility | Can Access Hardware? |
|-----------|---------------|---------------------|
| Reducer | State transitions | No |
| SideEffects | Hardware I/O | Yes |
| Views | Display rendering | Display only |
| Main | Event dispatch | Polling only |

### Efficiency
Version-based dirty checking is O(1) regardless of state size. No need for deep comparison.

## File Organization

```
pixel_blit_firmware/
├── app_state.h          # State struct and helpers
├── action.h             # Action types and factories
├── reducer.c/h          # Pure state reducer
├── side_effects.c/h     # Hardware I/O
├── views.c/h            # Display rendering
├── main.c               # Event loop and dispatch
└── docs/
    └── reactive_architecture.md  # This document
```

## Comparison to Other Patterns

This architecture is similar to:
- **Redux** (JavaScript) - Same action/reducer/store pattern
- **Elm Architecture** - Model/Update/View with messages
- **MVI** (Model-View-Intent) - Unidirectional data flow

It differs from:
- **MVC** - No bidirectional bindings
- **Observer pattern** - No explicit subscriptions; state-driven updates
- **Signal-based reactivity** (RxJS, Combine) - No streams; discrete state transitions

## Migration Strategy

When refactoring existing code to this pattern:

1. **Extract state**: Identify all mutable state and consolidate into AppState
2. **Identify events**: List all inputs (buttons, timers, sensors) that cause changes
3. **Create actions**: Define action types for each event
4. **Write reducer**: Move state transition logic to pure functions
5. **Isolate side effects**: Move all hardware I/O to SideEffects module
6. **Wire up dispatch**: Connect event detection to action dispatch

## Reference Implementation

See the AWG (Arbitrary Waveform Generator) project at `../awg` for a complete implementation of this pattern in an embedded C++ context.
