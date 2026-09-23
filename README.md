# IdentiPi Key

A fingerprint-gated USB keyboard. Scan an enrolled finger, the device types
a stored password. No app, no network, no cloud — the password never
leaves the device except as keystrokes typed directly into whatever has
keyboard focus.

Built on a Raspberry Pi Pico 2 W (RP2350) and the [SB Components IdentiPi
HAT](https://github.com/sbcshop/IdentiPi_Software) (display, fingerprint sensor, joystick).

## Features

- Enroll up to 10 fingerprints, bind each to one of up to 32 stored
  passwords (many fingers can share one password)
- On-screen keyboard for entering passwords and descriptions — no host
  software needed, ever
- USB HID typing with randomised inter-key timing
- UK and DE host keyboard layouts, including DE's full Y/Z swap and
  non-obvious punctuation remapping
- Escalating lockout after repeated failed scans, persisted across power
  cycles
- Config data (passwords) encrypted at rest with a key generated entirely
  on-device from the RP2350's hardware TRNG
- RP2350 secure boot, `DEBUG_DISABLE`, and OTP page locks — only signed
  firmware runs, and the encryption key can't be read out over SWD or
  BOOTSEL once provisioned

See `DESIGN.md` for the full specification, threat model, and known
limitations (including one worth reading before relying on this device:
the fingerprint sensor itself can be physically substituted by an
attacker with the right tools — see `DESIGN.md` §3).

## Building

Requires **Pico SDK 2.0+** (RP2350 support; 1.x will not work).

```bash
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .   # if not already present
mkdir build && cd build
cmake -G Ninja ..
ninja
```

Output is `build/identipi.uf2`. Flash by holding BOOTSEL while plugging in
and dragging the file onto the RP2350 drive.

On Windows, the VS Code **Raspberry Pi Pico** extension supplies the SDK,
ARM GCC, CMake, and Ninja — use **Import Project** on this folder, not
"New C/C++ Project" (which overwrites `CMakeLists.txt`).

## Layout

```
CMakeLists.txt
src/
  version.h      firmware version + pin map
  main.c         state machine, idle/scan loop
  display.[ch]   ST7789 driver
  fp.[ch]        fingerprint sensor, F5 protocol
  buttons.[ch]   joystick, debounce, auto-repeat
  hid.[ch]       USB HID keyboard output, jittered timing
  config.[ch]    flash-backed config storage, AES-GCM encryption
  keyboard.[ch]  on-screen keyboard for password entry
  menu.[ch]      menu system, enrollment flow
  otp_key.[ch]   on-device OTP key generation/provisioning
  font6x9.h      generated bitmap font, ASCII 0x20-0x7E
tools/
  bump_version.py
```

## Security provisioning

Enabling secure boot and locking the device down is a separate, one-time,
irreversible step — see `PROVISIONING.md`. The device works fully without
it (plaintext-adjacent, config still AES-GCM encrypted but the key isn't
yet locked down); provisioning is what makes theft of the device actually
yield nothing.

## Hardware notes

A few things that cost real debugging time and are in none of the vendor
documentation — see `CLAUDE.md` for the complete list:

1. **The fingerprint sensor is not AS608/ZFM.** It speaks a distinct
   8-byte protocol (`F5 CMD P1 P2 P3 P0 CHK F5`) that SB's own datasheet
   never names.
2. **GP14 and GP15 are swapped** versus SB's published pinout.
3. **The display needs window offsets** (x=40, y=53) — it's a window into
   a larger controller GRAM, not addressed at (0,0).
4. On a 1:N fingerprint compare, `P3` carries the **match privilege** on
   success, not a status code — SB's own demo code gets this wrong and
   reports successful matches as failures.

## Licence

MIT — see [Licence.md](Licence.md). This software comes **as is, with no
warranty**. The provisioning steps make permanent OTP writes that can
permanently brick a board, and nobody has audited the security design. Read
the disclaimer in `Licence.md` before building or provisioning a device.

## Acknowledgments

The majority of this project's firmware — architecture, implementation,
debugging, and the security provisioning design — was written by
[Claude](https://claude.ai) (Anthropic), working from hardware in hand and
iterating against real test results.
