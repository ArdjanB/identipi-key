/*
 * mbedtls_config.h -- minimal mbedtls feature set, milestone 5
 *
 * We need exactly one thing: AES-GCM to authenticate-encrypt the config
 * blob. GCM_C requires CIPHER_C plus a block cipher (AES_C) -- mirrors
 * the "Requires:" note in mbedtls's own reference mbedtls_config.h.
 * Everything else (TLS, x509, entropy/DRBG, hashes) stays off: we
 * generate the key and nonce ourselves from the hardware TRNG
 * (pico/rand.h), the same source already used for HID jitter in hid.c,
 * so mbedtls's own entropy/DRBG machinery is never invoked.
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_GCM_C

/* entropy_poll.c compiles unconditionally (not gated behind a feature
 * macro) and hard-errors without this on anything that isn't Unix or
 * Windows. We never call into mbedtls's entropy/DRBG subsystem at all --
 * the key and nonce come straight from pico/rand.h -- so this is purely
 * to let that file compile to a no-op on bare metal. */
#define MBEDTLS_NO_PLATFORM_ENTROPY

#endif /* MBEDTLS_CONFIG_H */
