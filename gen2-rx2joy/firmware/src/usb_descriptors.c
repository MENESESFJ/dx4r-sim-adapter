/* ============================================================
 *  RX2JOY  -  USB adapter for surface RC simulators
 *  usb_descriptors.c  -  descriptores TinyUSB
 *
 *  Compañero de rx2joy_hid.h
 *  Plataforma: RP2040 / RP2040-Zero + Pico SDK + TinyUSB
 * ============================================================ */

#include "tusb.h"
#include "rx2joy_hid.h"

/* ------------------------------------------------------------
 *  1. DESCRIPTOR DE DISPOSITIVO
 *
 *  Es la primera respuesta del dongle cuando lo enchufas.
 *  De aqui salen el VID/PID con los que Windows indexa la
 *  calibracion y VRC Pro el mapeo de ejes.
 * ------------------------------------------------------------ */
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,        /* USB 2.0 Full Speed */

    /* 0x00 en clase/subclase/protocolo: la clase real (HID) se
     * declara en la interfaz, no en el dispositivo. Es lo
     * correcto para un dispositivo de una sola funcion. */
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = RX2JOY_VID,
    .idProduct          = RX2JOY_PID,
    .bcdDevice          = RX2JOY_BCD_DEVICE,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

/* TinyUSB pide este descriptor al enumerar. */
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &desc_device;
}


/* ------------------------------------------------------------
 *  2. DESCRIPTOR DE REPORT HID
 *
 *  La macro viene de rx2joy_hid.h: 2 ejes de 16 bits + 4 botones.
 * ------------------------------------------------------------ */
uint8_t const desc_hid_report[] = {
    RX2JOY_HID_REPORT_DESC
};

/* instance: siempre 0, porque hay una sola interfaz HID. */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void) instance;
    return desc_hid_report;
}


/* ------------------------------------------------------------
 *  3. DESCRIPTOR DE CONFIGURACION
 *
 *  Una sola interfaz HID con un endpoint IN de interrupcion.
 *
 *  El intervalo de polling es el parametro que mas se nota al
 *  usar el dongle: 1 ms = 1000 Hz, el maximo en Full Speed.
 *  Ojo con una cosa: aunque el receptor entregue datos nuevos
 *  cada 14 ms (S.BUS) o 20 ms (PWM), conviene dejarlo en 1 ms
 *  igual, porque asi el PC recoge cada trama apenas llega en
 *  vez de esperar al siguiente ciclo de sondeo. Esa espera
 *  extra es latencia pura y gratuita.
 * ------------------------------------------------------------ */
enum { ITF_NUM_HID = 0, ITF_NUM_TOTAL };

#define EPNUM_HID       0x81    /* endpoint 1, direccion IN */
#define RX2JOY_POLL_MS  1       /* 1 ms = 1000 Hz */

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    /* config number, interface count, string index,
     * total length, attribute, power in mA */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    /* interface number, string index, protocol,
     * report descriptor len, EP In address, size, polling interval */
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, RX2JOY_POLL_MS)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return desc_configuration;
}


/* ------------------------------------------------------------
 *  4. DESCRIPTORES DE TEXTO
 *
 *  El indice 2 (producto) es literalmente lo que aparece en
 *  joy.cpl y en la lista de controles de VRC Pro. Conviene que
 *  sea corto: los paneles de Windows truncan los nombres largos.
 *
 *  El numero de serie se genera desde el ID unico del chip
 *  RP2040. Eso permite distinguir dos dongles conectados al
 *  mismo PC aunque compartan VID/PID.
 * ------------------------------------------------------------ */
#include "pico/unique_id.h"

char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },   /* 0: idioma, ingles (0x0409) */
    RX2JOY_STR_MANUF,               /* 1: fabricante */
    RX2JOY_STR_PRODUCT,             /* 2: producto   */
    serial_str                      /* 3: numero de serie */
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }

        /* El serial se rellena la primera vez que se pide. */
        if (index == 3 && serial_str[0] == '\0') {
            pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
        }

        const char *str = string_desc_arr[index];

        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31) chr_count = 31;

        /* ASCII -> UTF-16LE, que es lo que exige USB. */
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    /* Primer word: longitud total y tipo de descriptor. */
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}


/* ------------------------------------------------------------
 *  5. CALLBACKS OBLIGATORIOS DE HID
 *
 *  TinyUSB no compila sin ellos. Como el dongle solo envia
 *  datos y nunca recibe (no hay LEDs ni force feedback),
 *  quedan vacios a proposito.
 * ------------------------------------------------------------ */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer;   (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer;   (void) bufsize;
}


/* ------------------------------------------------------------
 *  6. ENVIO DEL REPORT  (llamar desde el bucle principal)
 *
 *  Uso tipico en main.c:
 *
 *      while (true) {
 *          tud_task();                    // atender USB, siempre
 *          if (hay_datos_nuevos) {
 *              rx2joy_report_t r = {
 *                  .x = rx2joy_scale(raw_str, cal.str_lo,
 *                                    cal.str_mid, cal.str_hi),
 *                  .y = rx2joy_scale(raw_thr, cal.thr_lo,
 *                                    cal.thr_mid, cal.thr_hi),
 *                  .buttons = 0
 *              };
 *              rx2joy_send(&r);
 *          }
 *      }
 *
 *  Importante: tud_task() tiene que correr seguido, sin bloqueos
 *  largos. Por eso la lectura de PWM conviene hacerla por
 *  interrupcion o PIO y no esperando el flanco dentro del bucle.
 * ------------------------------------------------------------ */
bool rx2joy_send(rx2joy_report_t const *report)
{
    if (!tud_hid_ready()) return false;   /* aun no enumerado o EP ocupado */
    return tud_hid_report(0, report, sizeof(rx2joy_report_t));
}
