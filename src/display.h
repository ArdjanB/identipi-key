/*
 * display.h -- ST7789 240x135 driver with an in-RAM framebuffer
 *
 * Panel geometry was determined empirically during bring-up:
 * MADCTL 0x60 (landscape), window origin x=40 y=53. The 1.14" panel is
 * a window inside the controller's 240x320 GRAM, so those offsets are
 * mandatory -- without them you address unwritten memory and get noise.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#define LCD_W 240
#define LCD_H 135

/*
 * Colours are stored in WIRE order (big-endian RGB565), because that is
 * what the ST7789 expects and it lets us blast the framebuffer out with
 * a single SPI write. Always build them with rgb565().
 */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));   /* byte-swap for the wire */
}

#define COL_BLACK   rgb565(0, 0, 0)
#define COL_WHITE   rgb565(255, 255, 255)
#define COL_GREY    rgb565(128, 128, 128)
#define COL_DIM     rgb565(70, 70, 70)
#define COL_RED     rgb565(255, 40, 40)
#define COL_GREEN   rgb565(0, 220, 80)
#define COL_BLUE    rgb565(0, 90, 255)
#define COL_YELLOW  rgb565(255, 200, 0)
#define COL_CYAN    rgb565(0, 200, 220)

#define TEXT_NO_BG (-1)     /* pass as bg to draw text transparently */

void display_init(void);
void display_flush(void);                    /* push framebuffer to panel */
void display_backlight(bool on);

void display_clear(uint16_t colour);
void display_pixel(int x, int y, uint16_t colour);
void display_fill_rect(int x, int y, int w, int h, uint16_t colour);
void display_hline(int x, int y, int w, uint16_t colour);
void display_vline(int x, int y, int h, uint16_t colour);
void display_rect(int x, int y, int w, int h, uint16_t colour);

/* scale 1 => 6x9 px per character, scale 2 => 12x18, etc.
 * bg may be TEXT_NO_BG to leave existing pixels alone. */
void display_char(int x, int y, char c, uint16_t fg, int32_t bg, int scale);
void display_text(int x, int y, const char *s, uint16_t fg, int32_t bg,
                  int scale);
int  display_text_width(const char *s, int scale);
void display_text_centred(int y, const char *s, uint16_t fg, int32_t bg,
                          int scale);

#endif /* DISPLAY_H */
