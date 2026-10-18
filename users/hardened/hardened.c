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

void keyboard_post_init_user(void) {
    bootloader_window_closed = true;
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
