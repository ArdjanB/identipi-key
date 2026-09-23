/*
 * fp.c -- fingerprint sensor driver, F5 8-byte protocol
 */
#include "fp.h"
#include "version.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"

#define FRAME_LEN 8

/* Incremental receive state, so the main loop can stay responsive while
 * the sensor sits waiting for a finger (which can take seconds). */
static uint8_t  rx_buf[FRAME_LEN];
static int      rx_len = 0;
static bool     pending = false;
static uint8_t  pending_cmd = 0;

void fp_init(void) {
    gpio_init(PIN_FP_EN);
    gpio_set_dir(PIN_FP_EN, GPIO_OUT);
    gpio_put(PIN_FP_EN, 0);            /* LOW = enabled */

    uart_init(FP_UART, FP_BAUD);
    gpio_set_function(PIN_FP_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_FP_RX, GPIO_FUNC_UART);
    uart_set_format(FP_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(FP_UART, false, false);
    uart_set_fifo_enabled(FP_UART, true);

    sleep_ms(300);                     /* module boot */
    fp_drain();
}

void fp_enable(bool on) {
    gpio_put(PIN_FP_EN, on ? 0 : 1);
    if (on) sleep_ms(300);
}

void fp_drain(void) {
    while (uart_is_readable(FP_UART)) (void)uart_getc(FP_UART);
    rx_len = 0;
    pending = false;
}

static uint8_t checksum(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3,
                        uint8_t p0) {
    return (uint8_t)(cmd ^ p1 ^ p2 ^ p3 ^ p0);
}

void fp_send(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3) {
    fp_drain();
    uint8_t frame[FRAME_LEN] = {
        0xF5, cmd, p1, p2, p3, 0x00, checksum(cmd, p1, p2, p3, 0x00), 0xF5
    };
    uart_write_blocking(FP_UART, frame, FRAME_LEN);
    pending = true;
    pending_cmd = cmd;
    rx_len = 0;
}

void fp_cancel(void) {
    pending = false;
    rx_len = 0;
}

fp_poll_t fp_poll(fp_reply_t *out) {
    if (!pending) return FP_POLL_IDLE;

    while (uart_is_readable(FP_UART)) {
        uint8_t b = (uint8_t)uart_getc(FP_UART);

        /* Resync: a frame always begins with 0xF5 */
        if (rx_len == 0 && b != 0xF5) continue;

        rx_buf[rx_len++] = b;

        if (rx_len == FRAME_LEN) {
            rx_len = 0;
            if (rx_buf[7] != 0xF5) continue;    /* bad tail, keep looking */

            uint8_t want = checksum(rx_buf[1], rx_buf[2], rx_buf[3],
                                    rx_buf[4], rx_buf[5]);
            if (want != rx_buf[6]) {
                pending = false;
                return FP_POLL_BAD;
            }
            pending = false;
            if (out) {
                out->cmd = rx_buf[1];
                out->p1  = rx_buf[2];
                out->p2  = rx_buf[3];
                out->p3  = rx_buf[4];
            }
            return FP_POLL_READY;
        }
    }
    return FP_POLL_WAITING;
}

bool fp_command(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3,
                fp_reply_t *out, uint32_t timeout_ms) {
    fp_send(cmd, p1, p2, p3);
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    while (!time_reached(deadline)) {
        fp_reply_t r;
        fp_poll_t st = fp_poll(&r);
        if (st == FP_POLL_READY) {
            if (r.cmd != cmd) return false;     /* reply to something else */
            if (out) *out = r;
            return true;
        }
        if (st == FP_POLL_BAD) return false;
        sleep_ms(2);
    }
    fp_cancel();
    return false;
}

bool fp_user_count(uint16_t *count) {
    fp_reply_t r;
    if (!fp_command(FP_CMD_USER_COUNT, 0, 0, 0, &r, 2000)) return false;
    if (r.p3 != FP_ACK_SUCCESS) return false;
    if (count) *count = (uint16_t)((r.p1 << 8) | r.p2);
    return true;
}

bool fp_delete_all(void) {
    fp_reply_t r;
    if (!fp_command(FP_CMD_DELETE_ALL, 0, 0, 0, &r, 5000)) return false;
    return r.p3 == FP_ACK_SUCCESS;
}

bool fp_delete_user(uint16_t id) {
    fp_reply_t r;
    if (!fp_command(FP_CMD_DELETE_USER, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF),
                    0, &r, 3000))
        return false;
    return r.p3 == FP_ACK_SUCCESS;
}

bool fp_search(uint16_t *id, uint8_t *privilege, bool *matched,
               uint32_t timeout_ms) {
    fp_reply_t r;
    if (matched) *matched = false;
    if (!fp_command(FP_CMD_COMPARE_1_N, 0, 0, 0, &r, timeout_ms)) return false;

    /* On this protocol P3 carries the PRIVILEGE (1..3) when a finger is
     * recognised -- it is not a status byte in the success case. SB's own
     * demo treats non-zero P3 as an error and so reports matches as
     * failures. */
    if (r.p3 >= 1 && r.p3 <= 3) {
        if (id) *id = (uint16_t)((r.p1 << 8) | r.p2);
        if (privilege) *privilege = r.p3;
        if (matched) *matched = true;
        return true;
    }
    /* No match, or the module timed out waiting for a finger. */
    return true;
}

const char *fp_ack_text(uint8_t ack) {
    switch (ack) {
        case FP_ACK_SUCCESS:  return "success";
        case FP_ACK_FAIL:     return "failed";
        case FP_ACK_FULL:     return "library full";
        case FP_ACK_NO_USER:  return "no such user";
        case FP_ACK_OCCUPIED: return "ID occupied";
        case FP_ACK_EXIST:    return "already enrolled";
        case FP_ACK_TIMEOUT:  return "timeout";
        default:              return "unknown";
    }
}
