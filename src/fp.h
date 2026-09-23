/*
 * fp.h -- fingerprint sensor driver, F5 8-byte protocol
 *
 * NOT the AS608/ZFM EF01 protocol. This module uses:
 *     F5 CMD P1 P2 P3 P0 CHK F5      (8 bytes, both directions)
 *     CHK = CMD ^ P1 ^ P2 ^ P3 ^ P0
 *     19200 8N1
 *
 * Verified against a real capture from the hardware:
 *     f5 09 00 96 ff 00 60 f5
 * where 09^00^96^ff^00 == 0x60.
 *
 * Fingerprint templates live on the sensor module, never in Pico flash.
 */
#ifndef FP_H
#define FP_H

#include <stdint.h>
#include <stdbool.h>

/* Commands */
#define FP_CMD_ADD_1        0x01
#define FP_CMD_ADD_2        0x02
#define FP_CMD_ADD_3        0x03
#define FP_CMD_DELETE_USER  0x04
#define FP_CMD_DELETE_ALL   0x05
#define FP_CMD_USER_COUNT   0x09
#define FP_CMD_USER_PRIV    0x0A
#define FP_CMD_COMPARE_1_1  0x0B
#define FP_CMD_COMPARE_1_N  0x0C
#define FP_CMD_COMP_LEVEL   0x28
#define FP_CMD_TIMEOUT      0x2E

/* Status returned in P3 */
#define FP_ACK_SUCCESS      0x00
#define FP_ACK_FAIL         0x01
#define FP_ACK_FULL         0x04
#define FP_ACK_NO_USER      0x05
#define FP_ACK_OCCUPIED     0x06
#define FP_ACK_EXIST        0x07
#define FP_ACK_TIMEOUT      0x08

typedef struct {
    uint8_t cmd;
    uint8_t p1;
    uint8_t p2;
    uint8_t p3;
} fp_reply_t;

typedef enum {
    FP_POLL_IDLE = 0,     /* nothing pending */
    FP_POLL_WAITING,      /* command sent, no complete frame yet */
    FP_POLL_READY,        /* frame received, see fp_last_reply() */
    FP_POLL_BAD,          /* checksum or framing error */
} fp_poll_t;

void fp_init(void);
void fp_enable(bool on);
void fp_drain(void);

/* Blocking request/response. Returns false on timeout or bad checksum. */
bool fp_command(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3,
                fp_reply_t *out, uint32_t timeout_ms);

/* Non-blocking: send, then call fp_poll() from the main loop so the UI
 * stays responsive while the sensor waits for a finger. */
void      fp_send(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3);
fp_poll_t fp_poll(fp_reply_t *out);
void      fp_cancel(void);

/* Convenience wrappers */
bool fp_user_count(uint16_t *count);
bool fp_delete_all(void);
bool fp_delete_user(uint16_t id);

/* 1:N search. On a match, P3 holds the privilege (1..3), not a status.
 * Returns true if the exchange succeeded; *matched says whether a finger
 * was recognised, *id is valid only when matched. */
bool fp_search(uint16_t *id, uint8_t *privilege, bool *matched,
               uint32_t timeout_ms);

const char *fp_ack_text(uint8_t ack);

#endif /* FP_H */
