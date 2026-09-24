/*
 * display.c — todo lo del OLED, ejecutandose en core1.
 *
 * Core0 no llama nada de este archivo. La unica interaccion es el
 * seqlock de shared_state.h, que core0 escribe a 50 Hz sin bloquearse.
 *
 * Si el display no esta conectado, core1 detecta el NACK en el init y
 * se queda dormido. El adaptador funciona igual: el OLED es opcional.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"

#include "ssd1306.h"
#include "shared_state.h"
#include "display.h"

/* --- pines --- */
#define I2C_PORT      i2c0
#define PIN_SDA       4
#define PIN_SCL       5
#define I2C_HZ        400000
#define OLED_ADDR     0x3C

/* Divisor 10k/10k desde el riel de 5 V del receptor hacia GPIO26. */
#define ADC_RAIL_CH   0
#define PIN_ADC_RAIL  26
#define RAIL_DIV_NUM  2       /* factor del divisor */

#define REFRESH_MS    50      /* 20 Hz: sobra para leer y no satura I2C */

#define TICKS_PER_US  50u

/* --- layout --- */
#define BAR_X   18
#define BAR_W   108
#define BAR_H   8
#define ST_BAR_Y 9
#define TH_BAR_Y 29

static bool oled_ok = false;

/* ------------------------------------------------------------------ */

static uint16_t read_rail_mv(void) {
    adc_select_input(ADC_RAIL_CH);
    uint32_t raw = adc_read();
    /* 12 bits sobre 3,3 V, multiplicado por el divisor */
    return (uint16_t)((raw * 3300u * RAIL_DIV_NUM) / 4095u);
}

static int read_chip_temp_c(void) {
    adc_select_input(4);
    uint32_t raw = adc_read();
    /* T = 27 - (V - 0.706)/0.001721, en milivolts y aritmetica entera */
    int mv = (int)((raw * 3300u) / 4095u);
    return 27 - ((mv - 706) * 1000) / 1721;
}

/* Dibuja una barra bipolar con marca de centro. */
static void draw_bar(int y, int16_t v, bool live) {
    ssd1306_rect(BAR_X, y, BAR_W, BAR_H, true);

    int cx    = BAR_X + BAR_W / 2;
    int halfw = BAR_W / 2 - 2;

    /* marca de centro */
    ssd1306_vline(cx, y - 2, 2, true);

    if (!live) return;

    int len = (int)(((int32_t)v * halfw) / 32767);
    if (len > halfw)  len = halfw;
    if (len < -halfw) len = -halfw;

    if (len >= 0) ssd1306_fill_rect(cx, y + 2, len + 1, BAR_H - 4, true);
    else          ssd1306_fill_rect(cx + len, y + 2, -len + 1, BAR_H - 4, true);
}

/* ticks -> "1483.2US" sin usar %f, que en newlib-nano puede no estar. */
static void fmt_us(char *out, size_t n, uint32_t ticks) {
    uint32_t d = ticks / (TICKS_PER_US / 10);   /* decimas de microsegundo */
    snprintf(out, n, "%lu.%luUS", (unsigned long)(d / 10), (unsigned long)(d % 10));
}

static void render(const ui_state_t *s) {
    char line[32], us[16];

    ssd1306_clear();

    /* --- encabezado --- */
    /* AND, no OR: alineado con build_report() en main.c. Con OR, el
     * display podia seguir diciendo "LINK" con throttle en failsafe
     * mientras el HID ya mandaba ceros -- la pantalla mentia justo
     * cuando mas importaba que no mintiera. */
    bool link = s->linked[0] && s->linked[1];
    if (!link) {
        snprintf(line, sizeof(line), "DX4R PRO   SIN ENLACE");
    } else {
        uint32_t f = s->frame_us[0] ? s->frame_us[0] : s->frame_us[1];
        snprintf(line, sizeof(line), "DX4R PRO  LINK %lu.%luMS",
                 (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    }
    ssd1306_text(0, 0, line);

    /* --- steering --- */
    ssd1306_text(0, ST_BAR_Y, "ST");
    draw_bar(ST_BAR_Y, s->axis[0], s->linked[0] && s->centered[0]);
    fmt_us(us, sizeof(us), s->ticks[0]);
    snprintf(line, sizeof(line), "%-9s %+6d", us, s->axis[0]);
    ssd1306_text(BAR_X, ST_BAR_Y + 10, line);

    /* --- throttle --- */
    ssd1306_text(0, TH_BAR_Y, "TH");
    draw_bar(TH_BAR_Y, s->axis[1], s->linked[1] && s->centered[1]);
    fmt_us(us, sizeof(us), s->ticks[1]);
    snprintf(line, sizeof(line), "%-9s %+6d", us, s->axis[1]);
    ssd1306_text(BAR_X, TH_BAR_Y + 10, line);

    /* --- separador --- */
    ssd1306_hline(0, 48, OLED_W, true);

    /* --- estado --- */
    uint16_t mv = read_rail_mv();
    int      tc = read_chip_temp_c();

    char volts[10];
    if (mv < 500) {
        /* sin divisor cableado: no inventamos un numero */
        snprintf(volts, sizeof(volts), "--.--V");
    } else {
        snprintf(volts, sizeof(volts), "%u.%02uV", mv / 1000, (mv % 1000) / 10);
    }

    const char *usb = s->usb_suspended ? "SUSP"
                    : s->usb_mounted   ? "USB"
                                       : "----";

    /* Solo entran 21 caracteres. Si falta calibrar, eso es mas urgente
     * que el ritmo de reportes, asi que ocupa su lugar. */
    char ultimo[10];
    if (!(s->centered[0] && s->centered[1]))
        snprintf(ultimo, sizeof(ultimo), "CAL?");
    else
        snprintf(ultimo, sizeof(ultimo), "%uHZ", (unsigned)s->hid_hz);

    snprintf(line, sizeof(line), "%s %dC %s %s", volts, tc, usb, ultimo);
    ssd1306_text(0, 53, line);

    ssd1306_show();
}

/* ------------------------------------------------------------------ */

void display_core1_main(void) {
    i2c_init(I2C_PORT, I2C_HZ);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);

    adc_init();
    adc_gpio_init(PIN_ADC_RAIL);
    adc_set_temp_sensor_enabled(true);

    oled_ok = ssd1306_init(I2C_PORT, OLED_ADDR);

    if (!oled_ok) {
        /* Sin display: core1 no tiene nada que hacer. Que duerma y no
         * consuma bus ni corriente. El adaptador sigue operativo. */
        while (true) sleep_ms(1000);
    }

    ui_state_t snap;
    while (true) {
        ui_snapshot(&snap);
        render(&snap);
        sleep_ms(REFRESH_MS);
    }
}
