// Hardened firmware: the bootloader can only be entered by physically holding
// the bootmagic key (K00 / Esc) while plugging the keyboard in.
//
// With HARDENED_VIA_MODE (on by default, see rules.mk) the mode is chosen at plug-in:
// - hardened (default): VIA commands are dropped and the keymap comes from flash.
// - VIA (hold F1 while plugging in): VIA works, so keys and macros can be changed
//   from the computer. The bootloader guard applies in both modes.
// Without it, VIA isn't built at all and the keymap always comes from flash.
#include QMK_KEYBOARD_H
#include "bootloader.h"
#include "keymap_introspection.h"

#if defined(XAP_ENABLE) || defined(CONSOLE_ENABLE) || defined(VIRTSER_ENABLE) || defined(MIDI_ENABLE)
#    error "hardened: host-to-keyboard data channels other than VIA must be disabled"
#endif
#if defined(DYNAMIC_MACRO_ENABLE) || defined(COMMAND_ENABLE)
#    error "hardened: dynamic macros and Command must be disabled"
#endif
#ifdef HARDENED_VIA_MODE
#    if !defined(VIA_ENABLE) || !defined(RAW_ENABLE) || !defined(DYNAMIC_KEYMAP_ENABLE)
#        error "hardened: HARDENED_VIA_MODE needs VIA, its runtime gate is in this file"
#    endif
#    ifdef VIA_INSECURE
#        error "hardened: VIA_INSECURE lets the computer read which keys are pressed"
#    endif
#elif defined(VIA_ENABLE) || defined(RAW_ENABLE) || defined(DYNAMIC_KEYMAP_ENABLE)
#    error "hardened: VIA, raw HID and dynamic keymaps must be off without HARDENED_VIA_MODE"
#endif
#ifndef BOOTMAGIC_ENABLE
#    error "hardened: bootmagic is required, it is the only way to reflash"
#endif
#if defined(STM32_BOOTLOADER_DUAL_BANK) && STM32_BOOTLOADER_DUAL_BANK
#    error "hardened: unsupported bootloader type"
#endif

#ifndef BOOTMAGIC_ROW
#    define BOOTMAGIC_ROW 0
#endif
#ifndef BOOTMAGIC_COLUMN
#    define BOOTMAGIC_COLUMN 0
#endif
#ifdef HARDENED_VIA_MODE
// Key held at plug-in to enable VIA: F1 (K01)
#    define VIA_MODE_ROW 0
#    define VIA_MODE_COLUMN 1
#endif

// Provided (weak) by platforms/chibios/bootloaders/stm32_dfu.c
void bootloader_marker_enable(void);

// Set once boot has finished; from then on the bootloader is unreachable.
static bool bootloader_window_closed = false;
static bool via_mode                 = false; // stays false without HARDENED_VIA_MODE
#ifdef HARDENED_VIA_MODE
static bool via_key_held_from_boot = false;
#endif

#ifdef RGB_MATRIX_ENABLE
static void boot_sweep_start(void);
#endif

void keyboard_post_init_user(void) {
    bootloader_window_closed = true;
#ifdef HARDENED_VIA_MODE
    via_mode               = matrix_get_row(VIA_MODE_ROW) & ((matrix_row_t)1 << VIA_MODE_COLUMN);
    via_key_held_from_boot = via_mode;
#endif
#ifdef RGB_MATRIX_ENABLE
    boot_sweep_start();
#endif
}

// Overrides the weak stm32_dfu implementation. Every route into the bootloader
// (bootmagic, QK_BOOT, anything added to QMK later) goes through here, so this
// is the single gate: allowed only during power-up AND with the key held down.
void bootloader_jump(void) {
    if (!bootloader_window_closed && (matrix_get_row(BOOTMAGIC_ROW) & ((matrix_row_t)1 << BOOTMAGIC_COLUMN))) {
        bootloader_marker_enable();
    }
    // Refused requests just reboot back into this firmware.
    NVIC_SystemReset();
}

#ifdef HARDENED_VIA_MODE
// Runs before every VIA command; returning true means "handled". In hardened
// mode every command is dropped without a reply.
bool via_command_kb(uint8_t *data, uint8_t length) {
    return !via_mode;
}

// Hardened mode ignores anything stored through VIA and uses the keymap in flash.
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) {
    if (key.row < MATRIX_ROWS && key.col < MATRIX_COLS) {
        return via_mode ? keycode_at_keymap_location(layer, key.row, key.col) : keycode_at_keymap_location_raw(layer, key.row, key.col);
    }
#    ifdef ENCODER_MAP_ENABLE
    if ((key.row == KEYLOC_ENCODER_CW || key.row == KEYLOC_ENCODER_CCW) && key.col < NUM_ENCODERS) {
        bool cw = key.row == KEYLOC_ENCODER_CW;
        return via_mode ? keycode_at_encodermap_location(layer, key.col, cw) : keycode_at_encodermap_location_raw(layer, key.col, cw);
    }
#    endif
    return KC_NO;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Don't type the F1 that was held to choose VIA mode
    if (via_key_held_from_boot && record->event.key.row == VIA_MODE_ROW && record->event.key.col == VIA_MODE_COLUMN) {
        if (!record->event.pressed) via_key_held_from_boot = false;
        return false;
    }
    return true;
}
#endif // HARDENED_VIA_MODE

#ifdef RGB_MATRIX_ENABLE
// Boot signature, so you can see this firmware (not the stock one) is running: a
// rainbow comet sweeps left to right once after plug-in, across the top row in
// hardened mode and across the whole keyboard in VIA mode. It identifies your
// build; it doesn't prove nobody copied it (see README.md).
#    define SWEEP_SPAN_MS 1260 // time for the head to cross the keyboard
#    define SWEEP_TRAIL_MS 360 // each key fades out over this time

static bool     sweep_led[RGB_MATRIX_LED_COUNT]; // LEDs taking part in the sweep
static uint32_t sweep_timer;
static bool     sweep_running   = false;
static bool     sweep_lights_on = false; // lighting was off; turned on just for the sweep

static void boot_sweep_start(void) {
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) sweep_led[i] = via_mode;
    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        uint8_t led = g_led_config.matrix_co[0][col];
        if (led != NO_LED) sweep_led[led] = true;
    }
    sweep_timer   = timer_read32();
    sweep_running = true;
    if (!rgb_matrix_is_enabled()) {
        sweep_lights_on = true;
        rgb_matrix_enable_noeeprom();
    }
}

void housekeeping_task_user(void) {
    if (sweep_running && timer_elapsed32(sweep_timer) >= SWEEP_SPAN_MS + SWEEP_TRAIL_MS) {
        sweep_running = false;
        if (sweep_lights_on) rgb_matrix_disable_noeeprom(); // back to the user's setting
    }
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (!sweep_running) return true;
    uint32_t elapsed = timer_elapsed32(sweep_timer);
    for (uint8_t i = led_min; i < led_max; i++) {
        uint32_t hit = (uint32_t)g_led_config.point[i].x * SWEEP_SPAN_MS / 224; // when the head reaches this key
        if (!sweep_led[i] || elapsed < hit || elapsed - hit >= SWEEP_TRAIL_MS) {
            rgb_matrix_set_color(i, 0, 0, 0);
            continue;
        }
        uint32_t age = elapsed - hit;
        hsv_t    hsv = {
            .h = g_led_config.point[i].x * 238 / 224, // rainbow from left to right
            // White-hot head turning to full colour, and a slow-then-fast fade
            .s = 255 - 96 * (SWEEP_TRAIL_MS - age) / SWEEP_TRAIL_MS,
            .v = RGB_MATRIX_MAXIMUM_BRIGHTNESS * (SWEEP_TRAIL_MS * SWEEP_TRAIL_MS - age * age) / (SWEEP_TRAIL_MS * SWEEP_TRAIL_MS),
        };
        rgb_t rgb = hsv_to_rgb_nocie(hsv); // linear brightness: brighter than the CIE curve while fading
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return false; // hide other indicators (e.g. Caps Lock) during the sweep
}
#endif
