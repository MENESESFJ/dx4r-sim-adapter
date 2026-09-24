/* ============================================================
 *  RX2JOY  -  USB adapter for surface RC simulators
 *  Descriptor HID: 2 ejes de 16 bits + 4 botones
 *  Plataforma: RP2040 / RP2040-Zero  +  TinyUSB (Pico SDK)
 * ============================================================ */

#ifndef RX2JOY_HID_H
#define RX2JOY_HID_H

#include <stdint.h>

/* ------------------------------------------------------------
 *  1. IDENTIDAD DEL DISPOSITIVO (VID / PID)
 *
 *  0x1209 = pid.codes, VID comunitario libre para proyectos
 *           abiertos y de uso personal.
 *  0x0001 = PID reservado para pruebas / desarrollo.
 *
 *  Estos dos numeros son la "huella" del dongle: Windows y
 *  VRC Pro guardan calibracion y mapeo indexados por ellos.
 *  Una vez que funcione, NO los cambies: si los cambias,
 *  el PC lo ve como un dispositivo nuevo y pierdes el mapeo.
 *
 *  Si algun dia hay rev1 y rev2 conviviendo, cambia SOLO el
 *  PID (0x0001 -> 0x0002) para que sean dispositivos distintos.
 * ------------------------------------------------------------ */
#define RX2JOY_VID          0x1209
#define RX2JOY_PID          0x0001
#define RX2JOY_BCD_DEVICE   0x0200   /* version 2.00 = rev2 UART */

#define RX2JOY_STR_MANUF    "RX2JOY"
#define RX2JOY_STR_PRODUCT  "RX2JOY Surface RC Sim"   /* lo que se ve en joy.cpl */


/* ------------------------------------------------------------
 *  2. RANGO DE LOS EJES
 *
 *  0 .. 32767  ->  32768 pasos por eje.
 *  Centro teorico = 16384.
 *
 *  A modo de comparacion: un descriptor de 8 bits da solo 256
 *  pasos, y eso se siente escalonado en la direccion. Con 15-16
 *  bits el volante queda suave y sobra resolucion para cualquier
 *  fuente (PWM en us, S.BUS en 11 bits, i-BUS en 16 bits).
 * ------------------------------------------------------------ */
#define RX2JOY_AXIS_MIN     0
#define RX2JOY_AXIS_CENTER  16384
#define RX2JOY_AXIS_MAX     32767


/* ------------------------------------------------------------
 *  3. REPORT HID
 *
 *  5 bytes en total. __packed es obligatorio: sin el, el
 *  compilador alinea los campos y el PC lee basura.
 * ------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint16_t x;        /* STR  - direccion  */
    uint16_t y;        /* THR  - acelerador */
    uint8_t  buttons;  /* bit0..bit3 libres (CAL, modo, etc.) */
} rx2joy_report_t;


/* ------------------------------------------------------------
 *  4. DESCRIPTOR HID
 *
 *  Esta tabla es lo que le dice a Windows "soy un joystick con
 *  dos ejes absolutos y cuatro botones". Sin ella no apareces
 *  como control de juego, apareces como dispositivo desconocido.
 *
 *  Usage Joystick (0x04) en vez de Gamepad (0x05): los
 *  simuladores de RC esperan joystick, y ademas evita que
 *  Windows lo capture como mando de Xbox.
 * ------------------------------------------------------------ */
#define RX2JOY_HID_REPORT_DESC                                          \
    0x05, 0x01,        /* Usage Page (Generic Desktop)              */  \
    0x09, 0x04,        /* Usage (Joystick)                          */  \
    0xA1, 0x01,        /* Collection (Application)                  */  \
                                                                        \
    0x09, 0x01,        /*   Usage (Pointer)                         */  \
    0xA1, 0x00,        /*   Collection (Physical)                   */  \
    0x05, 0x01,        /*     Usage Page (Generic Desktop)          */  \
    0x09, 0x30,        /*     Usage (X)  -> STR                     */  \
    0x09, 0x31,        /*     Usage (Y)  -> THR                     */  \
    0x15, 0x00,        /*     Logical Minimum (0)                   */  \
    0x26, 0xFF, 0x7F,  /*     Logical Maximum (32767)               */  \
    0x75, 0x10,        /*     Report Size (16 bits)                 */  \
    0x95, 0x02,        /*     Report Count (2 ejes)                 */  \
    0x81, 0x02,        /*     Input (Data, Variable, Absolute)      */  \
    0xC0,              /*   End Collection                          */  \
                                                                        \
    0x05, 0x09,        /*   Usage Page (Button)                     */  \
    0x19, 0x01,        /*   Usage Minimum (Button 1)                */  \
    0x29, 0x04,        /*   Usage Maximum (Button 4)                */  \
    0x15, 0x00,        /*   Logical Minimum (0)                     */  \
    0x25, 0x01,        /*   Logical Maximum (1)                     */  \
    0x75, 0x01,        /*   Report Size (1 bit)                     */  \
    0x95, 0x04,        /*   Report Count (4 botones)                */  \
    0x81, 0x02,        /*   Input (Data, Variable, Absolute)        */  \
                                                                        \
    0x75, 0x04,        /*   Report Size (4 bits)  - relleno         */  \
    0x95, 0x01,        /*   Report Count (1)                        */  \
    0x81, 0x03,        /*   Input (Constant) - completa el byte     */  \
                                                                        \
    0xC0               /* End Collection                            */


/* ------------------------------------------------------------
 *  5. ESCALADO DE LAS TRES FUENTES AL RANGO HID
 *
 *  Importante: NO uses rangos fijos. Cada marca escala distinto
 *  (Futaba, FlySky y Sanwa no coinciden), y por eso la rutina
 *  de calibracion con el boton CAL guarda min/centro/max reales
 *  de cada canal. Estas funciones son el paso final, ya con los
 *  extremos conocidos.
 * ------------------------------------------------------------ */

/* Escala lineal por tramos respecto al centro calibrado.
 * Por tramos y no lineal simple: si el centro no esta justo a
 * mitad de camino (muy comun por el trim de la radio), una
 * interpolacion unica deja el punto muerto descentrado. */
static inline uint16_t rx2joy_scale(int32_t raw,
                                    int32_t lo, int32_t mid, int32_t hi)
{
    if (raw <= lo)  return RX2JOY_AXIS_MIN;
    if (raw >= hi)  return RX2JOY_AXIS_MAX;

    if (raw < mid) {
        if (mid == lo) return RX2JOY_AXIS_CENTER;
        return (uint16_t)(((int64_t)(raw - lo) * RX2JOY_AXIS_CENTER)
                          / (mid - lo));
    } else {
        if (hi == mid) return RX2JOY_AXIS_CENTER;
        return (uint16_t)(RX2JOY_AXIS_CENTER
               + ((int64_t)(raw - mid) * (RX2JOY_AXIS_MAX - RX2JOY_AXIS_CENTER))
                 / (hi - mid));
    }
}

/* Valores tipicos de partida, antes de calibrar:
 *
 *   PWM   : raw en microsegundos      -> lo=1000  mid=1500  hi=2000
 *   S.BUS : raw 11 bits (0..2047)     -> lo=172   mid=992   hi=1811
 *   i-BUS : raw en us (viene ya en us)-> lo=1000  mid=1500  hi=2000
 *
 * Los de S.BUS son los de Futaba estandar. Sanwa en SSR y
 * FlySky difieren, asi que trata estos numeros como semilla
 * inicial, no como verdad: la calibracion los corrige.
 */

#endif /* RX2JOY_HID_H */
