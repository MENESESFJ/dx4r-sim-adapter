/*
 * RX2JOY — firmware gen2
 *
 * Radio -> receptor (PWM / S.BUS / i-BUS) -> RP2040 -> USB HID -> VRC Pro
 *
 * Decisiones de diseño heredadas del v1 y que se mantienen:
 *  - Captura de PWM por PIO, 20 ns de resolucion. Sin micros(), sin
 *    attachInterrupt().
 *  - Sin filtro digital. Ninguno. Si el jitter lo exige, se agrega el
 *    minimo indispensable (ver DEADBAND_TICKS en rx2joy.h).
 *  - HID de 16 bits con signo, para que el escalado no agregue
 *    cuantizacion propia.
 *  - bInterval = 1 ms en el descriptor HID.
 *  - Puerto CDC para instrumentacion.
 *
 * Nuevo en gen2:
 *  - Tres protocolos de entrada, elegidos por jumper en GP14.
 *  - Timeout de enlace calculado sobre el periodo real de trama en vez
 *    de un valor fijo.
 *  - Failsafe detectado por el bit de la trama S.BUS, no solo por
 *    ausencia de señal.
 *  - Calibracion guardable en flash, una por modo.
 *
 * Botón CAL (GP15):
 *  - pulsacion corta  -> recalibrar
 *  - pulsacion larga  -> guardar la calibracion actual en flash
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "bsp/board.h"
#include "tusb.h"

#include "rx2joy.h"
#include "shared_state.h"
#include "display.h"

/* ------------------------------------------------------------------ */

#define CAL_DEBOUNCE_MS   30u
#define CAL_LONG_MS       1500u
#define FLASH_MSG_MS      2500u

#define HID_REPORT_LEN    5     /* 2 ejes int16 + 1 byte de botones */
#define HID_HEARTBEAT_MS  20u

static rx_mode_t s_mode        = MODE_PWM;
static bool      dump_enabled  = false;
static uint32_t  g_hid_count   = 0;

static char      s_flash_msg[10] = { 0 };
static uint32_t  s_flash_until   = 0;

static void set_flash_msg(const char *m) {
    snprintf(s_flash_msg, sizeof(s_flash_msg), "%s", m);
    s_flash_until = board_millis() + FLASH_MSG_MS;
}

/* ------------------------------------------------------------------ */
/* Reporte HID                                                         */
/* ------------------------------------------------------------------ */

static void build_report(uint8_t *rep) {
    int16_t axes[CH_COUNT];
    for (uint32_t i = 0; i < CH_COUNT; i++) axes[i] = channel_axis(i);
    memcpy(rep, axes, sizeof(axes));
    rep[4] = 0;   /* botones sin usar; reservados para CH3/CH4 */
}

static void send_hid(void) {
    static uint8_t  prev[HID_REPORT_LEN];
    static uint32_t last_tx_ms = 0;

    if (!tud_hid_ready()) return;

    uint8_t rep[HID_REPORT_LEN];
    build_report(rep);

    uint32_t now_ms = board_millis();
    bool changed = (memcmp(rep, prev, sizeof(rep)) != 0);

    /* Enviar apenas cambia algo, mas un latido periodico para que el
     * host nunca crea que el dispositivo se colgo. */
    if (changed || (uint32_t)(now_ms - last_tx_ms) >= HID_HEARTBEAT_MS) {
        tud_hid_report(0, rep, sizeof(rep));
        memcpy(prev, rep, sizeof(rep));
        last_tx_ms = now_ms;
        g_hid_count++;
    }
}

/* ------------------------------------------------------------------ */
/* Publicación hacia el display (core1)                                */
/* ------------------------------------------------------------------ */

static void publish_ui(void) {
    static uint32_t last_pub_ms = 0;
    static uint32_t last_hz_ms  = 0;
    static uint16_t hid_hz      = 0;

    uint32_t now_ms = board_millis();
    if ((uint32_t)(now_ms - last_pub_ms) < 20u) return;   /* 50 Hz */
    last_pub_ms = now_ms;

    if ((uint32_t)(now_ms - last_hz_ms) >= 1000u) {
        hid_hz      = (uint16_t)g_hid_count;
        g_hid_count = 0;
        last_hz_ms  = now_ms;
    }

    ui_publish_begin();
    for (uint32_t i = 0; i < CH_COUNT; i++) {
        const channel_t *ch = &chan[i];
        ui_state.axis[i]     = channel_axis(i);
        ui_state.ticks[i]    = ch->last_ticks;
        ui_state.lo[i]       = ch->lo;
        ui_state.ctr[i]      = ch->ctr;
        ui_state.hi[i]       = ch->hi;
        ui_state.frame_us[i] = ch->frame_us;
        ui_state.bad[i]      = ch->bad_count;
        ui_state.linked[i]   = ch->linked;
        ui_state.centered[i] = ch->centered;
    }
    ui_state.usb_mounted   = tud_mounted();
    ui_state.usb_suspended = tud_suspended();
    ui_state.hid_hz        = hid_hz;
    ui_state.mode          = (uint8_t)s_mode;
    ui_state.failsafe      = inputs_failsafe();
    ui_state.frame_lost    = inputs_frame_lost();
    ui_state.frames_bad    = inputs_frames_bad();

    memcpy(ui_state.flash_msg, s_flash_msg, sizeof(ui_state.flash_msg));
    ui_state.flash_until_ms = s_flash_until;
    ui_publish_end();
}

/* ------------------------------------------------------------------ */
/* Consola CDC                                                         */
/* ------------------------------------------------------------------ */

/* Formateo de ticks a microsegundos sin coma flotante.
 *
 * El v1 usaba %.1f en print_status. En newlib-nano el soporte de
 * coma flotante en printf viene desactivado salvo que se pida
 * explicitamente, asi que ese formato podia imprimir basura. El
 * display ya evitaba %f por este mismo motivo; aca faltaba. */
static void fmt_us(char *out, size_t n, uint32_t ticks) {
    uint32_t d = ticks / (TICKS_PER_US / 10u);   /* decimas de us */
    snprintf(out, n, "%lu.%lu", (unsigned long)(d / 10u),
                                (unsigned long)(d % 10u));
}

static void cdc_puts(const char *s) {
    tud_cdc_write_str(s);
    tud_cdc_write_flush();
}

static void print_status(void) {
    char buf[200], a[12], b[12], c[12];

    int n = snprintf(buf, sizeof(buf),
        "# modo=%s  enlace=%d  failsafe=%d  tramas ok=%lu malas=%lu\n",
        mode_name(s_mode), (int)channels_link_ok(), (int)inputs_failsafe(),
        (unsigned long)inputs_frames_ok(), (unsigned long)inputs_frames_bad());
    if (n > 0) tud_cdc_write(buf, (uint32_t)n);

    for (uint32_t i = 0; i < CH_COUNT; i++) {
        const channel_t *ch = &chan[i];
        fmt_us(a, sizeof(a), ch->lo);
        fmt_us(b, sizeof(b), ch->ctr);
        fmt_us(c, sizeof(c), ch->hi);

        n = snprintf(buf, sizeof(buf),
            "CH%lu link=%d cal=%d  lo=%lu ctr=%lu hi=%lu ticks"
            "  (%s/%s/%s us)  frame=%lu us  malos=%lu\n",
            (unsigned long)(i + 1), (int)ch->linked, (int)ch->centered,
            (unsigned long)ch->lo, (unsigned long)ch->ctr,
            (unsigned long)ch->hi, a, b, c,
            (unsigned long)ch->frame_us, (unsigned long)ch->bad_count);
        if (n > 0) tud_cdc_write(buf, (uint32_t)n);
    }

    if (s_mode == MODE_SBUS) {
        n = snprintf(buf, sizeof(buf), "# sbus byte de cierre = 0x%02X\n",
                     inputs_end_byte());
        if (n > 0) tud_cdc_write(buf, (uint32_t)n);
    }
    tud_cdc_write_flush();
}

static void do_save(void) {
    if (!channels_all_centered()) {
        cdc_puts("# nada que guardar: falta calibrar\n");
        set_flash_msg("SIN CAL");
        return;
    }
    bool ok = calstore_save(s_mode);
    cdc_puts(ok ? "# calibracion guardada en flash\n"
                : "# ERROR al guardar en flash\n");
    set_flash_msg(ok ? "GUARDADO" : "ERROR");
}

static void poll_cdc(void) {
    if (!tud_cdc_available()) return;

    int c = tud_cdc_read_char();
    switch (c) {
    case 'd':
        dump_enabled = !dump_enabled;
        cdc_puts(dump_enabled ? "# dump ON  (canal,ticks,frame_us)\n"
                              : "# dump OFF\n");
        break;
    case 'c':
        channels_reset_cal();
        cdc_puts("# recalibrando: deja los controles en neutro\n");
        set_flash_msg("RECAL");
        break;
    case 's':
        print_status();
        break;
    case 'w':
        do_save();
        break;
    case 'e':
        cdc_puts(calstore_erase() ? "# flash de calibracion borrada\n"
                                  : "# ERROR al borrar\n");
        set_flash_msg("BORRADA");
        break;
    case '?':
        cdc_puts("# d=dump  c=recalibrar  s=estado  w=guardar  e=borrar\n");
        break;
    default:
        break;
    }
}

static void dump_channels(void) {
    static uint32_t prev_ticks[CH_COUNT];
    if (!dump_enabled || !tud_cdc_connected()) return;

    bool any = false;
    for (uint32_t i = 0; i < CH_COUNT; i++) {
        if (chan[i].last_ticks == prev_ticks[i]) continue;
        prev_ticks[i] = chan[i].last_ticks;

        char line[48];
        int n = snprintf(line, sizeof(line), "%lu,%lu,%lu\n",
                         (unsigned long)i,
                         (unsigned long)chan[i].last_ticks,
                         (unsigned long)chan[i].frame_us);
        if (n > 0) tud_cdc_write(line, (uint32_t)n);
        any = true;
    }
    if (any) tud_cdc_write_flush();
}

/* ------------------------------------------------------------------ */
/* Botón físico CAL                                                    */
/* ------------------------------------------------------------------ */

/*
 * Corta: recalibrar. Larga: guardar en flash.
 *
 * La accion larga se dispara al cumplirse el tiempo, con el boton aun
 * apretado, y no al soltarlo. Asi el usuario recibe la confirmacion en
 * el OLED mientras sigue presionando y sabe que ya puede soltar, en vez
 * de tener que adivinar cuanto es "largo".
 */
static void poll_cal_button(void) {
    static bool     stable   = false;   /* estado filtrado: true = apretado */
    static bool     raw_prev = true;    /* true = suelto (pull-up)          */
    static uint32_t edge_ms  = 0;
    static uint32_t down_ms  = 0;
    static bool     long_done = false;

    bool     raw    = gpio_get(PIN_CAL_BUTTON);   /* true = suelto */
    uint32_t now_ms = board_millis();

    if (raw != raw_prev) {
        edge_ms  = now_ms;
        raw_prev = raw;
    }

    if ((uint32_t)(now_ms - edge_ms) < CAL_DEBOUNCE_MS) return;

    bool pressed = !raw;

    if (pressed && !stable) {            /* flanco de bajada filtrado */
        down_ms   = now_ms;
        long_done = false;
    } else if (pressed && !long_done &&
               (uint32_t)(now_ms - down_ms) >= CAL_LONG_MS) {
        do_save();
        long_done = true;
    } else if (!pressed && stable && !long_done) {
        channels_reset_cal();
        set_flash_msg("RECAL");
    }

    stable = pressed;
}

/* ------------------------------------------------------------------ */

int main(void) {
    board_init();

    gpio_init(PIN_CAL_BUTTON);
    gpio_set_dir(PIN_CAL_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_CAL_BUTTON);

    /* El modo se resuelve antes de tocar ningun periferico: de el
     * depende si se arma el PIO o la UART, y no tiene sentido dejar
     * ambos configurados compitiendo por pines. */
    s_mode = mode_read();

    channels_init(s_mode);
    inputs_init(s_mode);

    /* Si hay calibracion guardada para este modo, se usa de entrada y
     * el adaptador queda operativo sin pasar por el neutro. */
    calstore_load(s_mode);

    tusb_init();

    /* El OLED entero vive en core1. Core0 no lo toca nunca. */
    multicore_launch_core1(display_core1_main);

    while (true) {
        tud_task();
        poll_cdc();
        poll_cal_button();
        inputs_poll(time_us_32());
        dump_channels();
        send_hid();
        publish_ui();
    }
}

/* ------------------------------------------------------------------ */
/* Callbacks HID exigidos por TinyUSB                                  */
/* ------------------------------------------------------------------ */

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type;
    if (reqlen < HID_REPORT_LEN) return 0;
    build_report(buffer);
    return HID_REPORT_LEN;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)bufsize;
}
