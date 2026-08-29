/*
 * DX4R PRO Sim Adapter — firmware V1
 *
 * Spektrum DX4R Pro -> SR2000 (5,5 ms, 2 canales) -> RP2040 -> USB HID -> VRC Pro
 *
 * Decisiones de diseño:
 *  - Captura por PIO, 20 ns de resolución. Sin micros(), sin attachInterrupt().
 *  - Sin filtro. Ninguno. Si despues de medir el jitter resulta necesario,
 *    se agrega el minimo indispensable (ver DEADBAND_TICKS).
 *  - HID de 16 bits con signo. El escalado no agrega cuantizacion propia,
 *    aunque la resolucion efectiva la ponga el enlace DSMR (~11 bits).
 *  - bInterval = 1 ms en el descriptor HID (ver usb_descriptors.c).
 *  - Calibracion automatica: el centro se captura cuando los controles
 *    estan quietos, y el rango se expande solo a medida que manejas.
 *    Recalibracion manual: boton fisico en GP15 (activo en bajo, pull-up
 *    interno) o tecla 'c' por consola CDC — ambos llaman al mismo punto.
 *  - Puerto CDC para instrumentacion: volcado de ticks crudos para
 *    medir la resolucion real del enlace (ver analiza_pulsos.py).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "bsp/board.h"
#include "tusb.h"

#include "pwm_capture.pio.h"
#include "shared_state.h"
#include "display.h"

/* ------------------------------------------------------------------ */
/* Configuración                                                       */
/* ------------------------------------------------------------------ */

#define PIN_CH1            2u          /* steering       */
#define PIN_CH2            3u          /* throttle/brake */

#define INVERT_CH1         false
#define INVERT_CH2         false

#define PIO_CLK_HZ         100000000.0f
#define TICKS_PER_US       50u         /* 2 ciclos PIO por iteracion */
#define US_TO_TICKS(us)    ((uint32_t)((us) * TICKS_PER_US))

/* Ventana de validacion: descarta glitches y pulsos imposibles */
#define PULSE_MIN_TICKS    US_TO_TICKS(800)
#define PULSE_MAX_TICKS    US_TO_TICKS(2200)

/* Calibracion de centro */
#define CENTER_SAMPLES     32u
#define CENTER_TOL_TICKS   US_TO_TICKS(20)
#define INITIAL_HALF_SPAN  US_TO_TICKS(400)

/* Perdida de enlace. A 5,5 ms esto son ~11 frames. */
#define LINK_TIMEOUT_US    60000u

/* Boton fisico de recalibracion. Activo en bajo, con pull-up interno:
 * no hace falta resistencia externa. */
#define PIN_CAL_BUTTON     15u
#define CAL_DEBOUNCE_MS    30u

/* Zona muerta alrededor del centro, en ticks. 0 = sin zona muerta.
 * Empezar en 0 y subir solo si el neutro no queda quieto en joy.cpl. */
#define DEADBAND_TICKS     0u

#define CH_COUNT           2u

/* ------------------------------------------------------------------ */
/* Estado por canal                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    uint     pin;
    uint     sm;
    bool     invert;

    /* captura */
    uint32_t last_ticks;
    uint32_t last_sample_us;
    uint32_t prev_sample_us;
    uint32_t frame_us;          /* periodo entre frames medido */
    bool     linked;

    /* calibracion */
    bool     centered;
    uint32_t ctr, lo, hi;
    uint32_t cal_acc;
    uint32_t cal_ref;
    uint32_t cal_n;

    /* estadistica */
    uint32_t bad_count;
} channel_t;

static channel_t chan[CH_COUNT];
static PIO       pio = pio0;

static bool     dump_enabled = false;
static uint32_t g_hid_count  = 0;   /* reportes enviados, se resetea cada 1 s */

/* ------------------------------------------------------------------ */

static inline uint32_t absdiff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static void channel_reset_cal(channel_t *ch) {
    ch->centered = false;
    ch->cal_acc  = 0;
    ch->cal_n    = 0;
    ch->cal_ref  = 0;
    ch->lo = ch->hi = ch->ctr = 0;
}

/* Punto unico de recalibracion: lo llaman tanto el boton fisico
 * como la tecla 'c' de la consola CDC, asi que se comportan igual. */
static void request_recalibration(void) {
    for (uint i = 0; i < CH_COUNT; i++) channel_reset_cal(&chan[i]);
}

static void channel_init(channel_t *ch, uint pin, uint sm, bool invert, uint offset) {
    memset(ch, 0, sizeof(*ch));
    ch->pin    = pin;
    ch->sm     = sm;
    ch->invert = invert;
    channel_reset_cal(ch);
    pwm_capture_program_init(pio, sm, offset, pin, PIO_CLK_HZ);
}

/* Escalado partido en dos tramos, para que el centro caiga exacto en 0
 * aunque el recorrido no sea simetrico (que en un gatillo nunca lo es). */
static int16_t scale_axis(const channel_t *ch, uint32_t t) {
    if (t >= ch->ctr) {
        uint32_t d    = t - ch->ctr;
        uint32_t span = ch->hi - ch->ctr;
        if (d <= DEADBAND_TICKS || span == 0) return 0;
        d -= DEADBAND_TICKS;
        if (span <= DEADBAND_TICKS) return 0;
        span -= DEADBAND_TICKS;
        int64_t v = ((int64_t)d * 32767) / span;
        return (int16_t)(v > 32767 ? 32767 : v);
    } else {
        uint32_t d    = ch->ctr - t;
        uint32_t span = ch->ctr - ch->lo;
        if (d <= DEADBAND_TICKS || span == 0) return 0;
        d -= DEADBAND_TICKS;
        if (span <= DEADBAND_TICKS) return 0;
        span -= DEADBAND_TICKS;
        int64_t v = ((int64_t)d * 32768) / span;
        return (int16_t)(-(v > 32768 ? 32768 : v));
    }
}

static void channel_feed(channel_t *ch, uint32_t t, uint32_t now) {
    if (t < PULSE_MIN_TICKS || t > PULSE_MAX_TICKS) {
        ch->bad_count++;
        return;
    }

    ch->prev_sample_us = ch->last_sample_us;
    ch->last_sample_us = now;
    ch->last_ticks     = t;
    if (ch->linked) {
        ch->frame_us = now - ch->prev_sample_us;
    }
    ch->linked = true;

    if (!ch->centered) {
        /* Buscamos CENTER_SAMPLES muestras seguidas dentro de una ventana
         * estrecha. Si el usuario mueve algo, se reinicia la cuenta. */
        if (ch->cal_n == 0) {
            ch->cal_ref = t;
            ch->cal_acc = t;
            ch->cal_n   = 1;
        } else if (absdiff(t, ch->cal_ref) <= CENTER_TOL_TICKS) {
            ch->cal_acc += t;
            ch->cal_n++;
            if (ch->cal_n >= CENTER_SAMPLES) {
                ch->ctr      = ch->cal_acc / ch->cal_n;
                ch->lo       = ch->ctr - INITIAL_HALF_SPAN;
                ch->hi       = ch->ctr + INITIAL_HALF_SPAN;
                ch->centered = true;
            }
        } else {
            ch->cal_acc = 0;
            ch->cal_n   = 0;
        }
    } else {
        /* El rango solo se expande. Mueve volante y gatillo a los topes
         * una vez y queda calibrado para la sesion. */
        if (t < ch->lo) ch->lo = t;
        if (t > ch->hi) ch->hi = t;
    }
}

static void poll_channels(void) {
    uint32_t now = time_us_32();

    for (uint i = 0; i < CH_COUNT; i++) {
        channel_t *ch = &chan[i];

        /* Drenar la FIFO completa: nos interesa la muestra mas nueva,
         * no ponernos al dia con una cola vieja. */
        while (!pio_sm_is_rx_fifo_empty(pio, ch->sm)) {
            uint32_t t = pio_sm_get(pio, ch->sm);
            channel_feed(ch, t, now);

            if (dump_enabled && tud_cdc_connected()) {
                char line[48];
                int n = snprintf(line, sizeof(line), "%u,%u,%u\n",
                                 (unsigned)i, (unsigned)t, (unsigned)ch->frame_us);
                if (n > 0) tud_cdc_write(line, (uint32_t)n);
            }
        }

        if (ch->linked && (uint32_t)(now - ch->last_sample_us) > LINK_TIMEOUT_US) {
            ch->linked = false;
        }
    }

    if (dump_enabled && tud_cdc_connected()) tud_cdc_write_flush();
}

/* ------------------------------------------------------------------ */
/* Reporte HID                                                         */
/* ------------------------------------------------------------------ */

#define HID_REPORT_LEN 5   /* 2 ejes int16 + 1 byte de botones */

static void build_report(uint8_t *rep) {
    int16_t axes[CH_COUNT];

    for (uint i = 0; i < CH_COUNT; i++) {
        channel_t *ch = &chan[i];
        int16_t v = 0;

        if (ch->linked && ch->centered) {
            v = scale_axis(ch, ch->last_ticks);
            if (ch->invert) v = (v == INT16_MIN) ? INT16_MAX : (int16_t)(-v);
        }
        axes[i] = v;
    }

    memcpy(rep, axes, sizeof(axes));
    rep[4] = 0;   /* botones sin usar; reservados para CH3/CH4 a 11 ms */
}

static void send_hid(void) {
    static uint8_t prev[HID_REPORT_LEN];
    static uint32_t last_tx_ms = 0;

    if (!tud_hid_ready()) return;

    uint8_t rep[HID_REPORT_LEN];
    build_report(rep);

    uint32_t now_ms = board_millis();
    bool changed = (memcmp(rep, prev, sizeof(rep)) != 0);

    /* Enviar apenas cambia algo, mas un latido cada 20 ms para que el
     * host nunca crea que el dispositivo se colgo. */
    if (changed || (now_ms - last_tx_ms) >= 20) {
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
    if ((now_ms - last_pub_ms) < 20) return;   /* 50 Hz basta para un OLED */
    last_pub_ms = now_ms;

    if ((now_ms - last_hz_ms) >= 1000) {
        hid_hz       = (uint16_t)g_hid_count;
        g_hid_count  = 0;
        last_hz_ms   = now_ms;
    }

    uint8_t rep[HID_REPORT_LEN];
    build_report(rep);

    ui_publish_begin();
    for (uint i = 0; i < CH_COUNT; i++) {
        channel_t *ch = &chan[i];
        memcpy(&ui_state.axis[i], &rep[i * 2], sizeof(int16_t));
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
    ui_publish_end();
}

/* ------------------------------------------------------------------ */
/* Consola CDC                                                         */
/* ------------------------------------------------------------------ */

static void print_status(void) {
    char buf[256];
    for (uint i = 0; i < CH_COUNT; i++) {
        channel_t *ch = &chan[i];
        int n = snprintf(buf, sizeof(buf),
            "CH%u link=%d cal=%d  lo=%u ctr=%u hi=%u ticks"
            "  (%.1f/%.1f/%.1f us)  frame=%u us  malos=%u\n",
            (unsigned)(i + 1), (int)ch->linked, (int)ch->centered,
            (unsigned)ch->lo, (unsigned)ch->ctr, (unsigned)ch->hi,
            ch->lo / (double)TICKS_PER_US,
            ch->ctr / (double)TICKS_PER_US,
            ch->hi / (double)TICKS_PER_US,
            (unsigned)ch->frame_us, (unsigned)ch->bad_count);
        if (n > 0) tud_cdc_write(buf, (uint32_t)n);
    }
    tud_cdc_write_flush();
}

static void poll_cdc(void) {
    if (!tud_cdc_available()) return;

    int c = tud_cdc_read_char();
    switch (c) {
    case 'd':
        dump_enabled = !dump_enabled;
        tud_cdc_write_str(dump_enabled ? "# dump ON  (canal,ticks,frame_us)\n"
                                       : "# dump OFF\n");
        tud_cdc_write_flush();
        break;
    case 'c':
        request_recalibration();
        tud_cdc_write_str("# recalibrando: deja los controles en neutro\n");
        tud_cdc_write_flush();
        break;
    case 's':
        print_status();
        break;
    case '?':
        tud_cdc_write_str("# d=dump  c=recalibrar  s=estado\n");
        tud_cdc_write_flush();
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Botón físico de recalibración                                       */
/* ------------------------------------------------------------------ */

/* Antirrebote simple por tiempo: un flanco de bajada valido dispara
 * la recalibracion una sola vez, y no vuelve a disparar hasta que el
 * boton se suelta y se estabiliza. Costo: una lectura de GPIO por
 * vuelta del bucle principal, nada comparable al camino PIO -> HID. */
static void poll_cal_button(void) {
    static bool     pressed_state = false;   /* filtrado */
    static bool     raw_prev      = true;    /* true = suelto (pull-up) */
    static uint32_t edge_ms       = 0;

    bool raw = gpio_get(PIN_CAL_BUTTON);      /* true = suelto */
    uint32_t now = board_millis();

    if (raw != raw_prev) {
        edge_ms  = now;
        raw_prev = raw;
    }

    if ((now - edge_ms) >= CAL_DEBOUNCE_MS) {
        bool now_pressed = !raw;
        if (now_pressed && !pressed_state) {
            request_recalibration();
        }
        pressed_state = now_pressed;
    }
}

/* ------------------------------------------------------------------ */

int main(void) {
    board_init();
    tusb_init();

    gpio_init(PIN_CAL_BUTTON);
    gpio_set_dir(PIN_CAL_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_CAL_BUTTON);

    uint offset = pio_add_program(pio, &pwm_capture_program);
    channel_init(&chan[0], PIN_CH1, 0, INVERT_CH1, offset);
    channel_init(&chan[1], PIN_CH2, 1, INVERT_CH2, offset);

    /* El OLED entero vive en core1. Core0 no lo toca nunca. */
    multicore_launch_core1(display_core1_main);

    while (true) {
        tud_task();
        poll_cdc();
        poll_cal_button();
        poll_channels();
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
