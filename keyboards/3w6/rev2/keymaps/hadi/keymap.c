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
    ALL_T_CYCLE = SAFE_RANGE
};

// Configuration constants
#define HOLD_THRESHOLD 200   // ms to distinguish tap from hold
#define TIMEOUT_DURATION 5000 // ms before returning to base layer
#define NOTIFICATION_DELAY 50 // ms delay for notification sequence

// State variables for layer cycling
static struct {
    uint8_t current_layer;
    uint16_t timer;
    bool timeout_active;
    bool hyper_active;
    bool key_held;
    bool notification_pending;
    uint16_t notification_timer;
    uint8_t notification_step;
} cycle_state = {_BASE, 0, false, false, false, false, 0, 0};

// Layer names are handled by OS notification scripts

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE] = LAYOUT(
        KC_B,         KC_L,          KC_D,        KC_W,        KC_Z,        KC_QUOT,     KC_F,        KC_O,        KC_U,        KC_J,
        KC_N,         KC_R,          KC_T,        KC_S,        KC_G,        KC_Y,        KC_H,        KC_A,        KC_E,        KC_I,
        LSFT_T(KC_Q), KC_X,          KC_M,        KC_C,        KC_V,        KC_K,        KC_P,        KC_COMM,     KC_DOT,      RSFT_T(KC_SLSH),
        LALT_T(KC_ESC), LCTL_T(KC_BSPC), ALL_T_CYCLE, LGUI_T(KC_ENT), LT(_NAVNUM,KC_SPC), LT(_SYM,KC_TAB)
    ),
    
    [_NAVNUM] = LAYOUT(
        KC_NUM,      KC_HOME,       KC_UP,       KC_END,      KC_PGUP,     KC_PSLS,     KC_7,        KC_8,        KC_9,        KC_EQL,
        KC_NO,       KC_LEFT,       KC_DOWN,     KC_RGHT,     KC_PGDN,     KC_DOT,      KC_4,        KC_5,        KC_6,        KC_0,
        KC_LSFT,     KC_WBAK,       KC_WREF,     KC_WFWD,     KC_INS,      KC_ASTR,     KC_1,        KC_2,        KC_3,        KC_MINS,
        _______,     LALT(KC_BSPC), ALL_T_CYCLE, _______,     _______,     _______
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
        KC_NO,       KC_NO,         ALL_T_CYCLE, KC_NO,       KC_NO,       KC_NO
    )
};

// Notification functions (using Hyper: Ctrl+Shift+Super+Alt)
static void trigger_layer_notification(uint8_t layer) {
    // Start the notification sequence
    cycle_state.notification_pending = true;
    cycle_state.notification_timer = timer_read();
    cycle_state.notification_step = 0;

    // Send a brief key sequence that can be caught by notification scripts
    // We'll use Hyper+F1 + layer number as our notification trigger
    register_mods(MOD_LCTL | MOD_LSFT | MOD_LALT | MOD_LGUI);
    register_code(KC_F1 + layer); // F1 for BASE, F2 for NAVNUM, etc.
}

static void complete_notification_sequence(void) {
    // Complete the notification key sequence
    unregister_code(KC_F1 + cycle_state.current_layer);
    unregister_mods(MOD_LCTL | MOD_LSFT | MOD_LALT | MOD_LGUI);
    cycle_state.notification_pending = false;
}

// Helper functions
static void cycle_to_next_layer(void) {
    const uint8_t layer_sequence[] = {_BASE, _NUM, _NAVNUM};
    const uint8_t sequence_length = sizeof(layer_sequence) / sizeof(layer_sequence[0]);
    
    // Find current position in sequence
    uint8_t current_index = 0;
    for (uint8_t i = 0; i < sequence_length; i++) {
        if (layer_sequence[i] == cycle_state.current_layer) {
            current_index = i;
            break;
        }
    }
    
    // Move to next layer in sequence
    uint8_t next_index = (current_index + 1) % sequence_length;
    cycle_state.current_layer = layer_sequence[next_index];
    layer_move(cycle_state.current_layer);
    
    // Trigger notification for the new layer
    trigger_layer_notification(cycle_state.current_layer);
    
    // Update timeout state
    cycle_state.timeout_active = (cycle_state.current_layer != _BASE);
    cycle_state.timer = timer_read();
}

static void activate_hyper_key(void) {
    const uint8_t hyper_mods = MOD_LCTL | MOD_LSFT | MOD_LALT | MOD_LGUI;
    register_mods(hyper_mods);
    cycle_state.hyper_active = true;
}

static void deactivate_hyper_key(void) {
    const uint8_t hyper_mods = MOD_LCTL | MOD_LSFT | MOD_LALT | MOD_LGUI;
    unregister_mods(hyper_mods);
    cycle_state.hyper_active = false;
}

static void reset_to_base_layer(void) {
    cycle_state.current_layer = _BASE;
    cycle_state.timeout_active = false;
    layer_move(_BASE);
    
    // Trigger notification for base layer
    trigger_layer_notification(_BASE);
}

// QMK callback functions
layer_state_t layer_state_set_user(layer_state_t state) {
    return update_tri_layer_state(state, _NAVNUM, _SYM, _FUNC);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case ALL_T_CYCLE:
            if (record->event.pressed) {
                cycle_state.timer = timer_read();
                cycle_state.key_held = true;
            } else {
                cycle_state.key_held = false;
                
                if (cycle_state.hyper_active) {
                    // Was held as hyper key - deactivate it
                    deactivate_hyper_key();
                } else {
                    // Was a short tap - cycle layers
                    cycle_to_next_layer();
                }
            }
            return false;
        
        default:
            // HOLD_ON_OTHER_KEY_PRESS behavior: activate hyper key immediately
            if (record->event.pressed && cycle_state.key_held && !cycle_state.hyper_active) {
                activate_hyper_key();
            }
            
            // Reset timeout timer on any keypress when timeout is active
            if (record->event.pressed && cycle_state.timeout_active) {
                cycle_state.timer = timer_read();
            }
            break;
    }
    return true;
}

void matrix_scan_user(void) {
    // Handle notification sequence timing
    if (cycle_state.notification_pending && 
        timer_elapsed(cycle_state.notification_timer) >= NOTIFICATION_DELAY) {
        complete_notification_sequence();
    }
    
    // Check if we should activate hyper key after hold threshold
    if (cycle_state.key_held && !cycle_state.hyper_active && 
        timer_elapsed(cycle_state.timer) >= HOLD_THRESHOLD) {
        activate_hyper_key();
    }
    
    // Check for timeout to return to base layer
    if (cycle_state.timeout_active && timer_elapsed(cycle_state.timer) > TIMEOUT_DURATION) {
        reset_to_base_layer();
    }
}
