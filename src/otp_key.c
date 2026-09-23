/*
 * otp_key.c -- OTP-resident config key, milestone 5
 */
#include "otp_key.h"

#include "pico/bootrom.h"
#include "pico/rand.h"

#include <string.h>

/* Rows 0xC0-0xCF (16 rows, ECC mode = 2 bytes/row = 32 bytes): the
 * config's AES-256 key. Rows 0xD0-0xDF held in reserve for a future
 * rotation slot (see CLAUDE.md's OTP key-rotation design discussion).
 *
 * CONFIRMED against a real `picotool otp list` dump of this board's OTP
 * row map (2026-09-21): named/reserved rows run 0x0000-0x0064 (chip ID,
 * calibration, boot config), then 0x0080-0x00BF is entirely BOOTKEY0-3
 * (the four secure-boot public-key-hash slots -- this is what "page 2
 * conditionally reserved" turns out to mean), then the listing jumps
 * straight to 0x0F48 (KEY1_0, the OTP access keys) with *nothing* listed
 * between 0x00C0 and 0x0F47. Our range sits in the middle of that gap --
 * not inferred from secondary docs anymore, directly confirmed on this
 * actual chip. Page-lock storage lives at 0x0f80-0x0fff (PAGE0_LOCK0
 * through PAGE63_LOCK1, one pair per page), for when the later
 * lock-the-page step happens.
 */
#define OTP_KEY_ROW_BASE 0x0C0u
_Static_assert(OTP_KEY_LEN % 2 == 0, "ECC mode needs a whole number of rows");

static otp_cmd_t make_cmd(uint32_t row, bool is_write) {
    otp_cmd_t cmd;
    cmd.flags = (row & OTP_CMD_ROW_BITS) | OTP_CMD_ECC_BITS;
    if (is_write) cmd.flags |= OTP_CMD_WRITE_BITS;
    return cmd;
}

bool otp_key_read(uint8_t key[OTP_KEY_LEN]) {
    otp_cmd_t cmd = make_cmd(OTP_KEY_ROW_BASE, false);
    int rc = rom_func_otp_access(key, OTP_KEY_LEN, cmd);
    return rc == BOOTROM_OK;
}

bool otp_key_is_provisioned(void) {
    uint8_t key[OTP_KEY_LEN];
    if (!otp_key_read(key)) return false;

    uint8_t zero[OTP_KEY_LEN] = {0};
    bool all_zero = memcmp(key, zero, OTP_KEY_LEN) == 0;
    memset(key, 0, sizeof(key));   /* don't let a real key linger on the stack */
    return !all_zero;
}

bool otp_key_provision(void) {
    if (otp_key_is_provisioned()) return false;   /* never overwrite */

    uint8_t key[OTP_KEY_LEN];
    for (size_t i = 0; i < OTP_KEY_LEN; i += 8) {
        uint64_t r = get_rand_64();
        size_t n = (OTP_KEY_LEN - i) < 8 ? (OTP_KEY_LEN - i) : 8;
        memcpy(key + i, &r, n);
    }

    otp_cmd_t cmd = make_cmd(OTP_KEY_ROW_BASE, true);
    int rc = rom_func_otp_access(key, OTP_KEY_LEN, cmd);
    memset(key, 0, sizeof(key));
    return rc == BOOTROM_OK;
}
