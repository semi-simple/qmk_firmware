// Hardened firmware: the bootloader can only be entered by physically holding
// the bootmagic key (K00 / Esc) while plugging the keyboard in.
#include QMK_KEYBOARD_H
#include "bootloader.h"

#if defined(RAW_ENABLE) || defined(VIA_ENABLE) || defined(XAP_ENABLE) || defined(CONSOLE_ENABLE) || defined(VIRTSER_ENABLE) || defined(MIDI_ENABLE)
#    error "hardened: host-to-keyboard data channels must be disabled"
#endif
#if defined(DYNAMIC_KEYMAP_ENABLE) || defined(DYNAMIC_MACRO_ENABLE) || defined(COMMAND_ENABLE)
#    error "hardened: dynamic keymaps, dynamic macros and Command must be disabled"
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

// Provided (weak) by platforms/chibios/bootloaders/stm32_dfu.c
void bootloader_marker_enable(void);

// Set once boot has finished; from then on the bootloader is unreachable.
static bool bootloader_window_closed = false;

#ifdef RGB_MATRIX_ENABLE
static void boot_sweep_start(void);
#endif

void keyboard_post_init_user(void) {
    bootloader_window_closed = true;
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

#ifdef RGB_MATRIX_ENABLE
// Boot signature: right after plug-in a rainbow comet runs once across the top
// row, so you can see this firmware (not the stock one) is running. It identifies
// your build; it doesn't prove nobody copied it (see README.md).
#    define SWEEP_STEP_MS 45   // head moves one key every 45 ms
#    define SWEEP_TRAIL_MS 180 // each key fades out over 180 ms

static uint8_t  sweep_leds[MATRIX_COLS]; // top-row LEDs, left to right
static uint8_t  sweep_count;
static uint32_t sweep_timer;
static bool     sweep_running   = false;
static bool     sweep_lights_on = false; // lighting was off; turned on just for the sweep

static void boot_sweep_start(void) {
    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        uint8_t led = g_led_config.matrix_co[0][col];
        if (led != NO_LED) sweep_leds[sweep_count++] = led;
    }
    sweep_timer   = timer_read32();
    sweep_running = sweep_count > 0;
    if (sweep_running && !rgb_matrix_is_enabled()) {
        sweep_lights_on = true;
        rgb_matrix_enable_noeeprom();
    }
}

void housekeeping_task_user(void) {
    if (sweep_running && timer_elapsed32(sweep_timer) >= (uint32_t)sweep_count * SWEEP_STEP_MS + SWEEP_TRAIL_MS) {
        sweep_running = false;
        if (sweep_lights_on) rgb_matrix_disable_noeeprom(); // back to the user's setting
    }
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (!sweep_running) return true;
    for (uint8_t i = led_min; i < led_max; i++) rgb_matrix_set_color(i, 0, 0, 0);
    uint32_t elapsed = timer_elapsed32(sweep_timer);
    for (uint8_t k = 0; k < sweep_count; k++) {
        uint32_t hit = (uint32_t)k * SWEEP_STEP_MS;
        uint8_t  led = sweep_leds[k];
        if (elapsed < hit || elapsed - hit >= SWEEP_TRAIL_MS || led < led_min || led >= led_max) continue;
        hsv_t hsv = {
            .h = k * 255 / sweep_count, // rainbow from left to right
            .s = 255,
            .v = RGB_MATRIX_MAXIMUM_BRIGHTNESS * (SWEEP_TRAIL_MS - (elapsed - hit)) / SWEEP_TRAIL_MS,
        };
        rgb_t rgb = hsv_to_rgb(hsv);
        rgb_matrix_set_color(led, rgb.r, rgb.g, rgb.b);
    }
    return false; // hide other indicators (e.g. Caps Lock) during the sweep
}
#endif
