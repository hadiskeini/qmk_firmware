#include QMK_KEYBOARD_H
#include "keymap_us_international.h"

// Layer definitions
enum layers {
    _BASE = 0,
    _NAVNUM,
    _SYM,
    _FUNC,
    _GAME,
    _NUM
};

// Custom keycodes
enum custom_keycodes {
    HYPER_Q = SAFE_RANGE,
    SHIFT_CYCLE
};

// Unified key state structure
typedef struct {
    uint16_t timer;
    bool key_held;
    bool modifier_active;
    bool other_key_pressed;  // Track if another key was pressed during hold
    bool use_hold_on_other_key_press;  // Different behavior per key
} key_state_t;

// Key buffering for proper ordering
typedef struct {
    uint16_t keycode;
    bool pressed;
    bool valid;
} buffered_key_t;

static buffered_key_t key_buffer = {0, false, false};

// Global state
static struct {
    uint8_t current_layer;
    uint16_t timer;
    bool timeout_active;
    bool notification_pending;
    uint16_t notification_timer;
} cycle_state = {_BASE, 0, false, false, 0};

static key_state_t hyper_q_state = {0, false, false, false, false};  // No HOLD_ON_OTHER_KEY_PRESS
static key_state_t shift_cycle_state = {0, false, false, false, true};  // Use HOLD_ON_OTHER_KEY_PRESS

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE] = LAYOUT(
        KC_B,         KC_L,          KC_D,        KC_W,        KC_Z,        KC_QUOT,     KC_F,        KC_O,        KC_U,        KC_J,
        KC_N,         KC_R,          KC_T,        KC_S,        KC_G,        KC_Y,        KC_H,        KC_A,        KC_E,        KC_I,
        HYPER_Q,      KC_X,          KC_M,        KC_C,        KC_V,        KC_K,        KC_P,        KC_COMM,     KC_DOT,      KC_SLSH,
        LALT_T(KC_ESC), LCTL_T(KC_BSPC), SHIFT_CYCLE, LGUI_T(KC_ENT), LT(_NAVNUM,KC_SPC), LT(_SYM,KC_TAB)
    ),

    [_NAVNUM] = LAYOUT(
        KC_NUM,      KC_HOME,       KC_UP,       KC_END,      KC_PGUP,     KC_PSLS,     KC_7,        KC_8,        KC_9,        KC_EQL,
        KC_NO,       KC_LEFT,       KC_DOWN,     KC_RGHT,     KC_PGDN,     KC_DOT,      KC_4,        KC_5,        KC_6,        KC_0,
        KC_LSFT,     KC_WBAK,       KC_WREF,     KC_WFWD,     KC_INS,      KC_ASTR,     KC_1,        KC_2,        KC_3,        KC_MINS,
        _______,     LALT(KC_BSPC), SHIFT_CYCLE, _______,     _______,     _______
    ),

    [_SYM] = LAYOUT(
        US_YEN,      KC_PERC,       KC_LBRC,     KC_RBRC,     US_EURO,     KC_NO,       KC_EQL,      KC_CIRC,     KC_QUOT,     KC_GRV,
        KC_AT,       KC_HASH,       KC_LPRN,     KC_RPRN,     US_SUP2,     US_DEG,      KC_MINS,     KC_PPLS,     KC_ASTR,     KC_BSLS,
        US_PND,      KC_AMPR,       KC_LCBR,     KC_RCBR,     KC_DLR,      KC_NO,       US_UNDS,     KC_SCLN,     US_COLN,     KC_EXLM,
        _______,     _______,       _______,     _______,     _______,     _______
    ),

    [_FUNC] = LAYOUT(
        CG_SWAP,     KC_MUTE,       KC_VOLD,     KC_VOLU,     KC_NO,       KC_F12,      KC_F7,       KC_F8,       KC_F9,       KC_PSCR,
        KC_MSTP,     KC_MPRV,       KC_MPLY,     KC_MNXT,     OSM(MOD_RALT), KC_F11,    KC_F4,       KC_F5,       KC_F6,       KC_SCRL,
        DF(_BASE),   DF(_GAME),     KC_NO,       KC_NO,       KC_NO,       KC_F10,      KC_F1,       KC_F2,       KC_F3,       KC_PAUS,
        _______,     _______,       _______,     _______,     _______,     _______
    ),

    [_GAME] = LAYOUT(
        KC_Q,        KC_W,          KC_F,        KC_P,        KC_B,        KC_J,        KC_L,        KC_U,        KC_Y,        DF(_BASE),
        KC_A,        KC_R,          KC_S,        KC_T,        KC_G,        KC_M,        KC_N,        KC_E,        KC_I,        KC_O,
        KC_Z,        KC_X,          KC_C,        KC_D,        KC_V,        KC_K,        KC_H,        KC_ESC,      KC_DOT,      KC_SLSH,
        KC_LALT,     KC_BSPC,       KC_DEL,      KC_ENT,      KC_SPC,      KC_TAB
    ),

    [_NUM] = LAYOUT(
        KC_NO,       KC_7,          KC_8,        KC_9,        KC_NO,       KC_NO,       KC_NO,       KC_NO,       KC_NO,       KC_NO,
        KC_0,        KC_4,          KC_5,        KC_6,        KC_DOT,      KC_NO,       KC_NO,       KC_NO,       KC_NO,       KC_NO,
        KC_NO,       KC_1,          KC_2,        KC_3,        KC_NO,       KC_NO,       KC_NO,       KC_NO,       KC_NO,       KC_NO,
        KC_NO,       KC_NO,         SHIFT_CYCLE, KC_NO,       KC_NO,       KC_NO
    )
};

// Constants
#define HYPER_MODS (MOD_LCTL | MOD_LSFT | MOD_LALT | MOD_LGUI)
static const uint8_t LAYER_SEQUENCE[] = {_BASE, _NUM, _NAVNUM};
#define SEQUENCE_LENGTH (sizeof(LAYER_SEQUENCE) / sizeof(LAYER_SEQUENCE[0]))

// Helper functions
static inline void trigger_layer_notification(uint8_t layer) {
    cycle_state.notification_pending = true;
    cycle_state.notification_timer = timer_read();
    register_mods(HYPER_MODS);
    register_code(KC_F1 + layer);
}

static inline void complete_notification_sequence(void) {
    unregister_code(KC_F1 + cycle_state.current_layer);
    unregister_mods(HYPER_MODS);
    cycle_state.notification_pending = false;
}

static uint8_t find_layer_index(uint8_t layer) {
    for (uint8_t i = 0; i < SEQUENCE_LENGTH; i++) {
        if (LAYER_SEQUENCE[i] == layer) return i;
    }
    return 0; // Default to first layer if not found
}

static void cycle_to_next_layer(void) {
    uint8_t current_index = find_layer_index(cycle_state.current_layer);
    uint8_t next_index = (current_index + 1) % SEQUENCE_LENGTH;

    cycle_state.current_layer = LAYER_SEQUENCE[next_index];
    layer_move(cycle_state.current_layer);
    trigger_layer_notification(cycle_state.current_layer);

    cycle_state.timeout_active = (cycle_state.current_layer != _BASE);
    if (cycle_state.timeout_active) {
        cycle_state.timer = timer_read();
    }
}

static inline void reset_to_base_layer(void) {
    cycle_state.current_layer = _BASE;
    cycle_state.timeout_active = false;
    layer_move(_BASE);
    trigger_layer_notification(_BASE);
}

// Enhanced key state handler with permissive hold
static void handle_key_press(key_state_t *state) {
    state->timer = timer_read();
    state->key_held = true;
    state->other_key_pressed = false;  // Reset on new press
}

static bool handle_key_release(key_state_t *state, uint8_t mods, void (*tap_action)(void)) {
    state->key_held = false;

    if (state->modifier_active) {
        // Modifier was active, just clean up
        unregister_mods(mods);
        state->modifier_active = false;
    } else {
        // Determine if this should be treated as tap or hold
        bool should_hold = false;

        // Permissive hold logic: if another key was pressed during the hold,
        // and we're past the tapping term, treat as hold
        if (state->other_key_pressed && timer_elapsed(state->timer) >= TAPPING_TERM) {
            should_hold = true;
        }
        // Regular hold timeout (held long enough without other keys)
        else if (timer_elapsed(state->timer) >= TAPPING_TERM) {
            should_hold = true;
        }

        if (should_hold) {
            // This was a hold - don't send the tap action
            // The modifier effect should already have been applied by matrix_scan
        } else if (tap_action) {
            // This was a quick tap - send the tap action
            tap_action();

            // If we have a buffered key, send it now to maintain proper order
            if (key_buffer.valid) {
                if (key_buffer.pressed) {
                    register_code(key_buffer.keycode);
                } else {
                    unregister_code(key_buffer.keycode);
                }
                key_buffer.valid = false;
            }
        }
    }

    // Reset state
    state->other_key_pressed = false;
    return false;
}

static void handle_other_key_press(key_state_t *state, uint8_t mods) {
    if (state->key_held) {
        state->other_key_pressed = true;  // Mark that another key was pressed

        // Apply HOLD_ON_OTHER_KEY_PRESS behavior only for keys that want it
        if (state->use_hold_on_other_key_press && !state->modifier_active) {
            register_mods(mods);
            state->modifier_active = true;
        }
    }
}

static void check_hold_timeout(key_state_t *state, uint8_t mods) {
    if (state->key_held && !state->modifier_active &&
        timer_elapsed(state->timer) >= TAPPING_TERM) {
        // Activate modifier when timeout is reached
        register_mods(mods);
        state->modifier_active = true;
    }
}

// Tap action callbacks
static void tap_q(void) { tap_code(KC_Q); }

// QMK callbacks
layer_state_t layer_state_set_user(layer_state_t state) {
    return update_tri_layer_state(state, _NAVNUM, _SYM, _FUNC);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case HYPER_Q:
            if (record->event.pressed) {
                handle_key_press(&hyper_q_state);
            } else {
                return handle_key_release(&hyper_q_state, HYPER_MODS, tap_q);
            }
            return false;

        case SHIFT_CYCLE:
            if (record->event.pressed) {
                handle_key_press(&shift_cycle_state);
            } else {
                return handle_key_release(&shift_cycle_state, MOD_LSFT, cycle_to_next_layer);
            }
            return false;

        default:
            if (record->event.pressed) {
                // Check if we need to buffer this key press
                bool should_buffer = false;

                // Buffer if HYPER_Q is held and not using HOLD_ON_OTHER_KEY_PRESS
                if (hyper_q_state.key_held && !hyper_q_state.use_hold_on_other_key_press &&
                    !hyper_q_state.modifier_active) {
                    should_buffer = true;
                }

                if (should_buffer) {
                    // Buffer this key press
                    key_buffer.keycode = keycode;
                    key_buffer.pressed = true;
                    key_buffer.valid = true;
                }

                // Handle modifier activation
                handle_other_key_press(&hyper_q_state, HYPER_MODS);
                handle_other_key_press(&shift_cycle_state, MOD_LSFT);

                // Reset timeout timer on any keypress
                if (cycle_state.timeout_active) {
                    cycle_state.timer = timer_read();
                }

                // Return false if we buffered the key to prevent immediate processing
                return !should_buffer;
            } else {
                // Key release - check if this was a buffered key
                if (key_buffer.valid && key_buffer.keycode == keycode) {
                    key_buffer.pressed = false;
                    // Don't process the release yet - it will be handled when the tap-hold key is released
                    return false;
                }
            }
            break;
    }
    return true;
}

void matrix_scan_user(void) {
    // Handle notification sequence timing
    if (cycle_state.notification_pending &&
        timer_elapsed(cycle_state.notification_timer) >= LAYER_NOTIFICATION_DELAY) {
        complete_notification_sequence();
    }

    // Check hold timeouts
    check_hold_timeout(&hyper_q_state, HYPER_MODS);
    check_hold_timeout(&shift_cycle_state, MOD_LSFT);

    // Check for timeout to return to base layer
    if (cycle_state.timeout_active &&
        timer_elapsed(cycle_state.timer) > LAYER_TIMEOUT_DURATION) {
        reset_to_base_layer();
    }
}
