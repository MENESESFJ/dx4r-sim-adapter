/*
 * calstore.c — calibracion guardada en flash.
 *
 * Motivo: con tres protocolos y varios receptores, recalibrar en cada
 * enchufada se vuelve tedioso. Se guarda un juego de valores por modo,
 * de modo que cambiar el jumper no obliga a recalibrar.
 *
 * Guardar es SIEMPRE explicito (pulsacion larga de CAL o 'w' por
 * consola). Escribir flash en el RP2040 exige detener el otro nucleo y
 * desactivar interrupciones durante algunos milisegundos: eso corta el
 * HID y el OLED, y no debe ocurrir sin que el usuario lo pida.
 *
 * Durante la escritura el codigo que se ejecuta no puede estar en el
 * propio flash. Las rutinas del SDK que tocan flash ya estan en RAM, y
 * el tramo critico va con interrupciones apagadas, asi que ninguna ISR
 * intenta ejecutar desde XIP mientras esta deshabilitado.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "rx2joy.h"

#define CAL_MAGIC    0x52583243u   /* "RX2C" */
#define CAL_VERSION  1u

/* Ultimo sector de la flash. En la RP2040-Zero son 2 MB, igual que en
 * la Pico, asi que el offset sale del tamaño declarado por el SDK y no
 * de un numero escrito a mano. */
#define CAL_OFFSET   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint32_t lo, ctr, hi;
    uint8_t  valid;
    uint8_t  pad[3];
} cal_entry_t;

typedef struct {
    uint32_t    magic;
    uint32_t    version;
    cal_entry_t ch[MODE_COUNT][CH_COUNT];
    uint32_t    crc;
} cal_blob_t;

_Static_assert(sizeof(cal_blob_t) <= FLASH_PAGE_SIZE,
               "el bloque de calibracion no cabe en una pagina de flash");

/* ------------------------------------------------------------------ */

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
        }
    }
    return ~c;
}

static const cal_blob_t *flash_blob(void) {
    return (const cal_blob_t *)(XIP_BASE + CAL_OFFSET);
}

static bool blob_valid(const cal_blob_t *b) {
    if (b->magic != CAL_MAGIC || b->version != CAL_VERSION) return false;
    uint32_t want = crc32((const uint8_t *)b,
                          sizeof(cal_blob_t) - sizeof(uint32_t));
    return want == b->crc;
}

/* ------------------------------------------------------------------ */

bool calstore_load(rx_mode_t mode) {
    const cal_blob_t *b = flash_blob();
    if (!blob_valid(b) || mode >= MODE_COUNT) return false;

    bool any = false;
    for (uint32_t i = 0; i < CH_COUNT; i++) {
        const cal_entry_t *e = &b->ch[mode][i];
        if (!e->valid) continue;

        /* Los valores guardados se validan igual que una muestra viva:
         * una flash corrupta no debe poder dejar el adaptador con un
         * centro imposible y los ejes clavados en un extremo. */
        if (e->lo < PULSE_MIN_TICKS || e->hi > PULSE_MAX_TICKS) continue;
        if (!(e->lo < e->ctr && e->ctr < e->hi)) continue;

        chan[i].lo       = e->lo;
        chan[i].ctr      = e->ctr;
        chan[i].hi       = e->hi;
        chan[i].centered = true;
        any = true;
    }
    return any;
}

bool calstore_save(rx_mode_t mode) {
    if (mode >= MODE_COUNT) return false;

    /* Se parte del contenido actual para no borrar la calibracion de
     * los otros modos: la flash se borra por sectores completos. */
    static cal_blob_t blob;
    const cal_blob_t *cur = flash_blob();

    if (blob_valid(cur)) memcpy(&blob, cur, sizeof(blob));
    else                 memset(&blob, 0, sizeof(blob));

    blob.magic   = CAL_MAGIC;
    blob.version = CAL_VERSION;

    for (uint32_t i = 0; i < CH_COUNT; i++) {
        cal_entry_t *e = &blob.ch[mode][i];
        if (!chan[i].centered) { e->valid = 0; continue; }
        e->lo    = chan[i].lo;
        e->ctr   = chan[i].ctr;
        e->hi    = chan[i].hi;
        e->valid = 1;
    }

    blob.crc = crc32((const uint8_t *)&blob,
                     sizeof(blob) - sizeof(uint32_t));

    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &blob, sizeof(blob));

    /* Core1 tiene que quedar detenido: si intenta ejecutar desde flash
     * mientras el sector se borra, el chip se cuelga. */
    multicore_lockout_start_blocking();
    uint32_t save = save_and_disable_interrupts();

    flash_range_erase(CAL_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CAL_OFFSET, page, FLASH_PAGE_SIZE);

    restore_interrupts(save);
    multicore_lockout_end_blocking();

    return blob_valid(flash_blob());
}

bool calstore_erase(void) {
    multicore_lockout_start_blocking();
    uint32_t save = save_and_disable_interrupts();

    flash_range_erase(CAL_OFFSET, FLASH_SECTOR_SIZE);

    restore_interrupts(save);
    multicore_lockout_end_blocking();

    return !blob_valid(flash_blob());
}
