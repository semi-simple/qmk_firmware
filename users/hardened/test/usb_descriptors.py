"""Checks what a computer sees over USB in hardened mode and in VIA mode.

Runs the compiled firmware's own USB request handlers (QMK's
usb_get_descriptor_cb and usb_requests_hook_cb) in an ARM emulator and sends
them the requests a computer makes while enumerating the keyboard.

usage: python usb_descriptors.py .build/keychron_q3_ansi_hardened.elf
needs: pip install unicorn==2.1.4 pyelftools==0.33, and the build's
       .build/obj_<target>/cflags.txt (to compute ChibiOS struct offsets)
"""
import os
import struct
import subprocess
import sys
import tempfile

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3

ELF = sys.argv[1]
OBJ_DIR = os.path.join(os.path.dirname(ELF), "obj_" + os.path.basename(ELF)[:-len(".elf")])


def usb_driver_offsets():
    """offsetof() for the USBDriver fields we touch, compiled with the build's own flags."""
    fields = ["config", "setup", "ep0next", "ep0n"]
    src = '#include "hal.h"\n#include <stddef.h>\n' + "".join(f"const unsigned off_{f} = offsetof(USBDriver, {f});\n" for f in fields)
    flags = [f for f in open(os.path.join(OBJ_DIR, "cflags.txt")).read().split() if not f.startswith(("-flto", "-MMD", "-MP", "-MF", "-Wa,"))]
    with tempfile.TemporaryDirectory() as tmp:
        c, o = os.path.join(tmp, "offsets.c"), os.path.join(tmp, "offsets.o")
        open(c, "w").write(src)
        subprocess.run(" ".join(["arm-none-eabi-gcc", *flags, "-c", c, "-o", o]), shell=True, check=True)
        out = {}
        for f in fields:
            data = subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", f"--only-section=.rodata.off_{f}", o, "/dev/stdout"], capture_output=True, check=True).stdout
            out[f] = struct.unpack("<I", data[:4])[0]
        return out


USBD = usb_driver_offsets()
elf = ELFFile(open(ELF, "rb"))
syms = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols() if s.name}

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for base, size in [(0x08000000, 0x40000), (0x10000000, 0x10000), (0x20000000, 0x20000), (0x40000000, 0x20000000), (0xE0000000, 0x100000)]:
    uc.mem_map(base, size)
for seg in elf.iter_segments():
    if seg["p_type"] == "PT_LOAD" and seg["p_memsz"]:
        uc.mem_write(seg["p_paddr"], seg.data())  # flash image, including .data initial values
        uc.mem_write(seg["p_vaddr"], seg.data() + b"\0" * (seg["p_memsz"] - seg["p_filesz"]))  # RAM: .data, zeroed .bss

# Self-test: MUTATE=<function> makes that function return immediately, to show
# the checks catch a broken hook.
if os.environ.get("MUTATE"):
    uc.mem_write(syms[os.environ["MUTATE"]] & ~1, b"\x70\x47")  # bx lr

RET = 0x2001F000  # return address: emulation stops here
USBP = 0x2001E000  # fake USBDriver


def call(name, *args):
    for reg, value in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], args):
        uc.reg_write(reg, value)
    uc.reg_write(UC_ARM_REG_SP, 0x2001C000)
    uc.reg_write(UC_ARM_REG_LR, RET | 1)
    uc.emu_start(syms[name] | 1, RET, count=2_000_000)
    return uc.reg_read(UC_ARM_REG_R0)


def set_via_mode(on):
    uc.mem_write(syms["usb_shows_via"], bytes([1 if on else 0]))


def setup(bmRequestType, bRequest, wValue, wIndex, wLength=0x400):
    uc.mem_write(USBP, b"\0" * 0x200)
    uc.mem_write(USBP + USBD["config"], struct.pack("<I", syms["usbcfg"]))  # QMK's real USBConfig
    uc.mem_write(USBP + USBD["setup"], struct.pack("<BBHHH", bmRequestType, bRequest, wValue, wIndex, wLength))


def get_descriptor(wValue, wIndex=0):
    """Standard GET_DESCRIPTOR to the device, as ChibiOS dispatches it."""
    setup(0x80, 6, wValue, wIndex)
    p = call("usb_get_descriptor_cb", USBP, wValue & 0xFF, wValue >> 8, wIndex)
    if p == 0:
        return None
    size, addr = struct.unpack("<II", uc.mem_read(p, 8))
    return bytes(uc.mem_read(addr, size))


def request(bmRequestType, bRequest, wValue, wIndex):
    """A request through QMK's requests hook: (handled, data it would send, buffer address)."""
    setup(bmRequestType, bRequest, wValue, wIndex)
    handled = call("usb_requests_hook_cb", USBP) & 0xFF
    nxt = struct.unpack("<I", uc.mem_read(USBP + USBD["ep0next"], 4))[0]
    n = struct.unpack("<I", uc.mem_read(USBP + USBD["ep0n"], 4))[0]
    return bool(handled), (bytes(uc.mem_read(nxt, n)) if nxt and n else b""), nxt


def parse_config(d):
    """Checks a configuration descriptor's structure; returns its interfaces."""
    assert d[1] == 2, "not a configuration descriptor"
    total, num = struct.unpack("<H", d[2:4])[0], d[4]
    assert total == len(d), f"wTotalLength {total} != {len(d)}"
    ifaces, i, cur = [], 0, None
    while i < len(d):
        ln, typ = d[i], d[i + 1]
        assert ln >= 2 and i + ln <= len(d), "bad descriptor length"
        if typ == 4:
            cur = {"num": d[i + 2], "alt": d[i + 3], "eps": []}
            ifaces.append(cur)
        elif typ == 5:
            cur["eps"].append(d[i + 2])
        i += ln
    nums = [x["num"] for x in ifaces if x["alt"] == 0]
    assert nums == list(range(num)), f"interfaces {nums} vs bNumInterfaces {num}"
    return ifaces


failures = 0


def check(cond, msg):
    global failures
    print(("  ok    " if cond else "  FAIL  ") + msg)
    failures += not cond


VIA_PAGE = bytes([0x06, 0x60, 0xFF])  # Usage Page (0xFF60), how VIA finds keyboards

print("VIA mode (F1 held at plug-in):")
set_via_mode(True)
via_cfg = get_descriptor(0x0200)
via_ifaces = parse_config(via_cfg)
report = {x["num"]: request(0x81, 6, 0x2200, x["num"])[1] for x in via_ifaces}
check([x["num"] for x in via_ifaces] == [0, 1, 2], f"3 interfaces {[x['num'] for x in via_ifaces]}")
check(report[1][:3] == VIA_PAGE, "interface 1 is VIA (usage page 0xFF60)")
check(any(not e & 0x80 for x in via_ifaces for e in x["eps"]), "has an OUT endpoint (VIA)")

print("Hardened mode (default):")
set_via_mode(False)
cfg = get_descriptor(0x0200)
ifaces = parse_config(cfg)
check([x["num"] for x in ifaces] == [0, 1], f"2 interfaces, numbered contiguously {[x['num'] for x in ifaces]}")
check(all(e & 0x80 for x in ifaces for e in x["eps"]), f"only IN endpoints {[hex(e) for x in ifaces for e in x['eps']]}")
check(all(request(0x81, 6, 0x2200, i)[1][:3] != VIA_PAGE for i in (0, 1)), "no interface reports usage page 0xFF60")
check(cfg[5:9] == via_cfg[5:9], "configuration attributes and power unchanged")
check(len(ifaces) == 2 and ifaces[0]["eps"] == via_ifaces[0]["eps"] and ifaces[1]["eps"] == via_ifaces[2]["eps"], "keyboard and shared interfaces keep their endpoints")
ok0, d0, _ = request(0x81, 6, 0x2200, 0)
ok1, d1, _ = request(0x81, 6, 0x2200, 1)
check(ok0 and d0 == report[0], "host interface 0 -> keyboard report descriptor")
check(ok1 and d1 == report[2], "host interface 1 -> shared (media/NKRO) report descriptor")
check(not request(0x81, 6, 0x2200, 2)[0], "host interface 2 doesn't exist (request refused)")
ok, d, _ = request(0x81, 6, 0x2100, 1)
check(ok and len(d) == 9 and struct.unpack("<H", d[7:9])[0] == len(report[2]), "HID descriptor of host interface 1 matches the shared report")
check(request(0x21, 0x09, 0x0200, 1)[2] == syms["set_report_buf"], "Caps Lock report to host interface 1 is accepted (NKRO)")
check(request(0x21, 0x09, 0x0200, 0)[2] == syms["set_report_buf"], "Caps Lock report to host interface 0 is accepted")
check(request(0x21, 0x0A, 0x0000, 1)[0], "SET_IDLE to host interface 1 is accepted")
check(request(0xA1, 0x02, 0x0000, 1)[0], "GET_IDLE on host interface 1 is answered")
check(get_descriptor(0x0100)[1] == 1, "device descriptor unaffected")

print("Back to VIA mode:")
set_via_mode(True)
check(get_descriptor(0x0200) == via_cfg, "original configuration descriptor presented again")
check(request(0x81, 6, 0x2200, 1)[1] == report[1], "interface 1 is VIA again")

print("FAILED" if failures else "ALL PASSED", f"({failures} failures)")
sys.exit(1 if failures else 0)
