/*
 * ssd1306.c — driver minimo para OLED 128x64 por I2C.
 *
 * Un frame completo son 1025 bytes. A 400 kHz eso son 23 ms, que es
 * cuatro veces el frame del SR2000. Por eso todo esto vive en core1
 * y nunca toca el camino receptor -> HID.
 */

#include <string.h>
#include "ssd1306.h"
#include "font5x7.h"

static i2c_inst_t *s_i2c;
static uint8_t     s_addr;

/* El byte 0 se reserva para el codigo de control 0x40, asi que el frame
 * entero sale en una sola transferencia I2C sin copias intermedias. */
static uint8_t s_buf[1 + OLED_FB_SIZE];
#define FB (s_buf + 1)

static bool cmd(uint8_t c) {
    uint8_t b[2] = { 0x00, c };
    return i2c_write_blocking(s_i2c, s_addr, b, 2, false) == 2;
}

bool ssd1306_init(i2c_inst_t *i2c, uint8_t addr) {
    s_i2c  = i2c;
    s_addr = addr;

    static const uint8_t seq[] = {
        0xAE,               /* display off              */
        0xD5, 0x80,         /* clock divide             */
        0xA8, 0x3F,         /* multiplex = 64            */
        0xD3, 0x00,         /* display offset            */
        0x40,               /* start line 0              */
        0x8D, 0x14,         /* charge pump ON            */
        0x20, 0x00,         /* memoria horizontal        */
        0xA1,               /* segment remap             */
        0xC8,               /* COM scan descendente      */
        0xDA, 0x12,         /* COM pins                  */
        0x81, 0xCF,         /* contraste                 */
        0xD9, 0xF1,         /* precharge                 */
        0xDB, 0x40,         /* VCOMH                     */
        0xA4,               /* seguir el RAM             */
        0xA6,               /* normal, no invertido      */
        0xAF                /* display on                */
    };

    for (size_t i = 0; i < sizeof(seq); i++) {
        if (!cmd(seq[i])) return false;   /* display ausente o mal cableado */
    }

    s_buf[0] = 0x40;
    ssd1306_clear();
    ssd1306_show();
    return true;
}

void ssd1306_clear(void) {
    memset(FB, 0, OLED_FB_SIZE);
}

void ssd1306_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint8_t *p = &FB[(y / 8) * OLED_W + x];
    uint8_t  m = (uint8_t)(1u << (y & 7));
    if (on) *p |= m; else *p &= (uint8_t)~m;
}

void ssd1306_hline(int x, int y, int w, bool on) {
    for (int i = 0; i < w; i++) ssd1306_pixel(x + i, y, on);
}

void ssd1306_vline(int x, int y, int h, bool on) {
    for (int i = 0; i < h; i++) ssd1306_pixel(x, y + i, on);
}

void ssd1306_rect(int x, int y, int w, int h, bool on) {
    if (w <= 0 || h <= 0) return;
    ssd1306_hline(x, y, w, on);
    ssd1306_hline(x, y + h - 1, w, on);
    ssd1306_vline(x, y, h, on);
    ssd1306_vline(x + w - 1, y, h, on);
}

void ssd1306_fill_rect(int x, int y, int w, int h, bool on) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ssd1306_pixel(x + i, y + j, on);
}

static void draw_char(int x, int y, char c) {
    /* minusculas al alfabeto de mayusculas: la tabla no las trae */
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if ((uint8_t)c < FONT_FIRST_CHAR || (uint8_t)c > FONT_LAST_CHAR) c = '?';

    const uint8_t *g = font5x7[(uint8_t)c - FONT_FIRST_CHAR];
    for (int col = 0; col < FONT_WIDTH; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < FONT_HEIGHT; row++) {
            if (bits & (1u << row)) ssd1306_pixel(x + col, y + row, true);
        }
    }
}

void ssd1306_text(int x, int y, const char *s) {
    while (*s) {
        draw_char(x, y, *s++);
        x += FONT_WIDTH + 1;
        if (x > OLED_W) break;
    }
}

void ssd1306_show(void) {
    cmd(0x21); cmd(0x00); cmd(OLED_W - 1);      /* rango de columnas */
    cmd(0x22); cmd(0x00); cmd(OLED_PAGES - 1);  /* rango de paginas  */
    i2c_write_blocking(s_i2c, s_addr, s_buf, sizeof(s_buf), false);
}
