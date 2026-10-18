# Hardened build: the host must not be able to talk to the keyboard beyond
# plain HID input reports, and must not be able to make it enter the bootloader.

# No vendor/raw HID channel at all (VIA, Keychron Launcher, XAP, console, ...)
VIA_ENABLE            = no
RAW_ENABLE            = no
XAP_ENABLE            = no
CONSOLE_ENABLE        = no
VIRTSER_ENABLE        = no
MIDI_ENABLE           = no
# Keymap lives in flash only; the host can't rewrite keys or macros
DYNAMIC_KEYMAP_ENABLE = no
DYNAMIC_MACRO_ENABLE  = no
# No magic key combos (Command feature can jump to the bootloader)
COMMAND_ENABLE        = no
# Physical "hold Esc while plugging in" is the only way into DFU
BOOTMAGIC_ENABLE      = yes

SRC += hardened.c
