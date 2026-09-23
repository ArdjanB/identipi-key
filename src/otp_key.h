/*
 * otp_key.h -- the config-blob AES-256 key, milestone 5
 *
 * Generated on-device from the RP2350 hardware TRNG and burned to OTP by
 * otp_key_provision() -- never on a host, never in a file, never in this
 * or any other chat session. See CLAUDE.md's design rules: the raw key
 * value must never be logged, displayed, or transmitted anywhere,
 * including for debugging.
 *
 * This is a *separate* key from the signing keypair (DESIGN.md 6a) --
 * that one proves firmware is ours and is generated on the developer's
 * own machine with picotool, never here.
 */
#ifndef OTP_KEY_H
#define OTP_KEY_H

#include <stdint.h>
#include <stdbool.h>

#define OTP_KEY_LEN 32   /* AES-256 */

bool otp_key_is_provisioned(void);

/* Reads the key into `key`. Returns false if not provisioned (or on a
 * genuine read failure -- either way, there's no usable key). */
bool otp_key_read(uint8_t key[OTP_KEY_LEN]);

/* Generates OTP_KEY_LEN bytes from the hardware TRNG and burns them.
 * Refuses (returns false, no OTP write attempted) if already
 * provisioned -- OTP bits only ever go 0->1, so there is no "redo". */
bool otp_key_provision(void);

#endif /* OTP_KEY_H */
