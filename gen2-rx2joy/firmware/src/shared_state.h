#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/sync.h"

/*
 * Comunicacion core0 -> core1 mediante seqlock.
 *
 * Por que no un mutex: un mutex puede bloquear al escritor. Core0 corre
 * el camino receptor -> HID y no puede esperar nunca a core1 por un
 * display. El seqlock hace que el escritor jamas se bloquee: publica y
 * sigue. Si core1 lee justo durante la escritura, detecta el conflicto
 * por el contador y reintenta. El costo para core0 son dos incrementos
 * y dos barreras, a 50 Hz.
 */

typedef struct {
    int16_t  axis[2];        /* valor HID enviado          */
    uint32_t ticks[2];       /* ancho de pulso crudo       */
    uint32_t lo[2], ctr[2], hi[2];
    uint32_t frame_us[2];
    uint32_t bad[2];
    bool     linked[2];
    bool     centered[2];

    bool     usb_mounted;
    bool     usb_suspended;
    uint16_t hid_hz;         /* reportes por segundo       */

    uint8_t  mode;           /* rx_mode_t                  */
    bool     failsafe;       /* solo S.BUS lo sabe de veras */
    bool     frame_lost;
    uint32_t frames_bad;     /* tramas descartadas          */

    /* Mensaje efimero en la barra inferior: confirmaciones de guardado
     * o borrado de calibracion. Vive unos segundos y se apaga solo. */
    char     flash_msg[10];
    uint32_t flash_until_ms;
} ui_state_t;

extern volatile uint32_t ui_seq;
extern ui_state_t        ui_state;

/* --- lado escritor (core0) --- */

static inline void ui_publish_begin(void) {
    ui_seq++;
    __dmb();
}

static inline void ui_publish_end(void) {
    __dmb();
    ui_seq++;
}

/* --- lado lector (core1) --- */

static inline void ui_snapshot(ui_state_t *dst) {
    uint32_t a, b;
    do {
        a = ui_seq;
        __dmb();
        *dst = ui_state;
        __dmb();
        b = ui_seq;
    } while (a != b || (a & 1u));   /* impar = escritura en curso */
}

#endif
