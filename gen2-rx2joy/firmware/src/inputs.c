/*
 * inputs.c — todo lo que depende del protocolo de entrada.
 *
 * Tres modos, elegidos por jumper y resueltos una sola vez al arranque:
 *
 *   PWM    un cable por canal, medido por PIO con 20 ns de resolucion
 *   S.BUS  UART 100000 8E2 invertido, 25 bytes, 16 canales de 11 bits
 *   i-BUS  UART 115200 8N1, 32 bytes, 14 canales de 16 bits en us
 *
 * Los dos buses serie comparten UART0 en GP1 y difieren solo en la
 * configuracion del puerto y en el parser. La sincronizacion de trama
 * se hace por silencio entre tramas, que es el metodo robusto: buscar
 * el byte de cabecera dentro del flujo puede enganchar en un dato que
 * coincida con la cabecera, y el error se arrastra hasta el proximo
 * reinicio.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "pwm_capture.pio.h"
#include "rx2joy.h"

/* ------------------------------------------------------------------ */
/* Selector de modo (JP1 en GP14)                                      */
/* ------------------------------------------------------------------ */

/*
 * Tres estados con un pin: se lee dos veces, una con pull-up interno y
 * otra con pull-down. Si ambas coinciden, el pin esta atado a un riel;
 * si difieren, esta flotando y sigue al pull que le pongamos.
 *
 *   jumper a 3V3  -> 1, 1  -> PWM
 *   jumper a GND  -> 0, 0  -> S.BUS
 *   sin jumper    -> 1, 0  -> i-BUS
 *
 * Los 2 ms de espera no son adorno: el pin tiene capacidad parasita y
 * el pull interno ronda los 60 kOhm, asi que la constante de tiempo es
 * de microsegundos, pero con el jumper puesto conviene dar margen para
 * que la lectura no dependa del layout.
 */
rx_mode_t mode_read(void) {
    gpio_init(PIN_MODE_SEL);
    gpio_set_dir(PIN_MODE_SEL, GPIO_IN);

    gpio_pull_up(PIN_MODE_SEL);
    sleep_ms(2);
    bool with_pu = gpio_get(PIN_MODE_SEL);

    gpio_pull_down(PIN_MODE_SEL);
    sleep_ms(2);
    bool with_pd = gpio_get(PIN_MODE_SEL);

    gpio_disable_pulls(PIN_MODE_SEL);

    if (with_pu && with_pd)   return MODE_PWM;    /* atado a 3V3 */
    if (!with_pu && !with_pd) return MODE_SBUS;   /* atado a GND */
    return MODE_IBUS;                             /* flotando    */
}

const char *mode_name(rx_mode_t m) {
    switch (m) {
    case MODE_PWM:  return "PWM";
    case MODE_SBUS: return "SBUS";
    case MODE_IBUS: return "IBUS";
    default:        return "??";
    }
}

uint32_t mode_default_frame_us(rx_mode_t m) {
    switch (m) {
    case MODE_SBUS: return 14000u;
    case MODE_IBUS: return 7000u;
    default:        return 20000u;   /* PWM estandar */
    }
}

/* ------------------------------------------------------------------ */
/* Estado comun                                                        */
/* ------------------------------------------------------------------ */

static rx_mode_t s_mode = MODE_PWM;

static volatile uint32_t s_frames_ok  = 0;
static volatile uint32_t s_frames_bad = 0;
static volatile bool     s_failsafe   = false;
static volatile bool     s_frame_lost = false;

uint32_t inputs_frames_ok(void)  { return s_frames_ok;  }
uint32_t inputs_frames_bad(void) { return s_frames_bad; }
bool     inputs_failsafe(void)   { return s_failsafe;   }
bool     inputs_frame_lost(void) { return s_frame_lost; }

/* ------------------------------------------------------------------ */
/* Camino PWM                                                          */
/* ------------------------------------------------------------------ */

static PIO s_pio = pio0;

static void pwm_init(void) {
    uint offset = pio_add_program(s_pio, &pwm_capture_program);
    pwm_capture_program_init(s_pio, 0, offset, PIN_PWM_STR, PIO_CLK_HZ);
    pwm_capture_program_init(s_pio, 1, offset, PIN_PWM_THR, PIO_CLK_HZ);
}

static void pwm_poll(void) {
    for (uint32_t i = 0; i < CH_COUNT; i++) {
        /* Drenar la FIFO completa: interesa la muestra mas nueva, no
         * ponerse al dia con una cola vieja.
         *
         * El timestamp se toma por muestra y no una sola vez para todo
         * el drenado. En el v1 se tomaba una sola vez y, cuando habia
         * mas de una muestra encolada, el periodo medido salia cero. */
        while (!pio_sm_is_rx_fifo_empty(s_pio, i)) {
            uint32_t t = pio_sm_get(s_pio, i);
            channels_feed(i, t, time_us_32());
            s_frames_ok++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Camino serial: recepcion por interrupcion                           */
/* ------------------------------------------------------------------ */

#define BUS_MAX_FRAME   32u
#define BUS_GAP_US      800u    /* silencio que marca fin de trama */

#define SBUS_FRAME_LEN  25u
#define SBUS_HEADER     0x0F
#define SBUS_FLAG_LOST  0x04
#define SBUS_FLAG_FS    0x08

#define IBUS_FRAME_LEN  32u
#define IBUS_HDR0       0x20
#define IBUS_HDR1       0x40

static volatile uint8_t  s_rx[BUS_MAX_FRAME];
static volatile uint32_t s_rx_idx      = 0;
static volatile uint32_t s_rx_last_us  = 0;

static volatile uint8_t  s_frame[BUS_MAX_FRAME];
static volatile bool     s_frame_ready = false;
static volatile uint32_t s_frame_us    = 0;
static volatile uint8_t  s_end_byte    = 0;   /* diagnostico S.BUS */

static uint32_t s_frame_len = SBUS_FRAME_LEN;

/* Entrega la trama al bucle principal. Se llama desde la ISR, asi que
 * hace lo minimo: copiar y levantar la bandera. El parseo y el trabajo
 * con los canales ocurren fuera de la interrupcion. */
static void publish_frame(uint32_t now) {
    if (s_frame_ready) {
        /* El consumidor se quedo atras. Se prefiere la trama nueva: en
         * control, un dato viejo no sirve para nada. */
        s_frames_bad++;
    }
    memcpy((void *)s_frame, (const void *)s_rx, s_frame_len);
    s_frame_us    = now;
    s_frame_ready = true;
}

static void on_uart_rx(void) {
    while (uart_is_readable(uart0)) {
        uint8_t c   = (uint8_t)uart_get_hw(uart0)->dr;
        uint32_t now = time_us_32();

        /* Silencio largo = arranca una trama nueva. */
        if ((uint32_t)(now - s_rx_last_us) > BUS_GAP_US) {
            s_rx_idx = 0;
        }
        s_rx_last_us = now;

        if (s_rx_idx < s_frame_len) {
            s_rx[s_rx_idx++] = c;

            if (s_rx_idx == s_frame_len) {
                publish_frame(now);
                /* Se queda en "lleno": los bytes extra de esta rafaga se
                 * descartan y el proximo silencio reinicia el indice. */
            }
        }
    }
}

static void bus_init(rx_mode_t mode) {
    uint  baud   = (mode == MODE_SBUS) ? 100000u : 115200u;
    uint  stop   = (mode == MODE_SBUS) ? 2u : 1u;
    uart_parity_t par = (mode == MODE_SBUS) ? UART_PARITY_EVEN : UART_PARITY_NONE;

    s_frame_len = (mode == MODE_SBUS) ? SBUS_FRAME_LEN : IBUS_FRAME_LEN;

    uart_init(uart0, baud);
    uart_set_format(uart0, 8, stop, par);
    uart_set_hw_flow(uart0, false, false);

    /* FIFO desactivada a proposito: una interrupcion por byte da un
     * timestamp exacto, y de ese timestamp depende la deteccion de
     * silencio que sincroniza las tramas. Con FIFO los bytes llegan en
     * lotes y el instante real de cada uno se pierde. El costo son
     * ~8000 interrupciones por segundo, irrelevante frente al resto. */
    uart_set_fifo_enabled(uart0, false);

    gpio_set_function(PIN_BUS_RX, GPIO_FUNC_UART);

    /* S.BUS es UART invertido. En el RP2040 se invierte en el propio
     * pin, sin transistor externo. */
    if (mode == MODE_SBUS) {
        gpio_set_inover(PIN_BUS_RX, GPIO_OVERRIDE_INVERT);
    } else {
        gpio_set_inover(PIN_BUS_RX, GPIO_OVERRIDE_NORMAL);
    }

    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);   /* solo RX */
}

/* ------------------------------------------------------------------ */
/* Parsers                                                             */
/* ------------------------------------------------------------------ */

/* S.BUS empaqueta 16 canales de 11 bits, LSB primero, en 22 bytes.
 * El desempaquetado generico evita la tabla de corrimientos a mano,
 * que es donde suelen colarse los errores de un bit. */
static void sbus_unpack(const uint8_t *b, uint16_t *out, uint32_t n) {
    uint32_t bits = 0, nbits = 0, byte = 1;
    for (uint32_t i = 0; i < n; i++) {
        while (nbits < 11) {
            bits |= (uint32_t)b[byte++] << nbits;
            nbits += 8;
        }
        out[i] = (uint16_t)(bits & 0x7FFu);
        bits >>= 11;
        nbits -= 11;
    }
}

/* S.BUS crudo (0..2047) a ticks.
 *
 * Mapeo estandar de Futaba: us = raw * 5/8 + 880, o sea 172 -> 988,
 * 992 -> 1500 y 1811 -> 2012. Otras marcas escalan distinto, pero eso
 * da igual: lo unico que se necesita es que la conversion sea lineal y
 * monotona, porque la calibracion se encarga de los extremos reales. */
static inline uint32_t sbus_to_ticks(uint16_t raw) {
    return ((uint32_t)raw * 125u) / 4u + 44000u;
}

static void sbus_parse(const uint8_t *b, uint32_t now) {
    if (b[0] != SBUS_HEADER) { s_frames_bad++; return; }

    uint16_t ch[4];
    sbus_unpack(b, ch, 4);

    uint8_t flags = b[23];
    s_end_byte   = b[24];
    s_frame_lost = (flags & SBUS_FLAG_LOST) != 0;

    /* El bit de failsafe es la unica forma fiable de detectar que la
     * radio se apago: el receptor sigue emitiendo tramas puntuales con
     * las posiciones de failsafe, asi que ningun timeout las veria. */
    s_failsafe = (flags & SBUS_FLAG_FS) != 0;

    /* Una trama marcada como perdida trae datos repetidos, no nuevos:
     * alimentarla ensuciaria la calibracion y el periodo medido. */
    if (s_frame_lost) { s_frames_bad++; return; }

    channels_feed(CH_STR, sbus_to_ticks(ch[BUS_CH_STR]), now);
    channels_feed(CH_THR, sbus_to_ticks(ch[BUS_CH_THR]), now);
    s_frames_ok++;
}

static void ibus_parse(const uint8_t *b, uint32_t now) {
    if (b[0] != IBUS_HDR0 || b[1] != IBUS_HDR1) { s_frames_bad++; return; }

    /* i-BUS no tiene bits de estado, pero si checksum, y eso ya filtra
     * la basura. La deteccion de enlace caido recae en el timeout. */
    uint16_t sum = 0xFFFFu;
    for (uint32_t i = 0; i < 30u; i++) sum -= b[i];

    uint16_t want = (uint16_t)(b[30] | ((uint16_t)b[31] << 8));
    if (sum != want) { s_frames_bad++; return; }

    /* Canales de 16 bits little-endian, ya en microsegundos. */
    uint16_t v0 = (uint16_t)(b[2 + 2 * BUS_CH_STR] |
                             ((uint16_t)b[3 + 2 * BUS_CH_STR] << 8));
    uint16_t v1 = (uint16_t)(b[2 + 2 * BUS_CH_THR] |
                             ((uint16_t)b[3 + 2 * BUS_CH_THR] << 8));

    channels_feed(CH_STR, (uint32_t)v0 * TICKS_PER_US, now);
    channels_feed(CH_THR, (uint32_t)v1 * TICKS_PER_US, now);
    s_frames_ok++;
}

static void bus_poll(void) {
    if (!s_frame_ready) return;

    uint8_t  local[BUS_MAX_FRAME];
    uint32_t now;

    /* Copia con la interrupcion apagada: la ISR puede pisar el buffer
     * en cualquier momento, y una trama mitad vieja mitad nueva pasaria
     * el checksum de i-BUS por pura mala suerte alguna vez. */
    uint32_t save = save_and_disable_interrupts();
    memcpy(local, (const void *)s_frame, s_frame_len);
    now           = s_frame_us;
    s_frame_ready = false;
    restore_interrupts(save);

    if (s_mode == MODE_SBUS) sbus_parse(local, now);
    else                     ibus_parse(local, now);
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

void inputs_init(rx_mode_t mode) {
    s_mode = mode;
    if (mode == MODE_PWM) pwm_init();
    else                  bus_init(mode);
}

void inputs_poll(uint32_t now_us) {
    if (s_mode == MODE_PWM) pwm_poll();
    else                    bus_poll();

    channels_check_timeouts(now_us);

    /* Sin tramas no hay forma de saber si hay failsafe: el estado debe
     * caducar junto con el enlace, o quedaria congelado en true despues
     * de desconectar el receptor. */
    if (s_mode == MODE_SBUS) {
        bool any_linked = false;
        for (uint32_t i = 0; i < CH_COUNT; i++) {
            if (chan[i].linked) { any_linked = true; break; }
        }
        if (!any_linked) { s_failsafe = false; s_frame_lost = false; }
    }
}

uint8_t inputs_end_byte(void) { return s_end_byte; }
