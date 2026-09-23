/*
 * keyboard.c -- on-screen character grid, milestone 3/4
 */
#include "keyboard.h"
#include "display.h"
#include "buttons.h"

#include <string.h>
#include <stdio.h>

#define COLS       10
#define CELL_W     22
#define CELL_H     15
#define GRID_X0     4
#define GRID_Y0    40

#define KEYBOARD_MAX_LEN 40   /* covers both DESC_LEN(24) and SECRET_LEN(33) */

typedef enum {
    CELL_BLANK = 0,
    CELL_CHAR,
    CELL_SHIFT,
    CELL_MODE,
    CELL_DEL,
    CELL_OK,
    CELL_CANCEL,
} cell_kind_t;

typedef struct {
    cell_kind_t kind;
    char        ch;      /* CELL_CHAR: lowercase base character */
    const char *label;   /* control cells: short on-screen label */
} cell_t;

#define ALPHA_ROWS 4
#define DIGIT_ROWS 4

static const cell_t alpha_grid[ALPHA_ROWS][COLS] = {
    {{CELL_CHAR,'a',0},{CELL_CHAR,'b',0},{CELL_CHAR,'c',0},{CELL_CHAR,'d',0},{CELL_CHAR,'e',0},
     {CELL_CHAR,'f',0},{CELL_CHAR,'g',0},{CELL_CHAR,'h',0},{CELL_CHAR,'i',0},{CELL_CHAR,'j',0}},
    {{CELL_CHAR,'k',0},{CELL_CHAR,'l',0},{CELL_CHAR,'m',0},{CELL_CHAR,'n',0},{CELL_CHAR,'o',0},
     {CELL_CHAR,'p',0},{CELL_CHAR,'q',0},{CELL_CHAR,'r',0},{CELL_CHAR,'s',0},{CELL_CHAR,'t',0}},
    {{CELL_CHAR,'u',0},{CELL_CHAR,'v',0},{CELL_CHAR,'w',0},{CELL_CHAR,'x',0},{CELL_CHAR,'y',0},
     {CELL_CHAR,'z',0},{CELL_CHAR,'.',0},{CELL_CHAR,',',0},{CELL_CHAR,'-',0},{CELL_CHAR,'_',0}},
    {{CELL_CHAR,'@',0},{CELL_SHIFT,0,"SH"},{CELL_MODE,0,"12"},{CELL_DEL,0,"<-"},
     {CELL_OK,0,"OK"},{CELL_CANCEL,0,"X"},
     {CELL_BLANK,0,0},{CELL_BLANK,0,0},{CELL_BLANK,0,0},{CELL_BLANK,0,0}},
};

/* Rows 1-2 are the 20 extra symbols. '#' and '\' are layout-dependent on
 * the typing side (see hid.c) but that's invisible here -- this grid just
 * needs a character to append to the buffer. */
static const cell_t digit_grid[DIGIT_ROWS][COLS] = {
    {{CELL_CHAR,'0',0},{CELL_CHAR,'1',0},{CELL_CHAR,'2',0},{CELL_CHAR,'3',0},{CELL_CHAR,'4',0},
     {CELL_CHAR,'5',0},{CELL_CHAR,'6',0},{CELL_CHAR,'7',0},{CELL_CHAR,'8',0},{CELL_CHAR,'9',0}},
    {{CELL_CHAR,'!',0},{CELL_CHAR,'$',0},{CELL_CHAR,'%',0},{CELL_CHAR,'&',0},{CELL_CHAR,'(',0},
     {CELL_CHAR,')',0},{CELL_CHAR,'@',0},{CELL_CHAR,'*',0},{CELL_CHAR,'-',0},{CELL_CHAR,'_',0}},
    {{CELL_CHAR,'<',0},{CELL_CHAR,'>',0},{CELL_CHAR,'?',0},{CELL_CHAR,'=',0},{CELL_CHAR,'#',0},
     {CELL_CHAR,':',0},{CELL_CHAR,';',0},{CELL_CHAR,'+',0},{CELL_CHAR,'/',0},{CELL_CHAR,'\\',0}},
    {{CELL_MODE,0,"AB"},{CELL_DEL,0,"<-"},{CELL_OK,0,"OK"},{CELL_CANCEL,0,"X"},
     {CELL_BLANK,0,0},{CELL_BLANK,0,0},{CELL_BLANK,0,0},{CELL_BLANK,0,0},
     {CELL_BLANK,0,0},{CELL_BLANK,0,0}},
};

static int row_len(const cell_t *row) {
    int n = 0;
    while (n < COLS && row[n].kind != CELL_BLANK) n++;
    return n;
}

static void draw_grid(const cell_t grid[][COLS], int rows, int cur_r, int cur_c,
                      bool shift_on) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < COLS; c++) {
            const cell_t *cell = &grid[r][c];
            if (cell->kind == CELL_BLANK) continue;

            int x = GRID_X0 + c * CELL_W;
            int y = GRID_Y0 + r * CELL_H;
            bool sel = (r == cur_r && c == cur_c);

            /* OK/CANCEL get a colour-coded border regardless of cursor
             * position -- they sit right next to each other, and on a
             * screen this small an off-by-one column is easy to miss. */
            if (sel) {
                display_fill_rect(x, y, CELL_W - 2, CELL_H - 2, COL_CYAN);
            } else if (cell->kind == CELL_OK) {
                display_rect(x, y, CELL_W - 2, CELL_H - 2, COL_GREEN);
            } else if (cell->kind == CELL_CANCEL) {
                display_rect(x, y, CELL_W - 2, CELL_H - 2, COL_RED);
            } else if (cell->kind != CELL_CHAR) {
                display_rect(x, y, CELL_W - 2, CELL_H - 2, COL_DIM);
            }

            uint16_t fg = sel ? COL_BLACK : COL_WHITE;
            if (cell->kind == CELL_CHAR) {
                char ch = cell->ch;
                if (shift_on && ch >= 'a' && ch <= 'z') ch = (char) (ch - 'a' + 'A');
                char text[2] = { ch, '\0' };
                display_text(x + 8, y + 3, text, fg, TEXT_NO_BG, 1);
            } else {
                uint16_t ctrl_fg;
                if (sel) ctrl_fg = COL_BLACK;
                else if (cell->kind == CELL_OK) ctrl_fg = COL_GREEN;
                else if (cell->kind == CELL_CANCEL) ctrl_fg = COL_RED;
                else if (cell->kind == CELL_SHIFT && shift_on) ctrl_fg = COL_YELLOW;
                else ctrl_fg = COL_GREY;
                display_text(x + 2, y + 3, cell->label, ctrl_fg, TEXT_NO_BG, 1);
            }
        }
    }
}

bool keyboard_edit(const char *title, char *buf, size_t max_len) {
    char work[KEYBOARD_MAX_LEN];
    size_t len = strlen(buf);
    if (len > max_len - 1) len = max_len - 1;
    if (len >= sizeof(work)) len = sizeof(work) - 1;
    memcpy(work, buf, len);
    work[len] = '\0';

    bool page_digit = false;
    bool shift_on   = false;
    int  cur_r = 0, cur_c = 0;

    while (true) {
        const cell_t (*grid)[COLS] = page_digit ? digit_grid : alpha_grid;
        int rows = page_digit ? DIGIT_ROWS : ALPHA_ROWS;

        display_clear(COL_BLACK);
        display_text(4, 2, title, COL_CYAN, TEXT_NO_BG, 1);

        char count[12];
        snprintf(count, sizeof(count), "%u/%u", (unsigned) len,
                 (unsigned) (max_len - 1));
        display_text(LCD_W - display_text_width(count, 1) - 4, 2, count,
                     COL_GREY, TEXT_NO_BG, 1);
        display_hline(0, 12, LCD_W, COL_DIM);

        /* Scale 2: the one thing on this screen worth reading at a
         * glance is what you've typed so far. */
        {
            int max_chars = LCD_W / 12;
            const char *show = work;
            if ((int) len > max_chars) show = work + (len - max_chars);
            display_text(2, 16, show, COL_WHITE, TEXT_NO_BG, 2);
        }
        display_hline(0, 36, LCD_W, COL_DIM);

        draw_grid(grid, rows, cur_r, cur_c, shift_on);
        display_flush();

        button_t b = buttons_wait_repeat(BTN_MASK_ANY, 0);
        int cur_row_len = row_len(grid[cur_r]);

        switch (b) {
        case BTN_LEFT:
            cur_c = (cur_c - 1 + cur_row_len) % cur_row_len;
            break;
        case BTN_RIGHT:
            cur_c = (cur_c + 1) % cur_row_len;
            break;
        case BTN_UP:
            cur_r = (cur_r - 1 + rows) % rows;
            {
                int nl = row_len(grid[cur_r]);
                if (cur_c >= nl) cur_c = nl - 1;
            }
            break;
        case BTN_DOWN:
            cur_r = (cur_r + 1) % rows;
            {
                int nl = row_len(grid[cur_r]);
                if (cur_c >= nl) cur_c = nl - 1;
            }
            break;
        case BTN_SEL: {
            const cell_t *cell = &grid[cur_r][cur_c];
            switch (cell->kind) {
            case CELL_CHAR: {
                char ch = cell->ch;
                if (shift_on && ch >= 'a' && ch <= 'z') ch = (char) (ch - 'a' + 'A');
                if (len < max_len - 1 && len < sizeof(work) - 1) {
                    work[len++] = ch;
                    work[len] = '\0';
                }
                break;
            }
            case CELL_SHIFT:
                shift_on = !shift_on;
                break;
            case CELL_MODE:
                page_digit = !page_digit;
                cur_r = 0;
                cur_c = 0;
                break;
            case CELL_DEL:
                if (len > 0) work[--len] = '\0';
                break;
            case CELL_OK:
                memcpy(buf, work, len + 1);
                return true;
            case CELL_CANCEL:
                return false;
            default:
                break;
            }
            break;
        }
        default:
            break;
        }
    }
}
