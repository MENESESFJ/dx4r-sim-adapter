/*
 * usb_descriptors.c — dispositivo compuesto HID (joystick) + CDC (consola)
 *
 * El punto critico de todo el archivo es el ultimo argumento de
 * TUD_HID_DESCRIPTOR: bInterval = 1. TinyUSB trae 10 por defecto y eso
 * solo ya agrega hasta 10 ms de latencia, mas que el frame del SR2000.
 */

#include "tusb.h"

/* ------------------------------------------------------------------ */
/* Descriptor de reporte HID                                           */
/* ------------------------------------------------------------------ */

uint8_t const desc_hid_report[] = {
    0x05, 0x01,             /* Usage Page (Generic Desktop)   */
    0x09, 0x04,             /* Usage (Joystick)               */
    0xA1, 0x01,             /* Collection (Application)       */

    0x09, 0x01,             /*   Usage (Pointer)              */
    0xA1, 0x00,             /*   Collection (Physical)        */
    0x09, 0x30,             /*     Usage (X)  = steering      */
    0x09, 0x31,             /*     Usage (Y)  = throttle      */
    0x16, 0x00, 0x80,       /*     Logical Minimum (-32768)   */
    0x26, 0xFF, 0x7F,       /*     Logical Maximum ( 32767)   */
    0x75, 0x10,             /*     Report Size (16)           */
    0x95, 0x02,             /*     Report Count (2)           */
    0x81, 0x02,             /*     Input (Data,Var,Abs)       */
    0xC0,                   /*   End Collection               */

    /* 8 botones sin usar. Cuestan un byte y evitan que algun driver
     * se ponga raro con un joystick de cero botones. */
    0x05, 0x09,             /*   Usage Page (Button)          */
    0x19, 0x01,             /*   Usage Minimum (1)            */
    0x29, 0x08,             /*   Usage Maximum (8)            */
    0x15, 0x00,             /*   Logical Minimum (0)          */
    0x25, 0x01,             /*   Logical Maximum (1)          */
    0x75, 0x01,             /*   Report Size (1)              */
    0x95, 0x08,             /*   Report Count (8)             */
    0x81, 0x02,             /*   Input (Data,Var,Abs)         */

    0xC0                    /* End Collection                 */
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

/* ------------------------------------------------------------------ */
/* Descriptor de dispositivo                                           */
/* ------------------------------------------------------------------ */

/* VID/PID de pid.codes reservados para pruebas y uso privado.
 * No distribuyas hardware con este par: pide uno propio en pid.codes. */
#define USB_VID 0x1209
#define USB_PID 0x0001

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    /* Compuesto con IAD */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/* ------------------------------------------------------------------ */
/* Descriptor de configuración                                         */
/* ------------------------------------------------------------------ */

enum {
    ITF_NUM_HID = 0,
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_HID        0x81
#define EPNUM_CDC_NOTIF  0x82
#define EPNUM_CDC_OUT    0x03
#define EPNUM_CDC_IN     0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

uint8_t const desc_configuration[] = {
    /* 250 mA: SR2000 (~50) + OLED (~20) + RP2040 (~30) ya no entran
     * comodos en los 100 mA por defecto. */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),

    /*                    itf,         desc, protocolo,             largo reporte,          ep,         tamano,                   bInterval */
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 1),

    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */

char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },   /* 0: ingles (0x0409) */
    "DIY",                          /* 1: fabricante      */
    "DX4R PRO Sim Adapter",         /* 2: producto        */
    "000001",                       /* 3: serie           */
    "DX4R Consola",                 /* 4: interfaz CDC    */
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;

        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
