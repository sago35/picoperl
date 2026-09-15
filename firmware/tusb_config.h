#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Device stack on the RP2040 native USB controller */
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_PICO
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN      __attribute__ ((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE  64

/* Classes: CDC (serial console) + HID (boot keyboard, consumer control, mouse) + MSC (script drive) + MIDI */
#define CFG_TUD_CDC             1
#define CFG_TUD_HID             3
#define CFG_TUD_MSC             1
#define CFG_TUD_MIDI            1
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64
#define CFG_TUD_VENDOR          0

#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  1024
#define CFG_TUD_CDC_EP_BUFSIZE  64

#define CFG_TUD_HID_EP_BUFSIZE  16

#define CFG_TUD_MSC_EP_BUFSIZE  512

#ifdef __cplusplus
}
#endif

#endif
