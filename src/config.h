/*
 * config.h -- persistent device configuration, milestone 3
 *
 * A single struct, rewritten to flash wholesale on every change (never
 * partially) so a power loss mid-write can't corrupt it -- the previous
 * copy stays valid until the new one is fully programmed. See DESIGN.md
 * section 4.
 *
 * Passwords are stored in PLAINTEXT until milestone 5 adds flash
 * encryption (DESIGN.md section 6a). Fingerprint templates are never
 * stored here; they live on the sensor module and are referenced only by
 * sensor_id.
 *
 * DESIGN.md's config{} sketch also lists a persisted `lockout_until_ms`.
 * It is deliberately not stored here: the RP2350 has no battery-backed
 * clock, so an absolute timestamp from before a power cycle is meaningless
 * after one. What must survive a replug is failed_attempts -- the current
 * escalation step is a pure function of that count, recomputed fresh at
 * boot (see main.c). Persisting a stale "until" time would only ever be
 * wrong: either it's already past (no protection) or it assumes time
 * passed that we can't actually verify.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PASSWORDS 32
#define MAX_FINGERS   10
#define DESC_LEN      24   /* includes NUL, per DESIGN.md */
#define SECRET_LEN    33   /* 32 chars + NUL, per DESIGN.md capacities */

typedef struct {
    uint8_t in_use;
    char    description[DESC_LEN];
    char    secret[SECRET_LEN];
} password_slot_t;

typedef struct {
    uint8_t  in_use;
    uint16_t sensor_id;       /* ID on the sensor module */
    uint8_t  name_index;      /* index into FINGER_NAMES */
    int8_t   password_index;  /* -1 = unbound */
} finger_slot_t;

typedef struct {
    uint16_t        config_version;
    uint8_t         password_count;
    uint8_t         finger_count;
    password_slot_t password[MAX_PASSWORDS];
    finger_slot_t   finger[MAX_FINGERS];
    uint32_t        failed_attempts;
    uint8_t         host_layout;   /* host_layout_t from hid.h */
} config_t;

#define FINGER_NAME_COUNT 10
extern const char *const FINGER_NAMES[FINGER_NAME_COUNT];

/* Loads from flash, or fills in defaults if flash is blank/corrupt. */
void config_load(config_t *cfg);

/* Wholesale rewrite. Returns false if the flash write didn't verify. */
bool config_save(config_t *cfg);

int  config_find_free_password(const config_t *cfg);
int  config_find_free_finger(const config_t *cfg);
int  config_finger_by_sensor_id(const config_t *cfg, uint16_t sensor_id);
bool config_name_in_use(const config_t *cfg, uint8_t name_index);

/* Returns the new slot index, or -1 if full. Truncates to fit the field
 * sizes above. */
int config_add_password(config_t *cfg, const char *description,
                        const char *secret);

/* Also unbinds any fingers bound to this password. */
void config_delete_password(config_t *cfg, int index);

int  config_add_finger(config_t *cfg, uint16_t sensor_id, uint8_t name_index,
                       int8_t password_index);
void config_delete_finger(config_t *cfg, int index);

#endif /* CONFIG_H */
