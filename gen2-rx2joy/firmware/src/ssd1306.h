#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define OLED_W       128
#define OLED_H       64
#define OLED_PAGES   (OLED_H / 8)
#define OLED_FB_SIZE (OLED_W * OLED_PAGES)

/* Todo esto corre en core1. Nada de aca se llama desde core0. */

bool ssd1306_init(i2c_inst_t *i2c, uint8_t addr);
void ssd1306_clear(void);
void ssd1306_pixel(int x, int y, bool on);
void ssd1306_hline(int x, int y, int w, bool on);
void ssd1306_vline(int x, int y, int h, bool on);
void ssd1306_rect(int x, int y, int w, int h, bool on);
void ssd1306_fill_rect(int x, int y, int w, int h, bool on);
void ssd1306_text(int x, int y, const char *s);
void ssd1306_show(void);

#endif
