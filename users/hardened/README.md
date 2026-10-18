# Hardened firmware for the Keychron Q3 (wired, STM32L432)

The keyboard can only be put into update (DFU) mode by physically holding **Esc**
while plugging in the cable. Nothing running on the computer can trigger an update.

## What changes compared to stock

- `hardened.c` replaces `bootloader_jump()`, the single function every route into
  the bootloader goes through. It refuses unless the keyboard is still starting up
  **and** Esc (matrix position K00) is held down. Anything else just reboots.
- Two modes, chosen when you plug the keyboard in (switch this feature off with
  `HARDENED_VIA_MODE = no` in `users/hardened/rules.mk`, or
  `make keychron/q3/ansi:hardened HARDENED_VIA_MODE=no`; VIA is then not built at all
  and the keyboard has no raw HID interface):
  - **Hardened (default):** every VIA command from the computer is dropped without a
    reply, and the keys come from the keymap built into the firmware. Anything changed
    through VIA is ignored. VIA's raw HID interface is still visible over USB.
  - **VIA (hold F1 while plugging in):** VIA and Keychron Launcher work, so keys and
    macros can be changed from the computer, and those changes apply in this mode only.
    Any program on the computer can use this channel while the mode is on. Even so,
    it can't reflash the keyboard (the bootloader guard applies in both modes) or
    read which keys you press (`VIA_INSECURE` is refused).
- Keychron's own raw HID commands are gone. The stock firmware accepts a packet
  (`0xAB 0x03 "JumpToBootloader"` + checksum) that enters DFU with no key press.
- No on-keyboard macro recording, XAP, console or Command. The build stops with an
  error if any of these, or `VIA_INSECURE`, is turned on.
- On every plug-in, a rainbow comet runs once from left to right (about 1.6 s):
  across the top row in hardened mode, across the whole keyboard in VIA mode. It
  tells you this firmware, not the stock one, is running, and in which mode. It is
  not proof against tampering, since anyone able to reflash the keyboard could copy
  it; the backup check below is the real proof.

In hardened mode, remapping means editing
`keyboards/keychron/q3/<variant>/keymaps/hardened/keymap.c`, rebuilding and reflashing.

## Build

```sh
git submodule update --init --depth 1 lib/chibios lib/chibios-contrib lib/printf lib/pico-sdk
make keychron/q3/ansi:hardened          # or ansi_encoder, iso, iso_encoder, jis, jis_encoder
sha256sum .build/keychron_q3_*_hardened.bin
```

Every push to this branch is built by `.github/workflows/hardened-q3.yml` with
`arm-none-eabi-gcc` 13.2.rel1 (Ubuntu 24.04's `gcc-arm-none-eabi 15:13.2.rel1-2`). Each run
and each release lists the SHA-256 of its binaries in `SHA256SUMS`. The build is
reproducible: the same commit built with that compiler gives byte-identical files.
Releases are made from **Actions → Hardened Keychron Q3 → Run workflow** with a name
such as `hardened-q3-v2`.

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

It must match the line for your variant in the `SHA256SUMS` of the release (or workflow
run) you flashed.

## Recovery

The STM32 ROM bootloader can't be overwritten, so a bad flash is recoverable with the
PCB reset button. Last resort: SWD with an ST-Link on the PCB test pads.
