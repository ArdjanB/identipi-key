# IdentiPi Key — project context

Fingerprint-gated USB keyboard. Raspberry Pi Pico 2 W (RP2350) + SB Components
IdentiPi HAT. Scan a finger, the device types a stored password over USB HID.

Full specification is in `DESIGN.md`. Read it before changing behaviour.
OTP/secure-boot provisioning steps are in `PROVISIONING.md`.

## Build

Pico SDK 2.0+ (RP2350 support required; 1.x will not work).

```bash
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .   # if not already present
mkdir build && cd build
cmake -G Ninja ..
ninja
```

Output `build/identipi.uf2`. Flash by holding BOOTSEL while plugging in and
dragging the file onto the RP2350 drive.

On Windows the VS Code "Raspberry Pi Pico" extension supplies SDK, ARM GCC,
CMake and Ninja. Use **Import Project**, never "New C/C++ Project" — the
latter overwrites `CMakeLists.txt`.

## HARDWARE FACTS — verified on real hardware, contradict vendor docs

These cost days of debugging. Do not "correct" them back to what SB
Components' documentation says.

1. **The fingerprint sensor is NOT AS608/ZFM.** It speaks the F5 8-byte
   protocol at 19200 8N1:
   ```
   F5 CMD P1 P2 P3 P0 CHK F5      CHK = CMD^P1^P2^P3^P0
   ```
   Verified against a real capture: `f5 09 00 96 ff 00 60 f5`, where
   `09^00^96^ff^00 == 0x60`. SB's datasheet gives the baud rate but never
   names the protocol, which is what sent us down the AS608 path.

2. **On a 1:N compare (`0x0C`), `P3` carries the PRIVILEGE (1–3) on success,
   not a status code.** SB's own `Read_1_N` demo treats non-zero `P3` as an
   error and therefore reports successful matches as "Failed". Do not copy
   their logic.

3. **Joystick GP14 and GP15 are swapped** versus the published pinout.
   Physical UP is **GP15**, physical RIGHT is **GP14**. LEFT GP16, DOWN GP17,
   SEL GP18. All active-low with pull-ups.

4. **The display needs window offsets.** ST7789, MADCTL `0x60` (landscape),
   240×135, origin **x=40, y=53**. The 1.14" panel is a window inside the
   controller's 240×320 GRAM — write at 0,0 and you address unwritten memory,
   which renders as colour noise.

5. **`FP_EN` (GP3) is pulled LOW by the board itself**, so the sensor is
   enabled by default and the pin need not be driven.

6. **RP2350 erratum E9:** a GPIO in input mode with pull-down can float near
   2.2 V and read HIGH from pad leakage. Pull-down reads are not trustworthy
   for detecting whether a line is driven. RP2040 does not behave this way.

7. **On enroll (`0x01`/`0x02`/`0x03`), `P3` must be a non-zero privilege
   level (1–3), not reserved/zero.** Confirmed against SB's own
   `Demo_Add_Fingerprint.py` / `IdentiPi.py` (`add_fingerprint()`): the
   frame is `CMD P1 P2 P3 00`, ID in `P1:P2` (matching `DELETE_USER` and
   `COMPARE_1_N`), privilege in `P3`. Sending `P3=0` gets you a misleading
   `ack=00` "success" on all three scans with no template ever actually
   stored — no error, just nothing findable afterward by ID or by a
   broad 1:N search. This design doesn't use privilege level for
   anything; any value 1–3 is fine, it just can't be 0.

8. **Do not put persistent config data in the last flash sector — plain
   drag-and-drop reflashing silently wipes it.** Every SDK-2.x-built UF2
   for RP2350 gets an "abs-block" from picotool (the fix for erratum E10):
   a 256-byte marker at a fixed address, `0x10FFFF00`, meant to land past
   the end of flash — a no-op — on any chip smaller than 16 MB. On our
   4 MB flash that address *aliases* back into real flash space instead of
   missing it, landing inside the last sector (`0x103FF000`–`0x103FFFFF`).
   Confirmed by parsing the actual generated `identipi.uf2`: the abs-block
   is present, targets that address, and every plain reflash re-erases
   that sector as a side effect — nothing to do with our own erase/program
   code. Config now lives at `PICO_FLASH_SIZE_BYTES / 2` (`src/config.c`),
   comfortably clear of both firmware growth at the bottom of flash and
   this aliasing at the top.

## Fingerprint templates

Stored on the sensor module (capacity 500), never in Pico flash. They survive
reflashing and move between host boards. Config in flash references them by
sensor user ID.

## Milestones

| # | State | Content |
|---|---|---|
| 1 | **done** | CMake, display, F5 driver, joystick, idle screen, lockout |
| 2 | **done** | USB HID typing with jitter (TinyUSB) |
| 3 | **done** | Config storage in flash, menu, enrollment, finger→password binding. Every core flow (enroll, delete, change, assign, info/export, layout switch) is bench-confirmed on real hardware. |
| 4 | **done** (pulled into M3) | On-screen keyboard for password entry — built early since M3's password/description entry had no other input path |
| 5 | **done** | Encrypted flash, secure boot, OTP lock, debug disable |

Milestones 1–4 give a fully working device with **plaintext** storage.
Milestone 5 is what makes theft yield nothing, and was last because its OTP
writes are **irreversible**. Final scope: signing keypair, secure boot,
config-blob encryption, `DEBUG_DISABLE`, glitch detectors, and OTP page
locks on the boot-key and config-key pages — see `PROVISIONING.md`.
Firmware-binary encryption was deliberately scoped out: this project's
source is public and no real secret lives in the firmware binary itself
(see "Two separate secrets" below), so encrypting the binary wouldn't have
protected anything.

## Design rules that are easy to get wrong

- **The menu must require a fingerprint match to open** (except when zero
  fingers are enrolled — bootstrap). Without this, a thief presses the
  joystick, enrolls their own finger, binds it to the existing password, and
  the device hands over the secret. That defeats the entire security design.
- **The failed-attempt counter must live in flash, not RAM.** In RAM it is
  defeated by unplugging the USB cable, which is trivial. The escalating
  lockout (5 s, 15 s, 60 s, 300 s, cap 15 min) must survive a power cycle.
- **Only the inter-key gap is jittered, not the key hold.** A randomly varying
  hold duration can trip auto-repeat on some hosts and double characters.
- **Only UK and DE host layouts are supported — US was dropped.** A global
  `host_layout` setting (UK/DE, default DE) picks between them; the enum
  was renumbered when US was removed (0=UK, 1=DE now), so a device with a
  stored value from before this change needs the layout re-selected once
  from the menu. UK differs from the underlying US-shaped keycode table
  only for `@`, `#`, `\`. **DE differs far more**: Y and Z are physically
  swapped (not just relabelled), and most of the digit-row/punctuation
  keys the on-screen keyboard offers (`&()=+*?-_/<>:;`) live on different
  keys entirely, not just a different modifier — see DESIGN.md 5.7 for the
  full table. The full character set on both layouts, including the Y/Z
  swap, is bench-confirmed on real hardware. Do not add `" ~ | ^` to the
  grid.
- **Never generate signing keys or write OTP fuses from an assistant session.**
  The private key must never leave the user's machine, and OTP writes are
  permanent.
- **Two separate secrets — do not conflate them.** The signing keypair
  encrypts nothing; it only proves the firmware is ours, and only a hash of
  its public half goes in OTP. A *separate* symmetric AES key, in OTP,
  encrypts the config blob. The private signing key lives on the
  developer's PC and nowhere else. (A third, firmware-binary encryption
  key was considered and dropped — see `PROVISIONING.md` for why:
  `picotool encrypt --embed` only supports interactive RAM-execute, not
  persistent boot, and the real fix would have meant a separate bootloader
  plus flash partition table, not worth it for a project whose source is
  public and keeps every real secret outside the firmware binary anyway.)
- **Passwords never go in OTP.** OTP holds keys, which do not change when the
  data they protect changes. Changing a password is decrypt-edit-re-encrypt
  of the flash blob; OTP is untouched and there is no limit on how often.
- **The config AES key must come from the RP2350 hardware TRNG**, never a
  PRNG. Use AES-GCM so tampering is detected rather than silently decrypting
  to garbage.
- **The config AES key is generated and burned entirely on-device**
  (`otp_key_provision()` in `src/otp_key.c`, wired to a double-confirmed
  menu action) — it must never be logged, printed, displayed on screen, or
  transmitted anywhere, including for debugging. The whole point of
  generating it on-chip from the hardware TRNG is that it never has to
  exist anywhere else; don't undo that for a debug print. Provisioning it
  is still an OTP write with the board in hand, same as any other, even
  though the code path lives in firmware rather than a picotool command.
  Rows `0xC0`-`0xDF` (see `src/otp_key.c`) are confirmed free against a
  real `picotool otp list` dump of this board's OTP row map.
- **Exercise the signed boot pipeline thoroughly — multiple power cycles,
  real use — with debug access still live, before burning `DEBUG_DISABLE`
  or any OTP page lock bit.** That's the actual point of no return; nothing
  before it is. Debug access is the only way to diagnose a boot problem,
  and it's gone forever after lock-down. See `PROVISIONING.md`.
- **The fingerprint sensor cannot prove its own identity to the Pico.** It's
  a dumb UART peripheral with no serial number or attestation capability we
  have evidence for. Physically substituting it with another F5-protocol
  sensor, enrolled with an attacker's own finger at a matching ID, produces
  an indistinguishable "match" — bypassing secure boot, flash encryption,
  and debug-disable entirely, since it never touches flash or debug. Not
  fixable in firmware; documented as a known threat-model gap in
  `DESIGN.md` §3, not something to try to "solve" with a plausible-looking
  but ultimately fake check (e.g. an ID-count sanity check) — the sensor's
  capacity (500) is small enough that any such check is trivially defeated
  by an attacker who already controls the substitute hardware.

## Style

C11, Pico SDK. Keep the sensor driver non-blocking (`fp_send` + `fp_poll`) so
the UI stays responsive while the module waits for a finger — a blocking
search freezes the joystick for seconds.

Firmware version lives in `src/version.h`, shown on the splash and Info
screens. `tools/bump_version.py` auto-increments the patch digit and stamps
`FIRMWARE_BUILD_TIME` on every build (wired into CMakeLists.txt as a
custom target) — don't hand-edit those two fields, they get overwritten.
`FIRMWARE_DATE` is the milestone/release date and stays manual.
