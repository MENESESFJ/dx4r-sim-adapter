/*
 * peak_probe.c — mide el voltaje pico real que entrega el SR2000,
 * ANTES de soldar la version definitiva del adaptador.
 *
 * Por que hace falta: el divisor 1.8k/3.3k de main.c asume que el
 * SR2000 entrega un HIGH logico de 5V (su alimentacion). Si en
 * realidad el receptor usa logica interna de 3.3V y solo esta
 * *alimentado* a 5V, el mismo divisor entrega ~2.1V en vez de
 * ~3.2V en el pin. Ambos casos son seguros para el RP2040 (VIH real
 * es 1.6V a 3.3V IOVDD, no 2.0V), pero el margen cambia ~3x entre
 * un caso y otro. Mejor medirlo que asumirlo.
 *
 * Como se mide sin osciloscopio: el mismo divisor que ya armaste
 * para CH1 se conecta a un pin ADC (GP26) en vez de a un GPIO
 * digital comun. El RP2040 muestrea a cientos de miles de veces por
 * segundo -- muchisimo mas rapido que el pulso de ~1-2ms que hay
 * que capturar -- asi que un simple "peak hold" en software agarra
 * el HIGH real con precision de sobra.
 *
 * Uso:
 *  1. Deja el MISMO divisor 1.8k/3.3k de CH1, pero mové su salida
 *     de GP2 a GP26 (ADC0) temporalmente.
 *  2. Compila y flashea ESTE firmware (no main.c).
 *  3. Abrí una terminal serie a 115200 baudios.
 *  4. Movete el stick de steering a fondo, en las dos direcciones,
 *     durante 5-10 segundos.
 *  5. Anota el PICO que se imprime.
 *
 * Lectura del resultado:
 *   ~3.2V  -> HIGH real ~5V.  El divisor actual esta comodo.
 *   ~2.1V  -> HIGH real ~3.3V. Sigue siendo seguro (margen ~0.5V
 *             sobre VIH), pero mas ajustado. Documentalo en el
 *             README y segui con el mismo divisor sin cambios,
 *             a menos que quieras mas margen.
 *
 * Terminado esto, volvé a mover el cable a GP2 y reflashea main.c.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#define PROBE_PIN       26u   /* GP26 = ADC0 */
#define PROBE_CHANNEL   0u
#define ADC_VREF        3.3f
#define ADC_MAX_COUNT   4095u

#define WINDOW_MS       1000u

int main(void) {
    stdio_init_all();

    adc_init();
    adc_gpio_init(PROBE_PIN);
    adc_select_input(PROBE_CHANNEL);

    /* Un par de segundos para que el usuario abra la terminal
     * antes de que empiece a imprimir. */
    sleep_ms(2000);

    printf("\n# peak_probe -- verificacion de HIGH real del SR2000\n");
    printf("# Conectar: divisor 1.8k/3.3k de CH1 -> GP26 (ADC0)\n");
    printf("# Mové el stick de steering a fondo, ambas direcciones,\n");
    printf("# durante unos 5-10 segundos.\n\n");

    uint16_t window_max = 0;
    uint16_t window_min = ADC_MAX_COUNT;
    uint16_t global_max = 0;
    uint32_t n = 0;
    absolute_time_t window_start = get_absolute_time();

    while (true) {
        uint16_t raw = adc_read();

        if (raw > window_max) window_max = raw;
        if (raw < window_min) window_min = raw;
        if (raw > global_max) global_max = raw;
        n++;

        if (absolute_time_diff_us(window_start, get_absolute_time()) >= (int64_t)WINDOW_MS * 1000) {
            float v_max     = window_max * ADC_VREF / ADC_MAX_COUNT;
            float v_min     = window_min * ADC_VREF / ADC_MAX_COUNT;
            float v_max_all = global_max * ADC_VREF / ADC_MAX_COUNT;

            printf("PICO=%.3fV  MIN=%.3fV  PICO_HISTORICO=%.3fV  (%lu muestras/s)\n",
                   v_max, v_min, v_max_all, (unsigned long)n);

            window_max  = 0;
            window_min  = ADC_MAX_COUNT;
            n           = 0;
            window_start = get_absolute_time();
        }
    }
}
