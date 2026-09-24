/*
 * rx2joy.h — configuracion central y tipos compartidos.
 *
 * RX2JOY gen2: adaptador USB multimarca para simuladores de RC de
 * superficie. Lee PWM, S.BUS o i-BUS segun la posicion de un jumper y
 * lo presenta al PC como joystick HID.
 *
 * Unidad interna comun: TICKS, a 50 por microsegundo.
 *   - PWM   : ya vienen en ticks desde el PIO
 *   - i-BUS : microsegundos * 50
 *   - S.BUS : 11 bits mapeados a microsegundos y luego a ticks
 *
 * Unificar la unidad antes de calibrar es lo que permite que todo lo
 * de aguas abajo (calibracion, escalado, display, consola) sea identico
 * en los tres modos y no haya tres caminos distintos que mantener.
 */

#ifndef RX2JOY_H
#define RX2JOY_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Pines  (identicos al proyecto original; GP1 y GP14 son nuevos)      */
/* ------------------------------------------------------------------ */

#define PIN_PWM_STR       2u    /* J1 via R3 */
#define PIN_PWM_THR       3u    /* J2 via R4 */
#define PIN_BUS_RX        1u    /* J4 via R5 — UART0 RX */
#define PIN_I2C_SDA       4u    /* OLED */
#define PIN_I2C_SCL       5u    /* OLED */
#define PIN_MODE_SEL     14u    /* JP1 selector de protocolo */
#define PIN_CAL_BUTTON   15u    /* SW1, activo en bajo, pull-up interno */
#define PIN_ADC_RAIL     26u    /* divisor R1/R2 */

/* ------------------------------------------------------------------ */
/* Unidad de tiempo                                                    */
/* ------------------------------------------------------------------ */

#define PIO_CLK_HZ        100000000.0f
#define TICKS_PER_US      50u
#define US_TO_TICKS(us)   ((uint32_t)((us) * TICKS_PER_US))

/* Ventana de validacion. Cubre los tres protocolos: un S.BUS crudo de
 * 0 mapea a 880 us y uno de 2047 a 2159 us, ambos dentro del rango. */
#define PULSE_MIN_TICKS   US_TO_TICKS(800)
#define PULSE_MAX_TICKS   US_TO_TICKS(2200)

/* ------------------------------------------------------------------ */
/* Modos                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    MODE_PWM  = 0,
    MODE_SBUS = 1,
    MODE_IBUS = 2,
    MODE_COUNT
} rx_mode_t;

/* Canales dentro de la trama del bus serial. Casi todas las radios de
 * superficie mandan direccion en 1 y acelerador en 2. */
#define BUS_CH_STR        0
#define BUS_CH_THR        1

#define CH_COUNT          2
#define CH_STR            0
#define CH_THR            1

/* ------------------------------------------------------------------ */
/* Estado por canal                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    bool     invert;

    /* captura */
    uint32_t last_ticks;
    uint32_t last_sample_us;
    uint32_t frame_us;          /* periodo entre tramas, suavizado */
    bool     linked;

    /* calibracion */
    bool     centered;
    uint32_t ctr, lo, hi;
    uint32_t cal_ref;           /* muestra de referencia de la ventana */
    uint32_t cal_acc;
    uint32_t cal_n;
    uint32_t cal_start_ms;

    /* estadistica */
    uint32_t bad_count;
} channel_t;

extern channel_t chan[CH_COUNT];

/* ------------------------------------------------------------------ */
/* Tunables de calibracion                                             */
/* ------------------------------------------------------------------ */

/* El centro se da por bueno cuando el canal estuvo quieto durante
 * CAL_STABLE_MS y ademas llegaron al menos CAL_MIN_SAMPLES muestras.
 *
 * Por que por tiempo y no por numero de muestras: el v1 pedia 32
 * muestras fijas, que a 5,5 ms del SR2000 son 176 ms pero a 20 ms de
 * un PWM estandar son 640 ms. El criterio quedaba atado a la frecuencia
 * del receptor en vez de a lo que realmente importa, que es cuanto
 * tiempo estuvo quieto el control. El minimo de muestras evita el
 * problema inverso: que un receptor muy rapido de por calibrado un
 * canal con un puñado de muestras ruidosas. */
#define CAL_STABLE_MS       300u
#define CAL_MIN_SAMPLES     8u
#define CENTER_TOL_TICKS    US_TO_TICKS(20)
#define INITIAL_HALF_SPAN   US_TO_TICKS(400)

/* Zona muerta alrededor del centro, en ticks. 0 = sin zona muerta. */
#define DEADBAND_TICKS      0u

/* ------------------------------------------------------------------ */
/* API: modo                                                          */
/* ------------------------------------------------------------------ */

rx_mode_t   mode_read(void);
const char *mode_name(rx_mode_t m);
uint32_t    mode_default_frame_us(rx_mode_t m);

/* ------------------------------------------------------------------ */
/* API: entradas                                                       */
/* ------------------------------------------------------------------ */

void inputs_init(rx_mode_t mode);
void inputs_poll(uint32_t now_us);

/* Solo S.BUS los reporta de verdad; en PWM e i-BUS quedan en false y
 * la deteccion recae en el timeout. */
bool inputs_failsafe(void);
bool inputs_frame_lost(void);

/* Tramas validas y tramas descartadas, para diagnostico por consola. */
uint32_t inputs_frames_ok(void);
uint32_t inputs_frames_bad(void);
uint8_t  inputs_end_byte(void);   /* ultimo byte de cierre S.BUS */

/* ------------------------------------------------------------------ */
/* API: canales y calibracion                                          */
/* ------------------------------------------------------------------ */

void    channels_init(rx_mode_t mode);
void    channels_feed(uint32_t idx, uint32_t ticks, uint32_t now_us);
void    channels_check_timeouts(uint32_t now_us);
bool    channels_link_ok(void);
int16_t channel_axis(uint32_t idx);
void    channels_reset_cal(void);
bool    channels_all_centered(void);

/* ------------------------------------------------------------------ */
/* API: persistencia de calibracion en flash                           */
/* ------------------------------------------------------------------ */

/* Guardar es explicito (pulsacion larga de CAL o 'w' por consola), no
 * automatico: escribir flash exige detener el otro nucleo y desactivar
 * interrupciones, y eso no debe pasar sin que el usuario lo pida. */
bool calstore_load(rx_mode_t mode);
bool calstore_save(rx_mode_t mode);
bool calstore_erase(void);

/* ------------------------------------------------------------------ */

static inline uint32_t absdiff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

#endif /* RX2JOY_H */
