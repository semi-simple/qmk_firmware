# Hardened build: the computer can never make the keyboard enter the bootloader.

# Hold F1 while plugging in to enable VIA for that session (see hardened.c).
# Set to no, here or with `make ... HARDENED_VIA_MODE=no`, to leave VIA out
# entirely: no raw HID interface, keymap in flash only.
HARDENED_VIA_MODE ?= yes

ifeq ($(strip $(HARDENED_VIA_MODE)), yes)
    VIA_ENABLE            = yes
    OPT_DEFS             += -DHARDENED_VIA_MODE
else
    VIA_ENABLE            = no
    RAW_ENABLE            = no
    DYNAMIC_KEYMAP_ENABLE = no
endif

# No other host-to-keyboard channels
XAP_ENABLE            = no
CONSOLE_ENABLE        = no
VIRTSER_ENABLE        = no
MIDI_ENABLE           = no
# No on-keyboard macro recording; no magic key combos (Command can jump to the bootloader)
DYNAMIC_MACRO_ENABLE  = no
COMMAND_ENABLE        = no
# Physical "hold Esc while plugging in" is the only way into DFU
BOOTMAGIC_ENABLE      = yes

SRC += hardened.c
