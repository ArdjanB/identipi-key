/*
 * config.c -- flash-backed configuration store, milestones 3 and 5
 */
#include "config.h"
#include "hid.h"        /* HOST_LAYOUT_DEFAULT */
#include "otp_key.h"    /* milestone 5: the config-blob AES key */

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico/rand.h"
#include "mbedtls/gcm.h"
#include "mbedtls/cipher.h"

#include <string.h>

const char *const FINGER_NAMES[FINGER_NAME_COUNT] = {
    "Left Thumb",  "Left Index",  "Left Middle",  "Left Ring",  "Left Pinky",
    "Right Thumb", "Right Index", "Right Middle", "Right Ring", "Right Pinky",
};

/* Two on-flash formats at the same offset, distinguished by magic.
 * CONFIG_MAGIC_PLAIN is milestone 3's original format, kept so a device
 * with no key provisioned yet (or one that just got a key but hasn't
 * saved since) keeps working exactly as before -- provisioning a key is
 * not itself a migration; the *next* save silently upgrades the blob. */
#define CONFIG_MAGIC_PLAIN     0x49504B31u   /* "IPK1" */
#define CONFIG_MAGIC_ENCRYPTED 0x49504B32u   /* "IPK2" */

#define GCM_NONCE_LEN 12
#define GCM_TAG_LEN   16

/* NOT the last sector -- that's the obvious choice and it's wrong on this
 * board. picotool adds an "abs-block" to every UF2 as the fix for RP2350
 * erratum E10: a 256-byte marker at a fixed address (0x10FFFF00) meant to
 * land past the end of flash -- a no-op -- on any chip smaller than 16 MB.
 * On our 4 MB flash that address *aliases* back into real flash space, and
 * it lands inside the last sector. Every plain drag-and-drop reflash was
 * silently re-erasing our config as a side effect of that errata
 * workaround, nothing to do with our own erase/program logic. Parking
 * comfortably away from both ends -- firmware growth at the bottom,
 * erratum aliasing at the top -- avoids the whole class of problem. See
 * CLAUDE.md hardware fact #8.
 */
#define CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES / 2)

typedef struct {
    uint32_t magic;
    config_t cfg;
    uint32_t checksum;
} config_flash_plain_t;

typedef struct {
    uint32_t magic;
    uint8_t  nonce[GCM_NONCE_LEN];
    uint8_t  ciphertext[sizeof(config_t)];
    uint8_t  tag[GCM_TAG_LEN];
} config_flash_encrypted_t;

typedef union {
    config_flash_plain_t     plain;
    config_flash_encrypted_t enc;
} config_flash_u;

#define PROGRAM_LEN \
    (((sizeof(config_flash_u) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE)

_Static_assert(PROGRAM_LEN <= FLASH_SECTOR_SIZE,
              "config_t grew past the one flash sector reserved for it");

/* Not cryptographic -- just catches a torn write in the plaintext format.
 * The encrypted format doesn't need this: GCM's own authentication tag
 * is real cryptographic tamper detection, per DESIGN.md 6a. */
static uint32_t checksum_of(const config_t *cfg) {
    const uint8_t *p = (const uint8_t *) cfg;
    uint32_t sum = 0x811C9DC5u;
    for (size_t i = 0; i < sizeof(*cfg); i++) {
        sum ^= p[i];
        sum *= 16777619u;
    }
    return sum;
}

static void set_defaults(config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->config_version = 1;
    cfg->host_layout = HOST_LAYOUT_DEFAULT;
    for (int i = 0; i < MAX_FINGERS; i++) cfg->finger[i].password_index = -1;
}

void config_load(config_t *cfg) {
    const uint8_t *flash_base = (const uint8_t *) (XIP_BASE + CONFIG_FLASH_OFFSET);
    const config_flash_encrypted_t *enc = (const config_flash_encrypted_t *) flash_base;

    uint8_t key[OTP_KEY_LEN];
    if (otp_key_read(key)) {
        bool ok = false;
        if (enc->magic == CONFIG_MAGIC_ENCRYPTED) {
            mbedtls_gcm_context gcm;
            mbedtls_gcm_init(&gcm);
            mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, OTP_KEY_LEN * 8);
            int rc = mbedtls_gcm_auth_decrypt(&gcm, sizeof(config_t),
                                              enc->nonce, GCM_NONCE_LEN,
                                              NULL, 0,
                                              enc->tag, GCM_TAG_LEN,
                                              enc->ciphertext, (uint8_t *) cfg);
            mbedtls_gcm_free(&gcm);
            ok = (rc == 0);
        }
        memset(key, 0, sizeof(key));
        if (ok) return;
        /* A key exists but nothing valid encrypted was found -- most
         * likely the key was just provisioned and nothing has been
         * saved since. Fall through and try the plaintext format. */
    }

    const config_flash_plain_t *plain = (const config_flash_plain_t *) flash_base;
    if (plain->magic == CONFIG_MAGIC_PLAIN &&
        checksum_of(&plain->cfg) == plain->checksum) {
        memcpy(cfg, &plain->cfg, sizeof(*cfg));
        return;
    }
    set_defaults(cfg);
}

bool config_save(config_t *cfg) {
    static uint8_t buf[PROGRAM_LEN] __attribute__((aligned(4)));
    memset(buf, 0xFF, sizeof(buf));

    uint8_t key[OTP_KEY_LEN];
    if (otp_key_read(key)) {
        config_flash_encrypted_t *out = (config_flash_encrypted_t *) buf;
        out->magic = CONFIG_MAGIC_ENCRYPTED;

        /* A fresh random nonce per save, from the hardware TRNG -- GCM
         * requires a unique nonce per encryption under the same key, and
         * a save happens rarely enough that a random 96-bit nonce is
         * astronomically unlikely to ever repeat. */
        uint32_t r0 = get_rand_32(), r1 = get_rand_32(), r2 = get_rand_32();
        memcpy(out->nonce,     &r0, 4);
        memcpy(out->nonce + 4, &r1, 4);
        memcpy(out->nonce + 8, &r2, 4);

        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, OTP_KEY_LEN * 8);
        mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, sizeof(config_t),
                                  out->nonce, GCM_NONCE_LEN, NULL, 0,
                                  (const uint8_t *) cfg, out->ciphertext,
                                  GCM_TAG_LEN, out->tag);
        mbedtls_gcm_free(&gcm);
    } else {
        config_flash_plain_t *out = (config_flash_plain_t *) buf;
        out->magic = CONFIG_MAGIC_PLAIN;
        out->cfg = *cfg;
        out->checksum = checksum_of(cfg);
    }
    memset(key, 0, sizeof(key));

    /* Interrupts off for the duration: flash is unreadable (so
     * un-executable) while it's being erased/programmed, and the SDK's
     * flash functions run from RAM specifically so this is safe -- but
     * only if nothing else (e.g. the USB IRQ) can preempt into
     * flash-resident code while it's off-line. Single core, no coordination
     * with core1 needed. */
    uint32_t saved = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_FLASH_OFFSET, buf, sizeof(buf));
    restore_interrupts(saved);

    /* Verify by reading the whole thing back through the same path
     * config_load() uses -- exercises the real decrypt+auth round trip
     * in the encrypted case, not just a raw byte comparison. */
    config_t check;
    config_load(&check);
    return memcmp(&check, cfg, sizeof(check)) == 0;
}

int config_find_free_password(const config_t *cfg) {
    for (int i = 0; i < MAX_PASSWORDS; i++)
        if (!cfg->password[i].in_use) return i;
    return -1;
}

int config_find_free_finger(const config_t *cfg) {
    for (int i = 0; i < MAX_FINGERS; i++)
        if (!cfg->finger[i].in_use) return i;
    return -1;
}

int config_finger_by_sensor_id(const config_t *cfg, uint16_t sensor_id) {
    for (int i = 0; i < MAX_FINGERS; i++)
        if (cfg->finger[i].in_use && cfg->finger[i].sensor_id == sensor_id)
            return i;
    return -1;
}

bool config_name_in_use(const config_t *cfg, uint8_t name_index) {
    for (int i = 0; i < MAX_FINGERS; i++)
        if (cfg->finger[i].in_use && cfg->finger[i].name_index == name_index)
            return true;
    return false;
}

int config_add_password(config_t *cfg, const char *description,
                        const char *secret) {
    int idx = config_find_free_password(cfg);
    if (idx < 0) return -1;

    password_slot_t *slot = &cfg->password[idx];
    memset(slot, 0, sizeof(*slot));
    slot->in_use = 1;
    strncpy(slot->description, description, DESC_LEN - 1);
    strncpy(slot->secret, secret, SECRET_LEN - 1);
    cfg->password_count++;
    return idx;
}

void config_delete_password(config_t *cfg, int index) {
    if (index < 0 || index >= MAX_PASSWORDS || !cfg->password[index].in_use)
        return;

    for (int i = 0; i < MAX_FINGERS; i++)
        if (cfg->finger[i].in_use && cfg->finger[i].password_index == index)
            cfg->finger[i].password_index = -1;

    memset(&cfg->password[index], 0, sizeof(cfg->password[index]));
    cfg->password_count--;
}

int config_add_finger(config_t *cfg, uint16_t sensor_id, uint8_t name_index,
                      int8_t password_index) {
    int idx = config_find_free_finger(cfg);
    if (idx < 0) return -1;

    finger_slot_t *slot = &cfg->finger[idx];
    slot->in_use = 1;
    slot->sensor_id = sensor_id;
    slot->name_index = name_index;
    slot->password_index = password_index;
    cfg->finger_count++;
    return idx;
}

void config_delete_finger(config_t *cfg, int index) {
    if (index < 0 || index >= MAX_FINGERS || !cfg->finger[index].in_use)
        return;
    memset(&cfg->finger[index], 0, sizeof(cfg->finger[index]));
    cfg->finger[index].password_index = -1;
    cfg->finger_count--;
}
