/*
 * channels.c — calibracion, escalado y deteccion de enlace.
 *
 * Este archivo no sabe nada de protocolos. Recibe muestras ya
 * convertidas a ticks y las trata igual vengan de donde vengan.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "bsp/board.h"

#include "rx2joy.h"

channel_t chan[CH_COUNT];

static rx_mode_t s_mode = MODE_PWM;

/* ------------------------------------------------------------------ */

void channels_reset_cal(void) {
    for (uint32_t i = 0; i < CH_COUNT; i++) {
        channel_t *ch = &chan[i];
        ch->centered     = false;
        ch->cal_acc      = 0;
        ch->cal_n        = 0;
        ch->cal_ref      = 0;
        ch->cal_start_ms = 0;
        ch->lo = ch->hi = ch->ctr = 0;
    }
}

void channels_init(rx_mode_t mode) {
    memset(chan, 0, sizeof(chan));
    s_mode = mode;
    channels_reset_cal();
}

bool channels_all_centered(void) {
    for (uint32_t i = 0; i < CH_COUNT; i++) {
        if (!chan[i].centered) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Timeout dinamico                                                    */
/* ------------------------------------------------------------------ */

/*
 * El v1 usaba 60 ms fijos, dimensionados para el SR2000 a 5,5 ms
 * (~11 tramas de margen). Con un PWM estandar de 20 ms esos mismos
 * 60 ms son solo 3 tramas: basta con que se pierdan dos seguidas para
 * que el adaptador declare el enlace caido y mande los ejes a cero,
 * o sea un tiron en medio de una curva.
 *
 * Aca el margen se calcula sobre el periodo realmente medido: cuatro
 * tramas mas 2 ms de holgura. Se acota por abajo para no ser
 * histerico y por arriba para que la perdida real se note rapido.
 */
static uint32_t link_timeout_us(const channel_t *ch) {
    uint32_t f = ch->frame_us;

    /* Antes de tener una medicion confiable, el valor nominal del modo. */
    if (f < 1000u || f > 60000u) f = mode_default_frame_us(s_mode);

    uint32_t t = f * 4u + 2000u;
    if (t < 15000u)  t = 15000u;
    if (t > 150000u) t = 150000u;
    return t;
}

/* ------------------------------------------------------------------ */
/* Ingreso de muestras                                                 */
/* ------------------------------------------------------------------ */

void channels_feed(uint32_t idx, uint32_t ticks, uint32_t now_us) {
    if (idx >= CH_COUNT) return;
    channel_t *ch = &chan[idx];

    if (ticks < PULSE_MIN_TICKS || ticks > PULSE_MAX_TICKS) {
        ch->bad_count++;
        return;
    }

    /* --- periodo entre tramas ---
     *
     * Defecto del v1: el periodo se calculaba con un unico timestamp
     * tomado al principio del drenado de la FIFO. Si habia mas de una
     * muestra encolada, la segunda y las siguientes daban un delta de
     * cero y el periodo mostrado se desplomaba.
     *
     * Aca se descarta cualquier delta implausible y se suaviza con una
     * media movil de 1/4, que es lo que hace util el numero: sirve para
     * dimensionar el timeout, y para eso importa la tendencia, no la
     * trama individual. */
    if (ch->linked && ch->last_sample_us != 0) {
        uint32_t dt = now_us - ch->last_sample_us;
        if (dt >= 1000u && dt <= 60000u) {
            ch->frame_us = ch->frame_us ? (ch->frame_us * 3u + dt) / 4u : dt;
        }
    }

    ch->last_sample_us = now_us;
    ch->last_ticks     = ticks;
    ch->linked         = true;

    /* --- calibracion del centro --- */
    if (!ch->centered) {
        uint32_t now_ms = board_millis();

        if (ch->cal_n == 0) {
            ch->cal_ref      = ticks;
            ch->cal_acc      = ticks;
            ch->cal_n        = 1;
            ch->cal_start_ms = now_ms;
        } else if (absdiff(ticks, ch->cal_ref) <= CENTER_TOL_TICKS) {
            ch->cal_acc += ticks;
            ch->cal_n++;

            if (ch->cal_n >= CAL_MIN_SAMPLES &&
                (now_ms - ch->cal_start_ms) >= CAL_STABLE_MS) {
                ch->ctr      = ch->cal_acc / ch->cal_n;
                ch->lo       = ch->ctr - INITIAL_HALF_SPAN;
                ch->hi       = ch->ctr + INITIAL_HALF_SPAN;
                ch->centered = true;
            }
        } else {
            /* Se movio: la ventana se reinicia desde esta muestra, no
             * desde cero. Asi, si el usuario mueve el control y lo deja
             * quieto en otro punto, la cuenta arranca de inmediato en
             * vez de perder una muestra. */
            ch->cal_ref      = ticks;
            ch->cal_acc      = ticks;
            ch->cal_n        = 1;
            ch->cal_start_ms = now_ms;
        }
    } else {
        /* El rango solo se expande. Mueve volante y gatillo a los topes
         * una vez y queda calibrado. */
        if (ticks < ch->lo) ch->lo = ticks;
        if (ticks > ch->hi) ch->hi = ticks;
    }
}

void channels_check_timeouts(uint32_t now_us) {
    for (uint32_t i = 0; i < CH_COUNT; i++) {
        channel_t *ch = &chan[i];
        if (ch->linked &&
            (uint32_t)(now_us - ch->last_sample_us) > link_timeout_us(ch)) {
            ch->linked = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Enlace                                                              */
/* ------------------------------------------------------------------ */

/*
 * Enlace global, no por canal.
 *
 * El SR2000 no deja de emitir el canal de acelerador en failsafe, solo
 * los demas, asi que chan[CH_THR].linked puede seguir en true con el
 * enlace RF caido. El indicador confiable es que CUALQUIER canal deje
 * de llegar. Por eso es AND y no por canal.
 *
 * En S.BUS ademas hay una señal explicita: el bit de failsafe de la
 * trama. Cuando esta puesto, el enlace esta caido aunque las tramas
 * sigan llegando puntuales, que es precisamente el caso que el timeout
 * no puede ver.
 */
bool channels_link_ok(void) {
    if (inputs_failsafe()) return false;

    for (uint32_t i = 0; i < CH_COUNT; i++) {
        if (!chan[i].linked) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Escalado                                                            */
/* ------------------------------------------------------------------ */

/*
 * Escalado partido en dos tramos, para que el centro caiga exacto en 0
 * aunque el recorrido no sea simetrico, que en un gatillo nunca lo es.
 * Con una interpolacion lineal unica el punto muerto quedaria corrido
 * hacia un lado apenas el trim de la radio mueva el centro.
 */
static int16_t scale_axis(const channel_t *ch, uint32_t t) {
    if (t >= ch->ctr) {
        uint32_t d    = t - ch->ctr;
        uint32_t span = ch->hi - ch->ctr;
        if (span <= DEADBAND_TICKS || d <= DEADBAND_TICKS) return 0;
        d    -= DEADBAND_TICKS;
        span -= DEADBAND_TICKS;
        int64_t v = ((int64_t)d * 32767) / span;
        return (int16_t)(v > 32767 ? 32767 : v);
    } else {
        uint32_t d    = ch->ctr - t;
        uint32_t span = ch->ctr - ch->lo;
        if (span <= DEADBAND_TICKS || d <= DEADBAND_TICKS) return 0;
        d    -= DEADBAND_TICKS;
        span -= DEADBAND_TICKS;
        int64_t v = ((int64_t)d * 32768) / span;
        return (int16_t)(-(v > 32768 ? 32768 : v));
    }
}

int16_t channel_axis(uint32_t idx) {
    if (idx >= CH_COUNT) return 0;
    const channel_t *ch = &chan[idx];

    if (!channels_link_ok() || !ch->centered) return 0;

    int16_t v = scale_axis(ch, ch->last_ticks);
    if (ch->invert) v = (v == INT16_MIN) ? INT16_MAX : (int16_t)(-v);
    return v;
}
