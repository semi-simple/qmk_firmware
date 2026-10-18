#pragma once

// Report every key pressed at once (N-key rollover) from the start, rather than
// the standard 6 keys. Fn+N still toggles it. BIOS screens and boot menus keep
// the standard format, since QMK switches back to it when they ask.
#define NKRO_DEFAULT_ON true
