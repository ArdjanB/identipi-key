# Provisioning: signing, secure boot, and lock-down

This covers the genuinely irreversible part of milestone 5: generating a
signing key, enabling RP2350 secure boot, and burning the OTP fuses that
lock everything down. If you're building this project for your own use,
this is the last stage, run once per board.

**Every command here touches key material or OTP and must be run by you,
by hand, with the board in hand** — never from an assistant session or any
automated script. A private key or OTP write that has passed through a
chat log or CI pipeline is not private, and OTP writes cannot be undone.

Config-blob encryption (a separate, earlier step — the on-device
`otp_key_provision()` menu action) should already be done before starting
this. Firmware-binary encryption is deliberately **not** part of this
project's design — see `DESIGN.md` §6a for why.

## Before you start

- Generate everything on your own machine; the private key never leaves it.
- Back up `private.pem` before relying on it for anything — losing it
  after secure boot is enabled means the board can never run new firmware
  again.
- **Recommended**: provision a spare, unfused board first if you have one.
  The early steps (signing, secure boot) stay recoverable by reflashing a
  corrected signed image as long as debug access is still live — but the
  final lock-down step (`DEBUG_DISABLE` + OTP page locks) is not. If you
  only have one board and choose to provision it directly, budget for the
  possibility that a mistake at that last step means retiring the chip
  and starting over on a new one — there's no in-between recovery.
- Verify every OTP selector name below against `picotool otp get`/`otp set
  --help` on your own installed picotool version before running it. Names
  and flags can drift between versions; don't trust a copy-pasted command
  blindly for something this irreversible.

## 1. Secure boot

Generate the signing keypair (secp256k1, PEM):

```
openssl ecparam -name secp256k1 -genkey -noout -out private.pem
```

Build the firmware normally, then seal and sign it:

```
picotool seal --sign identipi.elf identipi.signed.elf private.pem otp-secureboot.json
```

Flash `identipi.signed.elf` (`picotool load -x identipi.signed.elf`, or
convert to `.uf2` first with `picotool uf2 convert`) and confirm the device
boots and runs completely normally — this is the last easy moment to
compare signed-vs-plain behaviour before OTP changes make that harder.

`otp-secureboot.json` bundles everything the next step needs: the 32-byte
public-key hash, `KEY_VALID` for the slot it used, and
`SECURE_BOOT_ENABLE`. One command burns all three:

```
picotool otp load otp-secureboot.json
```

**Verify the key hash landed correctly before rebooting into a device
that will now enforce signature checks against it.** The RP2350 bootrom's
own field documentation is explicit about this: a boot key with an
uncorrectable ECC fault, combined with secure boot enabled, permanently
bricks the device. `otp load` already reports what it wrote; cross-check
independently with `picotool otp dump` (reads raw OTP contents directly,
not just an echo of the same input) and compare the boot-key rows
byte-for-byte against what `otp load` reported.

Mark the three unused boot-key slots invalid, so no one can install an
alternate key later:

```
picotool otp set BOOT_FLAGS1.KEY_INVALID 0xE
```

(`0xE` = binary `1110`: one bit per slot, slot 0 — the one just used —
left clear/valid, slots 1–3 marked invalid. Adjust the bitmask if your
key landed in a different slot.)

Reboot (a real power cycle, not just a soft reset) and confirm the device
still works normally — the first real exercise of the bootrom's signature
enforcement, not just the sealing step.

## 2. Lock-down (irreversible)

**Do not proceed unless the signed firmware from step 1 has been exercised
thoroughly — multiple power cycles, real use, every major feature.** Once
this step is done, SWD/picotool debug access is gone permanently, and nothing
past this point is fixable if something is subtly wrong.

Enable glitch detectors and disable debug access (same OTP row as secure
boot):

```
picotool otp set CRIT1.GLITCH_DETECTOR_ENABLE 1
picotool otp set CRIT1.DEBUG_DISABLE 1
```

Lock the OTP pages holding the boot keys and the config encryption key
against external BOOTSEL/picotool access:

```
picotool otp set PAGE2_LOCK1.LOCK_BL 3
picotool otp set PAGE3_LOCK1.LOCK_BL 3
```

Power-cycle (full unplug/replug) and confirm the board still boots and
works correctly with debug access now gone. This is the last check that
means anything.

### Two things worth understanding before running the lock step

**A factory-preset gotcha.** Fresh RP2350 chips ship with
`PAGE1_LOCK1`/`PAGE2_LOCK1` already preset to `0x040404` (their `LOCK_NS`
field defaults to `READ_ONLY`). OTP bits can only be *set*, never cleared —
writing a value that would require clearing an already-set bit fails.
Using the field-qualified `otp set ROW.FIELD value` form (as above, not
`--raw`) sidesteps this automatically, since it only touches the specific
field you name and correctly preserves everything else already burned in
that row.

**`LOCK_BL` vs `LOCK_S` are not interchangeable.** `LOCK_BL` is documented
by the RP2350 itself as a "dummy" bit with "no hardware effect" — a
convention the chip's own trusted, immutable USB bootloader is designed to
honour, not a silicon-level gate. It's genuinely effective against casual,
opportunistic extraction (exactly what `picotool otp get` over BOOTSEL is),
which is this project's actual threat model. `LOCK_S`, by contrast, is a
real hardware gate covering *all* Secure-mode code — which almost
certainly includes the bootrom's own signature-verification read of the
boot-key hashes. Setting `LOCK_S = INACCESSIBLE` on the boot-key page risks
blocking the bootrom's own ability to verify signatures on every future
boot. Leave `LOCK_S`/`LOCK_NS` alone; only `LOCK_BL` is touched above.

## Value reference

`LOCK_S`/`LOCK_NS`/`LOCK_BL` are each two-bit fields: `0 = READ_WRITE`,
`1 = READ_ONLY`, `3 = INACCESSIBLE` (`2` is reserved, behaves as
`INACCESSIBLE`, don't use it directly).

`KEY_VALID`/`KEY_INVALID` in `BOOT_FLAGS1` are 4-bit fields, one bit per
boot-key slot (0–3).
