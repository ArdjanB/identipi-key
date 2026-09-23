/*
 * display.c -- ST7789 driver
 */
#include "display.h"
#include "version.h"
#include "font6x9.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <string.h>

#define LCD_X_OFF 40      /* window origin inside the 240x320 GRAM */
#define LCD_Y_OFF 53

/* Framebuffer holds wire-order RGB565, so it can be written straight out. */
static uint16_t fb[LCD_W * LCD_H];

static void cs(bool low)  { gpio_put(PIN_LCD_CS, low ? 0 : 1); }
static void dc(bool data) { gpio_put(PIN_LCD_DC, data ? 1 : 0); }

static void lcd_cmd(uint8_t c, const uint8_t *data, size_t len) {
    cs(true);
    dc(false);
    spi_write_blocking(LCD_SPI, &c, 1);
    if (data && len) {
        dc(true);
        spi_write_blocking(LCD_SPI, data, len);
    }
    cs(false);
}

void display_backlight(bool on) {
    gpio_put(PIN_LCD_BL, on ? 1 : 0);
}

void display_init(void) {
    spi_init(LCD_SPI, 32 * 1000 * 1000);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_LCD_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);

    /* CS, DC, RST, BL are plain outputs. Unlike the MicroPython prototype
     * there is no risk of the SPI driver quietly claiming D/C here --
     * pin muxing is explicit. */
    gpio_init(PIN_LCD_CS);  gpio_set_dir(PIN_LCD_CS,  GPIO_OUT); gpio_put(PIN_LCD_CS, 1);
    gpio_init(PIN_LCD_DC);  gpio_set_dir(PIN_LCD_DC,  GPIO_OUT); gpio_put(PIN_LCD_DC, 0);
    gpio_init(PIN_LCD_RST); gpio_set_dir(PIN_LCD_RST, GPIO_OUT); gpio_put(PIN_LCD_RST, 1);
    gpio_init(PIN_LCD_BL);  gpio_set_dir(PIN_LCD_BL,  GPIO_OUT); gpio_put(PIN_LCD_BL, 1);

    /* hardware reset */
    sleep_ms(10);
    gpio_put(PIN_LCD_RST, 0); sleep_ms(50);
    gpio_put(PIN_LCD_RST, 1); sleep_ms(150);

    lcd_cmd(0x01, NULL, 0); sleep_ms(150);          /* SWRESET */
    lcd_cmd(0x11, NULL, 0); sleep_ms(150);          /* SLPOUT  */

    uint8_t colmod = 0x55;                          /* 16 bits/pixel */
    lcd_cmd(0x3A, &colmod, 1);

    uint8_t madctl = 0x60;                          /* landscape */
    lcd_cmd(0x36, &madctl, 1);

    lcd_cmd(0x21, NULL, 0);                         /* INVON, IPS panel */
    lcd_cmd(0x13, NULL, 0);                         /* NORON */
    lcd_cmd(0x29, NULL, 0); sleep_ms(50);           /* DISPON */

    display_clear(COL_BLACK);
    display_flush();
}

void display_flush(void) {
    uint8_t buf[4];
    uint16_t x0 = LCD_X_OFF, x1 = LCD_X_OFF + LCD_W - 1;
    uint16_t y0 = LCD_Y_OFF, y1 = LCD_Y_OFF + LCD_H - 1;

    buf[0] = x0 >> 8; buf[1] = x0 & 0xFF; buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
    lcd_cmd(0x2A, buf, 4);                          /* CASET */

    buf[0] = y0 >> 8; buf[1] = y0 & 0xFF; buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
    lcd_cmd(0x2B, buf, 4);                          /* RASET */

    cs(true);
    dc(false);
    uint8_t ramwr = 0x2C;
    spi_write_blocking(LCD_SPI, &ramwr, 1);
    dc(true);
    spi_write_blocking(LCD_SPI, (const uint8_t *)fb, sizeof(fb));
    cs(false);
}

void display_clear(uint16_t colour) {
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = colour;
}

void display_pixel(int x, int y, uint16_t colour) {
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return;
    fb[y * LCD_W + x] = colour;
}

void display_fill_rect(int x, int y, int w, int h, uint16_t colour) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > LCD_W ? LCD_W : x + w;
    int y1 = y + h > LCD_H ? LCD_H : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            fb[yy * LCD_W + xx] = colour;
}

void display_hline(int x, int y, int w, uint16_t colour) {
    display_fill_rect(x, y, w, 1, colour);
}

void display_vline(int x, int y, int h, uint16_t colour) {
    display_fill_rect(x, y, 1, h, colour);
}

void display_rect(int x, int y, int w, int h, uint16_t colour) {
    display_hline(x, y, w, colour);
    display_hline(x, y + h - 1, w, colour);
    display_vline(x, y, h, colour);
    display_vline(x + w - 1, y, h, colour);
}

void display_char(int x, int y, char c, uint16_t fg, int32_t bg, int scale) {
    if ((unsigned char)c < FONT_FIRST || (unsigned char)c > FONT_LAST) c = '?';
    const uint8_t *glyph = font6x9[(unsigned char)c - FONT_FIRST];

    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            bool on = (bits >> (FONT_W - 1 - col)) & 1;   /* MSB = leftmost */
            if (on) {
                display_fill_rect(x + col * scale, y + row * scale,
                                  scale, scale, fg);
            } else if (bg >= 0) {
                display_fill_rect(x + col * scale, y + row * scale,
                                  scale, scale, (uint16_t)bg);
            }
        }
    }
}

void display_text(int x, int y, const char *s, uint16_t fg, int32_t bg,
                  int scale) {
    int cx = x;
    for (const char *p = s; *p; p++) {
        if (*p == '\n') { cx = x; y += (FONT_H + 1) * scale; continue; }
        display_char(cx, y, *p, fg, bg, scale);
        cx += FONT_W * scale;
    }
}

int display_text_width(const char *s, int scale) {
    return (int)strlen(s) * FONT_W * scale;
}

void display_text_centred(int y, const char *s, uint16_t fg, int32_t bg,
                          int scale) {
    int w = display_text_width(s, scale);
    display_text((LCD_W - w) / 2, y, s, fg, bg, scale);
}
