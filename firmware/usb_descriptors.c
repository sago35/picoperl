/* USB descriptors: composite CDC (serial console) + HID boot keyboard + HID consumer control
 * + HID mouse + MSC (script drive). */
#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>

#ifndef PICOPERL_USB_VID
#define PICOPERL_USB_VID 0xCafe      /* TinyUSB's test VID; set your own for a real product */
#endif
#ifndef PICOPERL_USB_PID
#define PICOPERL_USB_PID 0x4010
#endif
#ifndef PICOPERL_USB_MANUFACTURER
#define PICOPERL_USB_MANUFACTURER "picoperl"
#endif
#ifndef PICOPERL_USB_PRODUCT
#define PICOPERL_USB_PRODUCT "picoperl keyboard"
#endif

/* ---- device ---- */
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,          /* IAD */
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = PICOPERL_USB_VID,
    .idProduct          = PICOPERL_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

/* ---- HID report descriptor: plain boot keyboard, no report ID ---- */
static const uint8_t desc_hid_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
/* second HID interface: consumer control (media keys), 16-bit usage, no report ID */
static const uint8_t desc_hid_report_consumer[] = { TUD_HID_REPORT_DESC_CONSUMER() };
/* third HID interface: boot mouse (buttons, x, y, wheel, pan), no report ID */
static const uint8_t desc_hid_report_mouse[] = { TUD_HID_REPORT_DESC_MOUSE() };

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    if (instance == 1) return desc_hid_report_consumer;
    if (instance == 2) return desc_hid_report_mouse;
    return desc_hid_report;
}

/* ---- configuration ---- */
enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_HID, ITF_NUM_HID_CONSUMER, ITF_NUM_HID_MOUSE, ITF_NUM_MSC,
       ITF_NUM_MIDI, ITF_NUM_MIDI_STREAMING, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_HID_IN    0x83
#define EPNUM_MSC_OUT   0x04
#define EPNUM_MSC_IN    0x84
#define EPNUM_HID2_IN   0x85
#define EPNUM_HID3_IN   0x86
#define EPNUM_MIDI_OUT  0x05
#define EPNUM_MIDI_IN   0x87

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + 3 * TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 5, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report), EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_CONSUMER, 7, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report_consumer), EPNUM_HID2_IN, CFG_TUD_HID_EP_BUFSIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_MOUSE, 8, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_report_mouse), EPNUM_HID3_IN, CFG_TUD_HID_EP_BUFSIZE, 5),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 6, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 9, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return desc_configuration; }

/* ---- strings ---- */
static const char *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },   /* 0: English (US) */
    PICOPERL_USB_MANUFACTURER,      /* 1 */
    PICOPERL_USB_PRODUCT,           /* 2 */
    NULL,                           /* 3: serial (chip id) */
    "picoperl console",             /* 4 */
    "picoperl keyboard",            /* 5 */
    "picoperl drive",               /* 6 */
    "picoperl media keys",          /* 7 */
    "picoperl mouse",               /* 8 */
    "picoperl midi",                /* 9 */
};

static uint16_t desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;
    if (index == 0) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index == 3) {
        char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        pico_get_unique_board_id_string(serial, sizeof serial);
        chr_count = (uint8_t)strlen(serial);
        for (uint8_t i = 0; i < chr_count; i++) desc_str[1 + i] = serial[i];
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 32) chr_count = 32;
        for (uint8_t i = 0; i < chr_count; i++) desc_str[1 + i] = str[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
