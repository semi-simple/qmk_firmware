#pragma once

// Report every key pressed at once (N-key rollover) from the start, rather than
// the standard 6 keys. Fn+N still toggles it. BIOS screens and boot menus keep
// the standard format, since QMK switches back to it when they ask.
#define NKRO_DEFAULT_ON true

// LED driver current for each group of channels, as in the stock Keychron
// firmware (read from a backup of it). QMK's Q3 config uses lower values
// (0x9D, 0x9D, 0x44), which makes all lighting dimmer than stock.
#undef SNLED27351_CURRENT_TUNE
#define SNLED27351_CURRENT_TUNE {0xC8, 0xC8, 0x68, 0xC8, 0xC8, 0x68, 0xC8, 0xC8, 0x68, 0xC8, 0xC8, 0x68}
