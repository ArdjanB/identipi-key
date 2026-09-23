/*
 * version.h -- firmware version and hardware pin map
 *
 * Shown on the Info screen and the splash screen. FIRMWARE_VERSION's
 * patch digit and FIRMWARE_BUILD_TIME are stamped automatically by
 * tools/bump_version.py on every build (see CMakeLists.txt) -- don't
 * hand-edit them, they'll just get overwritten by the next build.
 * FIRMWARE_DATE is the milestone/release date and stays manual.
 */
#ifndef VERSION_H
#define VERSION_H

#define FIRMWARE_VERSION "1.0.4"
#define FIRMWARE_DATE    "2026-09-22"
#define FIRMWARE_BUILD_TIME "2026-09-23 19:51:30"
#define FIRMWARE_NAME    "IdentiPi Key"

/* ------------------------------------------------------------------
 * Pin map -- VERIFIED ON HARDWARE 2026-08-15.
 * Note GP14/GP15: SB Components' published pinout has these swapped.
 * Their table claims GP14=UP, GP15=RIGHT. It is the other way around.
 * ------------------------------------------------------------------ */

/* Display: ST7789 240x135 on SPI1 */
#define PIN_LCD_SCK   10
#define PIN_LCD_MOSI  11
#define PIN_LCD_CS     9
#define PIN_LCD_DC     8
#define PIN_LCD_RST   12
#define PIN_LCD_BL    13
#define LCD_SPI       spi1

/* Fingerprint sensor: F5 protocol, UART0 @ 19200 8N1 */
#define PIN_FP_TX      0   /* Pico -> sensor RX */
#define PIN_FP_RX      1   /* Pico <- sensor TX */
#define PIN_FP_EN      3   /* LOW = enabled (board pulls it low anyway) */
#define FP_UART       uart0
#define FP_BAUD       19200

/* Joystick, all active-low with pull-ups */
#define PIN_JOY_UP    15
#define PIN_JOY_RIGHT 14
#define PIN_JOY_LEFT  16
#define PIN_JOY_DOWN  17
#define PIN_JOY_SEL   18

#endif /* VERSION_H */
