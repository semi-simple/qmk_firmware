# Hardened firmware for the Keychron Q3 (wired, STM32L432)

The keyboard can only be put into update (DFU) mode by physically holding **Esc**
while plugging in the cable. Nothing running on the computer can trigger an update.

## What changes compared to stock

- `hardened.c` replaces `bootloader_jump()`, the single function every route into
  the bootloader goes through. It refuses unless the keyboard is still starting up
  **and** Esc (matrix position K00) is held down. Anything else just reboots.
- No raw HID channel at all: VIA, Keychron Launcher, XAP, console and the Keychron
  factory-test commands are gone. Keychron's stock firmware accepts a raw HID packet
  (`0xAB 0x03 "JumpToBootloader"` + checksum) that enters DFU with no key press.
- No dynamic keymaps or macros, so nothing on the computer can remap keys or store
  key sequences on the keyboard.
- The build stops with an error if any of these features is turned back on.

Remapping now means editing `keyboards/keychron/q3/<variant>/keymaps/hardened/keymap.c`,
rebuilding and reflashing.

## Build

Based on upstream QMK commit `180f394b489c238cb231c2bf8b080262155e03c0`.

```sh
git submodule update --init --depth 1 lib/chibios lib/chibios-contrib lib/printf lib/pico-sdk
make keychron/q3/ansi:hardened          # or ansi_encoder, iso, iso_encoder, jis, jis_encoder
sha256sum .build/keychron_q3_*_hardened.bin
```

With `arm-none-eabi-gcc` 13.2.rel1 (Ubuntu 24.04's `gcc-arm-none-eabi 15:13.2.rel1-2`)
the binaries are byte-identical to `SHA256SUMS`. The GitHub Actions workflow
`.github/workflows/hardened-q3.yml` builds them the same way and fails if they differ.

## Flash

1. Unplug, hold Esc, plug in. Keys and lights go dark: the keyboard is in DFU mode.
   Fallback: remove the spacebar keycap and hold the PCB reset button while plugging in.
2. Open `flasher.html` from disk in Chrome or Edge (it makes no network requests), or the
   copy GitHub Pages serves from this branch at https://semi-simple.github.io/qmk_firmware/, or run
   `dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D keychron_q3_ansi_hardened.bin`.

Entering DFU with Esc also resets saved settings such as RGB mode.

## Check what is on the keyboard

Take a backup with `flasher.html` (DFU mode), then compare its start with the image:

```sh
head -c "$(stat -c %s keychron_q3_ansi_hardened.bin)" q3-backup.bin | sha256sum
```

It must match the line for your variant in `SHA256SUMS`.

## Recovery

The STM32 ROM bootloader can't be overwritten, so a bad flash is recoverable with the
PCB reset button. Last resort: SWD with an ST-Link on the PCB test pads.
