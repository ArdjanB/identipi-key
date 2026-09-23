# Licence

## MIT License

Copyright (c) 2026 Ardjan

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

## Important: no warranty — security and bricking risks

The notice below explains what the "AS IS" clause above means for this
project in practice. It does not change the licence terms.

**This software is provided AS IS, with NO WARRANTY of any kind.** You use,
build, flash and provision it entirely at your own risk.

### Security

- This is a hobby project. **No one has audited it**, and it is not certified
  for any purpose. It may contain bugs or design flaws that expose stored
  passwords.
- Known limitations are documented in `DESIGN.md` §3. These include the fact
  that the fingerprint sensor cannot prove its identity to the
  microcontroller, so an attacker with physical access could swap it for
  another sensor. The list may not be complete.
- Do not rely on this device as the only copy of any password or as the only
  thing protecting it. Keep independent backups of every stored secret.

### Permanent hardware changes (bricking)

- The provisioning steps in `PROVISIONING.md`, and the on-device key
  provisioning menu action, **write to the RP2350's one-time-programmable
  (OTP) memory. These writes are permanent and cannot be undone.**
- Enabling secure boot, burning `DEBUG_DISABLE`, or setting OTP page lock
  bits can leave a board **permanently unable to run** any firmware you
  can't sign with the matching key, **or permanently undebuggable**. If you
  lose the signing key, or burn a wrong value, the board cannot be recovered.
- If the OTP config key is lost or damaged, the data encrypted with it
  cannot be recovered.
- Flashing, provisioning, or modifying the hardware can damage the
  Raspberry Pi Pico 2 W, the IdentiPi HAT, the fingerprint sensor, or
  connected equipment.

The authors and copyright holders accept **no liability** for lost
passwords, data leaks, account compromise, bricked or damaged hardware, or
any other loss or damage from using this software or following its
documentation.

---

## Third-party components

- `pico_sdk_import.cmake` — Copyright (c) 2020 Raspberry Pi (Trading) Ltd.,
  BSD-3-Clause (see the header in that file).
- `src/font6x9.h` — glyphs taken from the X11 `koi6x9` bitmap font
  (xfonts-cyrillic), distributed under that font's original terms.
- The Pico SDK, TinyUSB and mbedTLS are build dependencies and are not
  included in this repository. They are under their own licences
  (BSD-3-Clause, MIT, and Apache-2.0 or GPL-2.0-or-later respectively).
