# IdentiPi Fingerprint Password Device — Design Specification

**Target hardware:** Raspberry Pi Pico 2 W (RP2350) + SB Components IdentiPi HAT

---

## 1. Purpose

A USB device that types a stored secret password on a keyboard interface,
released only on a matching fingerprint. Theft of the device must not yield
the stored passwords.

---

## 2. Hardware facts (verified on real hardware)

These were established empirically during bring-up. Several contradict the
vendor documentation.

| Item | Value | Note |
|---|---|---|
| Display | ST7789, 240×135 | MADCTL `0x60` (landscape), window offsets x=40, y=53 |
| Display SPI | SPI1: SCLK GP10, MOSI GP11 | CS GP9, D/C GP8, RST GP12, BL GP13 |
| Joystick | UP **GP15**, RIGHT **GP14**, LEFT GP16, DOWN GP17, SEL GP18 | **GP14/GP15 are swapped vs SB's published pinout** |
| Fingerprint UART | UART0, 19200 8N1, TX GP0, RX GP1 | enable GP3, LOW = on (board pulls it low itself) |
| Fingerprint protocol | **F5 8-byte protocol**, not AS608 | `F5 CMD P1 P2 P3 P0 CHK F5`, CHK = XOR of bytes 1–5 |
| Template storage | On the sensor module | Fingerprint data never touches Pico flash |

**Gotcha for MicroPython prototypes only:** `SPI(1)` defaults its MISO to GP8,
silently stealing the D/C line. Not applicable to the C/SDK build, where pin
muxing is explicit.

### Fingerprint protocol summary

Commands: `0x01/0x02/0x03` add (three scans), `0x04` delete user,
`0x05` delete all, `0x09` user count, `0x0B` 1:1 compare, `0x0C` 1:N compare,
`0x0A` user privilege, `0x28` comparison level.

Replies return `P3` as a status: `0x00` success, `0x01` fail, `0x04` full,
`0x05` no user, `0x07` already enrolled, `0x08` timeout. For 1:N compare, a
`P3` of 1–3 means **match**, with user ID = `P1<<8 | P2` and P3 the privilege.

> Note: SB's own `Read_1_N` demo treats non-zero P3 as an error and so
> misreports successful matches as "Failed".

---

## 3. Threat model

**In scope:** device is stolen. Attacker has physical possession, ordinary
tools, and can read the external flash chip or attempt SWD.

**Mitigations:** RP2350 secure boot (only signed firmware runs), encrypted
flash (decryption key in OTP, locked), permanent debug-port disable,
glitch detectors. Menu access gated behind a fingerprint match so the device
cannot simply be reconfigured to hand over its secrets.

**Out of scope:** funded laboratory attacks. Raspberry Pi's own RP2350 hacking
challenge saw secure boot defeated by voltage and laser fault injection
(errata E16, E20). The goal is to defeat casual and opportunistic extraction,
not a state actor.

**Explicit non-goal:** the fingerprint sensor is not a strong authenticator.
It is a convenience factor with a real false-accept rate, and its comparison
level (`0x28`) is the only tuning available.

**Known, unmitigated gap: sensor substitution.** The fingerprint sensor is
a dumb UART peripheral (F5 protocol) with no serial number, password, or
attestation capability we have found evidence of — confirmed by the
complete method list of SB Components' own `IdentiPi.py` reference class
(`__init__`, `display`, `calculate_checksum`, `calculation`, `Sleep`,
`Wake_up`, `Mode` [cmd `0x2D`, unexplored, but takes writable params so
looks like a setter, not a read], `user_count`, `add_fingerprint`,
`Delete_user`, `Delete_all_users`, `send_command`, `acknowledge`) —
nothing resembling a read-serial/identity command. An attacker with
physical possession and ordinary tools (already in scope above) can detach
the HAT, wire up a second F5-protocol sensor, enroll their own finger at a
matching `sensor_id`, and get a positive match indistinguishable from the
real thing. This bypasses secure boot, flash encryption, and debug-disable
entirely, since it never touches flash or debug — the device just types
the password out over HID exactly as designed for the legitimate owner.
This is a stronger claim than the false-accept-rate non-goal above: that
one is statistical (a different real finger occasionally matching); this
one is a deterministic, deliberate bypass with no crypto anywhere to defend
against it, since the sensor holds no secret to bind trust to. Not
firmware-fixable with this hardware. The only real mitigations are outside
firmware entirely: a tamper-evident seal or epoxy on the HAT header, so a
swap at least leaves visible evidence, even though it can't be prevented.

---

## 4. Data model

Fingerprint templates live on the sensor. Everything else lives in a single
encrypted blob in Pico flash, rewritten wholesale on change (avoids
partial-write corruption).

```
config {
    uint16  config_version
    uint8   password_count
    uint8   finger_count

    password[10] {
        uint8   in_use
        char    description[24]     // "Cloud Admin"
        char    secret[33]          // max 32 chars + NUL
    }

    finger[10] {
        uint8   in_use
        uint16  sensor_id           // ID held on the sensor module
        uint8   name_index          // index into FINGER_NAMES
        int8    password_index      // -1 = no password bound
    }

    uint32  failed_attempts         // persisted: must survive a replug
    uint32  lockout_until_ms
    uint8   host_layout             // 0=UK, 1=DE -- see 5.7 for what each affects
}
```

**Capacities:** 10 fingerprints, 32 passwords, 32-character maximum password.

**Finger names** (fixed list, chosen at enrollment):

```
Left Thumb    Left Index    Left Middle    Left Ring    Left Pinky
Right Thumb   Right Index   Right Middle   Right Ring   Right Pinky
```

**Binding:** many fingers → one password. Each finger binds to exactly one
password, or none. Set at enrollment, re-bindable any time from the menu's
"Assign password" item (5.5) — no WiFi UI needed, since WiFi is deferred
(5.7/9) and this covers the same need on-device. Deleting a password leaves
its fingers bound to nothing; scanning such a finger displays "No password
bound" and types nothing.

---

## 5. Behaviour

### 5.1 Idle

Displays: *"Scan your finger, or SEL for menu"* (shorter than it sounds here
— split across two lines so it reads clearly at the larger on-screen font
size; anything much longer runs off the 240px-wide display at that size)

- Text repositions every 10 s (mitigates image retention; this is an IPS LCD,
  so true burn-in is not a concern, but retention is)
- Backlight off after 60 s idle, waking on scan or joystick

### 5.2 Successful scan

The screen flashes green full-screen for half a second — readable at a
glance from across the room — then shows the detail, **typing concurrently**
so there is no dead pause overall:

```
    Right Thumb
    → Cloud Admin
```

Password typed over USB HID with randomised inter-key jitter (key hold fixed
at 10 ms, gap randomised 20–40 ms). A fixed cadence is a signature for anything
sampling USB timing, and some hosts drop keys at a steady rate.

If the finger is enrolled but bound to nothing: show "No password bound",
type nothing.

### 5.3 Failed scan

The screen flashes red full-screen for half a second, same reasoning as the
green match flash, then "Not recognised" for 2 s, return to idle. This flash
only happens on the passive idle-loop scan, not the menu-gate scan in 5.4 —
opening the menu is already a deliberate, watched action. **Rate limited:**
after 5 consecutive
failures, an escalating delay — 5 s, 15 s, 60 s, 300 s, capped at 15 min. A
success resets the counter.

The counter is **persisted to flash**, not held in RAM. A RAM-only counter is
defeated by unplugging and replugging the USB cable, which is trivial; the
penalty must survive a power cycle to mean anything. Writes occur only on
failure, so flash wear is not a concern.

The sensor offers no brute-force protection of its own, so without this a
stolen device can be attacked with fake fingers indefinitely.

### 5.4 Menu access

Joystick press → *"Scan enrolled finger for menu"* → menu on match.

**Bootstrap exception:** with zero fingerprints enrolled, the menu opens
without a scan. Nothing is stored yet, so there is nothing to protect.

### 5.5 Menu

| # | Item | Notes |
|---|---|---|
| 1 | Enroll fingerprint | Choose from the 10 names, three scans, then choose a password to bind |
| 2 | Delete fingerprint | Removes from sensor and config |
| 3 | Create password | On-screen keyboard |
| 4 | Change password | On-screen keyboard |
| 5 | Delete password | Bound fingers become unbound |
| 6 | Assign password | Move a password to a different finger, re-bind after a delete, or unassign a finger entirely — without re-enrolling or re-creating anything |
| 7 | Info | Counts, the finger→password map, firmware version |
| 8 | Info export | Types the same finger→password map and version over USB HID into whatever the host has focused — descriptions only, never the secrets themselves, since that would dump every stored password at once rather than one on a real match |
| 9 | Keyboard layout | UK / DE (US dropped) — see 5.7 for what each affects |

The list is visually grouped with a divider line after item 2 (the
fingerprint items) and after item 6 (the password items), so the two
groups plus "everything else" read as distinct at a glance.

Fingerprint pickers (Enroll's name choice, Delete/Assign's finger choice)
show left-hand and right-hand names in separate columns, grouped by hand
rather than split by position in the list — so if one hand has fewer
available/enrolled fingers than the other (one already used, say), that
column just shows fewer rows instead of a name from the other hand
bleeding across to keep the two columns even.

None of the menu's lists wrap top-to-bottom: pressing UP at the first row
or DOWN at the last just holds still, rather than jumping to the other end.

### 5.6 Password entry (on-screen keyboard)

Passwords are entered on the device itself with the joystick. No network is
involved, so the plaintext never crosses a boundary of any kind and the device
works with the radio permanently off.

Character grid navigated with UP/DOWN/LEFT/RIGHT, SEL to pick. A held
direction auto-repeats after a short initial delay, so scrolling through a
32-entry password list or moving across the grid doesn't need a press per
step. The letters page includes control cells:

```
  a b c d e f g h i j     [SHIFT]  toggle case
  k l m n o p q r s t     [123]    digits and symbols
  u v w x y z . , - _ @   [DEL]    backspace
  [SHIFT] [123] [DEL] [OK] [CANCEL]
```

The `[123]` page holds digits plus twenty punctuation/symbol characters,
requested for password strength, in two more rows of ten:

```
  0 1 2 3 4 5 6 7 8 9
  ! $ % & ( ) @ * - _
  < > ? = # : ; + / \
  [ABC] [DEL] [OK] [CANCEL]
```

Entry shows the text in clear (you are holding the device; the threat is
extraction from flash, not shoulder-surfing your own desk) with a character
count. Maximum 32 characters.

### 5.7 Keyboard layout handling

Only **UK and DE** are supported (US was dropped — not needed in practice).
HID sends scancodes, not characters, so what a given scancode produces
depends entirely on the host's own configured keyboard layout. The device
picks scancodes assuming the host is set to whichever of these two the user
selected; get it wrong and the wrong character appears.

**UK** differs from the underlying scancode table used internally (which is
US-shaped) for only three characters:

| Char | UK |
|---|---|
| `@` | Shift+' |
| `#` | dedicated ISO key (unshifted) |
| `\` | dedicated ISO key (unshifted) |

**DE (QWERTZ)** differs far more:

- **Y and Z are physically swapped.** Typing 'y' sends the scancode a UK/US
  keyboard would call "Z", and vice versa — two characters, two keys, just
  crossed. This affects any password containing either letter and is easy
  to overlook, since it only bites on this one specific layout.
- Most of the digit-row shift positions and the bottom-row punctuation live
  on entirely different keys, not just a different modifier:

  | Char | DE |
  |---|---|
  | `@` | AltGr+Q |
  | `#` | dedicated ISO key (unshifted) |
  | `\` | AltGr+ß (the key at the US `-` position) |
  | `&` `(` `)` `=` | Shift + the digit one position to the left of where US puts them (`6 8 9 0` instead of `7 9 0` and the bare `=` key) |
  | `+` `*` | the key *after* Ü, one slot further right than it looks (Ü itself sits at the US `[` position; `+`/`*` are at US `]`) |
  | `?` | Shift + the ß key |
  | `-` `_` | the key after the period key (unshifted / shifted) — *not* the US `-`/`_` key, which is ß/? on DE |
  | `/` | Shift+7 |
  | `<` `>` | dedicated ISO key (unshifted / shifted) |
  | `:` `;` | Shift + period / Shift + comma — DE has no dedicated semicolon key |

  `!`, `$`, `%`, `.`, `,` happen to land in the same place as the US-shaped
  table already assumes and need no override.

Bench-tested on real hardware: the full character set on both layouts —
`@`, `#`, `\`, the Y/Z swap, and `&()=+*?-_/<>:;` — is confirmed correct,
including on a real German host.

A **global `host_layout` setting** (UK / DE, default DE) is stored in config
and switchable from the menu. It's a single byte, and the enum was
renumbered when US was dropped — a device already carrying a stored value
from before this change will read back as whichever of UK/DE now sits at
that number, so re-selecting the layout once from the menu after updating
is worth doing to be sure.

Characters that move between layouts and are *not* included in the grid
(`" ~ | ^`) remain absent, so no further layout handling is needed for them.

**WiFi is deferred**, not designed out. If on-screen entry proves too tedious
in practice, the AP + HTTP admin flow can be added later behind the same
fingerprint gate. Deferring it removes roughly 40% of the firmware complexity
and the only network attack surface.

---

## 6. Firmware versioning

`FIRMWARE_VERSION` defined at the top of `version.h`, format
`MAJOR.MINOR.PATCH`. Shown on the Info screen and splash screen.

---

## 6a. Key material and what actually lives where

Two distinct secrets, doing two distinct jobs. Conflating them is the
easiest way to get this wrong.

| Secret | Kind | Lives | Written | Protects |
|---|---|---|---|---|
| Signing private key | asymmetric | **developer PC only** | generated once | proves firmware is ours |
| Signing public key hash | — | OTP | once, at provisioning | boot ROM checks against it |
| Config encryption key | symmetric (AES) | OTP, locked | once, at provisioning | the config blob |

**The signing keypair encrypts nothing.** It exists so the boot ROM can
verify the firmware image is ours. The private half never leaves the
developer's machine; only a hash of the public half is burned into OTP.

**The config encryption key is separate and symmetric.** It is what actually
encrypts stored passwords.

A third possibility — encrypting the firmware binary itself, which RP2350
supports as a separate feature — was deliberately not pursued. See "Why
firmware-binary encryption isn't used" below.

### OTP is written once; passwords change freely

Passwords never touch OTP. What is burned there is a *key*, and the key does
not change when the data it protects changes.

Changing a password means: decrypt the config blob into RAM, edit, re-encrypt
with the same OTP-held key, write back to flash. OTP is untouched. There is
no limit on how often this happens.

Flash endurance is roughly 100,000 erase cycles per sector. At a handful of
password changes per year this is irrelevant.

### The config blob needs its own key

RP2350's encrypted-flash-boot feature encrypts the **firmware binary**, not
arbitrary data written at runtime, so the config blob is handled explicitly
instead:

1. Generate a device-unique AES-256 key from the RP2350 hardware TRNG at
   provisioning time. **Never** from a PRNG such as MicroPython's `random`.
2. Burn it into an OTP page.
3. Set that page's lock bits so firmware may read it but debug access cannot.
4. Firmware encrypts and decrypts the config blob with it (AES-GCM, so
   tampering is detected rather than silently decrypting to garbage).

`src/otp_key.c` generates the key on-device from the hardware TRNG
(`pico/rand.h`, the same source used for HID jitter in `hid.c`) and burns
it via the bootrom's `rom_func_otp_access()`, at OTP rows `0xC0`-`0xCF`
(16 rows, ECC mode, 32 bytes) — with 16 more rows held in reserve at
`0xD0`-`0xDF` for a possible future rotation slot. Triggered from the menu
("Provision encryption key"), gated behind two sequential confirmations,
refuses if a key already exists (OTP bits only ever go 0→1 — there is no
"redo"). `src/config.c` picks between two on-flash formats by magic: plain
(no key provisioned) or AES-GCM-encrypted (once a key exists) — the blob
upgrades to encrypted form automatically on its next save.

All three protections below are in place: the key is generated on-device
and never leaves the chip, secure boot means only our firmware runs, and
debug access is permanently disabled. The three are a set — any one alone
leaves a hole:

| Missing | Attack that then works |
|---|---|
| Secure boot | flash attacker firmware that prints the key |
| Encrypted flash | desolder the flash chip and read plaintext |
| Debug disable | halt the CPU after boot, read the key from RAM |

### Why firmware-binary encryption isn't used

RP2350 supports encrypting the firmware binary itself (separate from the
config-blob encryption above), decrypting it into SRAM at boot. This was
tried and dropped. `picotool encrypt --embed` — the simpler of two available
approaches, bundling a decrypting stub into a single self-contained binary
— turned out to only support interactive, host-attached RAM execution
(`picotool load -x`), not genuine unattended persistent boot: the resulting
image loaded entirely into RAM and never actually ran from flash. The
alternative that does support persistent boot is a separate flash-resident
bootloader plus a flash partition table, matching the official
`pico-examples/bootloaders/encrypted` reference — a substantial
architectural addition, not a small follow-on step.

It wasn't worth building for this project: the source is public, and every
actual secret already lives outside the firmware binary (the config key in
OTP, passwords in the separately-encrypted flash blob) — so encrypting the
binary would only have hidden logic that's public anyway, not protected any
secret.

### Losing the signing key bricks the board

Once secure boot is enabled and the lock bits are burned, the board runs only
firmware signed with that key, permanently. There is no recovery path,
because a recovery path would be a bypass. Back the private key up before
burning anything.

---

## 7. Build and provisioning

**Toolchain:** Pico SDK + CMake, not Arduino. The Arduino core cannot reach
secure boot, OTP, or encrypted flash; those steps would need `picotool`
outside the IDE regardless.

**Workflow:** source is written here → you build locally (VS Code "Raspberry
Pi Pico" extension is the least painful route on Windows) → `.uf2` → BOOTSEL
and drag.

**Key handling:** the signing keypair is generated on your machine with
`openssl`/`picotool` and never leaves it. OTP fuse writes are permanent and
are run deliberately, with the board in hand.

Provisioning order at milestone 5 (see `PROVISIONING.md` for the full
command sequence):

1. Generate the signing keypair (ECDSA, secp256k1, PEM), back it up
2. Generate the config AES key from the hardware TRNG, burn to OTP
3. Sign and flash firmware, burn the public key hash to OTP, enable secure
   boot
4. Enable glitch detectors, burn `DEBUG_DISABLE`, hard-lock the OTP pages
   holding the boot keys and the config key — **the actual point of no
   return**, done only after step 3's firmware has been exercised
   repeatedly and confirmed working

Step 3 stays recoverable (reflash a corrected signed image) as long as
debug access is live; step 4 is not.

---

## 8. Milestones

| # | Deliverable | Verifies |
|---|---|---|
| 1 | CMake skeleton, display, fingerprint driver, idle screen | Toolchain works end to end |
| 2 | USB HID typing with jitter | TinyUSB on RP2350 |
| 3 | Config storage in flash (plaintext), menu, enrollment, binding | Full logic, no crypto yet |
| 4 | On-screen keyboard for password entry | Password management |
| 5 | Encrypted flash, secure boot, OTP lock, debug disable | The actual security |

All five milestones are complete. Milestones 1–4 build a fully working
device with plaintext storage; milestone 5 is what makes theft yield
nothing, and was deliberately last because its OTP steps are irreversible.

---

## 9. Open items

- **Comparison level.** Command `0x28` tunes the sensor's false-accept rate.
  Worth measuring against a non-enrolled finger before trusting the default.
- **WiFi.** Deferred in favour of on-screen entry. Revisit only if entering
  a 32-character password with a joystick proves intolerable.
- **Font-size consistency.** A few screens have minor inconsistency in font
  size choice; low priority, cosmetic only.
