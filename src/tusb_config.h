#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
  #error CFG_TUSB_MCU must be defined
#endif

// Enable host stack with pio-usb if Pico-PIO-USB
#define CFG_TUH_ENABLED     1
#define CFG_TUH_RPI_PIO_USB 1

#if CFG_TUSB_MCU == OPT_MCU_LPC43XX || CFG_TUSB_MCU == OPT_MCU_LPC18XX || CFG_TUSB_MCU == OPT_MCU_MIMXRT10XX
  #define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST | OPT_MODE_HIGH_SPEED)
#else
  #define CFG_TUSB_RHPORT0_MODE       OPT_MODE_HOST
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

// CFG_TUSB_DEBUG is defined by compiler in DEBUG build
#undef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG           1

// USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
// Tinyusb use follows macros to declare transferring memory so that they can be put
// into those specific section.
// e.g
// - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
// - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// CONFIGURATION
//--------------------------------------------------------------------

// Size of buffer to hold descriptors and other data used for enumeration
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUH_HUB                 1
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 4 // Typical keyboard + mouse device can have 3-4 HID interfaces
#define CFG_TUH_MIDI                0 // There will be at most one MIDIStreaming Interface descriptor
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

// Max device support (excluding hub device)
#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 5 : 1) // Hub typically has 4 ports

//------------- HID -------------//
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

#ifdef __cplusplus
 }
#endif

#endif // _TUSB_CONFIG_H_
