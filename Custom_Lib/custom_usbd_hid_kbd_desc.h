/**
 * Copyright (c) 2016 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef CUSTOM_USBD_HID_KBD_DESC_H__
#define CUSTOM_USBD_HID_KBD_DESC_H__

#include "nordic_common.h"
#include "custom_usbd_hid_kbd_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

//#define rep_byte = 32
#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_VOLUME 2


/**
 * @defgroup app_usbd_hid_kbd_desc USB HID keyboard descriptors
 * @ingroup app_usbd_hid_kbd
 *
 * @brief @tagAPI52840 Module with types, definitions, and API used by the HID keyboard class.
 * @{
 */

/**
 * @brief Initializer of interface descriptor for HID keyboard class.
 *
 * @param interface_number  Interface number.
 */
#define APP_USBD_HID_KBD_INTERFACE_DSC(interface_number)            \
        APP_USBD_HID_INTERFACE_DSC(interface_number,                \
                                   1,                               \
                                   APP_USBD_HID_SUBCLASS_BOOT,      \
                                   APP_USBD_HID_PROTO_KEYBOARD)

/**
 * @brief Initializer of HID descriptor for HID keyboard class.
 *
 * @param ...   Report descriptor item.
 */
#define APP_USBD_HID_KBD_HID_DSC(...)       \
        APP_USBD_HID_HID_DSC(__VA_ARGS__)
/**
 * @brief Initializer of endpoint descriptor for HID keyboard class.
 *
 * @param endpoint  Endpoint number.
 */
#define APP_USBD_HID_KBD_EP_DSC(endpoint)   \
        APP_USBD_HID_EP_DSC(endpoint, 8, 1)


/**
 * @brief Example of USB HID keyboard report descriptor.
 *
 */
/* working + calculator button */
#define APP_USBD_HID_KBD_REPORT_DSC() {                                                     \
        0x05, 0x01,                    /* USAGE_PAGE (Generic Desktop)                   */ \
        0x09, 0x06,                    /* USAGE (Keyboard)                               */ \
        0xa1, 0x01,                    /* COLLECTION (Application)                       */ \
        0x85, REPORT_ID_KEYBOARD,      /* Report ID = 1                                  */ \
        /* Modifier */ \
        0x05, 0x07,                    /*   USAGE_PAGE (KeyCodes)                        */ \
        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
        0x95, 0x08,                    /*   REPORT_COUNT (8)                             */ \
        0x19, 0xe0,                    /*   USAGE_MINIMUM (Keyboard LeftControl)         */ \
        0x29, 0xe7,                    /*   USAGE_MAXIMUM (Keyboard Right GUI)           */ \
        0x15, 0x00,                    /*   LOGICAL_MINIMUM (0)                          */ \
        0x25, 0x01,                    /*   LOGICAL_MAXIMUM (1)                          */ \
        0x81, 0x02,                    /*   INPUT (Data,Var,Abs)                         */ \
        /* Keys */ \
        0x95, 0x78,                    /*   REPORT_COUNT (120)    KEYMAP                 */ \
        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
        0x15, 0x00,                    /*   LOGICAL_MINIMUM (0)                          */ \
        0x25, 0x01,                    /*   LOGICAL_MAXIMUM (1)                          */ \
        0x05, 0x07,                    /*   USAGE_PAGE (KeyCodes)                        */ \
        0x19, 0x00,                    /*   USAGE_MINIMUM (0)                            */ \
        0x29, 0x78,                    /*   USAGE_MAXIMUM ()                             */ \
        0x81, 0x60,                    /*   IUTPUT (Data,Var,Abs)                        */ \
        \
        /* LEDs */ \
        0x95, 0x05,                    /*   REPORT_COUNT (5)      LEDs                   */ \
        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
        0x05, 0x08,                    /*   USAGE_PAGE (LEDs)                            */ \
        0x19, 0x01,                    /*   USAGE_MINIMUM (1)                            */ \
        0x29, 0x05,                    /*   USAGE_MAXIMUM (5)                            */ \
        0x91, 0x02,                    /*   ONPUT (Data,Var,Abs)                         */ \
        \
        0x95, 0x01,                    /*   REPORT_COUNT (1)                             */ \
        0x75, 0x03,                    /*   REPORT_SIZE (3)                              */ \
        0x91, 0x03,                    /*   OUTPUT (Cnst,Var,Abs)                        */ \
        \
        0xc0, \
        \
        /* Volume */ \
        0x05, 0x0c,  /* Usage Page (Consumer) */ \
        0x09, 0x01,  /* USAGE */ \
        0xa1, 0x01,  /* COLLECTION (Application)                       */ \
        0x85, 0x02,  /* Report ID = 2 */ \
        0x05, 0x0c,  /* Usage Page (Consumer) */ \
        0x15, 0x00,  /* logic min */ \
        0x25, 0x01,  /* logic max */ \
        0x75, 0x01,  /* rep size 1 */ \
        0x95, 0x08,  /* rep count (07) */ \
        0x09, 0xe9,  /* usage volume increase */ \
        0x09, 0xea,  /* usage volume decrease */ \
        0x09, 0xe2,  /* usage mute */ \
        0x09, 0xcd,  /* uasge play/pause */ \
        0x09, 0xb5,  /* uasge next track */ \
        0x09, 0xb6,  /* uasge Previous track */ \
        0x09, 0xb7,  /* uasge stop */ \
        0x0a, 0x92, 0x01, \
        0x81, 0x02,  /* output */ \
        \
        0xc0,                           /* END_COLLECTION                                 */\
}


/* Working desc with volume control */
//#define APP_USBD_HID_KBD_REPORT_DSC() {                                                     \
//        0x05, 0x01,                    /* USAGE_PAGE (Generic Desktop)                   */ \
//        0x09, 0x06,                    /* USAGE (Keyboard)                               */ \
//        0xa1, 0x01,                    /* COLLECTION (Application)                       */ \
//        0x85, REPORT_ID_KEYBOARD,      /* Report ID = 1                                  */ \
//        /* Modifier */ \
//        0x05, 0x07,                    /*   USAGE_PAGE (KeyCodes)                        */ \
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
//        0x95, 0x08,                    /*   REPORT_COUNT (8)                             */ \
//        0x19, 0xe0,                    /*   USAGE_MINIMUM (Keyboard LeftControl)         */ \
//        0x29, 0xe7,                    /*   USAGE_MAXIMUM (Keyboard Right GUI)           */ \
//        0x15, 0x00,                    /*   LOGICAL_MINIMUM (0)                          */ \
//        0x25, 0x01,                    /*   LOGICAL_MAXIMUM (1)                          */ \
//        0x81, 0x02,                    /*   INPUT (Data,Var,Abs)                         */ \
//        /* Keys */ \
//        0x95, 0x78,                    /*   REPORT_COUNT (120)    KEYMAP                 */ \
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
//        0x15, 0x00,                    /*   LOGICAL_MINIMUM (0)                          */ \
//        0x25, 0x01,                    /*   LOGICAL_MAXIMUM (1)                          */ \
//        0x05, 0x07,                    /*   USAGE_PAGE (KeyCodes)                        */ \
//        0x19, 0x00,                    /*   USAGE_MINIMUM (0)                            */ \
//        0x29, 0x78,                    /*   USAGE_MAXIMUM ()                             */ \
//        0x81, 0x60,                    /*   IUTPUT (Data,Var,Abs)                        */ \
//        \
//        /* LEDs */ \
//        0x95, 0x05,                    /*   REPORT_COUNT (5)      LEDs                   */ \
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
//        0x05, 0x08,                    /*   USAGE_PAGE (LEDs)                            */ \
//        0x19, 0x01,                    /*   USAGE_MINIMUM (1)                            */ \
//        0x29, 0x05,                    /*   USAGE_MAXIMUM (5)                            */ \
//        0x91, 0x02,                    /*   ONPUT (Data,Var,Abs)                         */ \
//        \
//        0x95, 0x01,                    /*   REPORT_COUNT (1)                             */ \
//        0x75, 0x03,                    /*   REPORT_SIZE (3)                              */ \
//        0x91, 0x03,                    /*   OUTPUT (Cnst,Var,Abs)                        */ \
//        \
//        0xc0, \
//        \
//        /* Volume */ \
//        0x05, 0x0c,  /* Usage Page (Consumer) */ \
//        0x09, 0x01,  /* USAGE */ \
//        0xa1, 0x01,  /* COLLECTION (Application)                       */ \
//        0x85, 0x02,  /* Report ID = 2 */ \
//        0x05, 0x0c,  /* Usage Page (Consumer) */ \
//        0x15, 0x00,  /* logic min */ \
//        0x25, 0x01,  /* logic max */ \
//        0x75, 0x01,  /* rep size 1 */ \
//        0x95, 0x08,  /* rep count (07) */ \
//        0x09, 0xe9,  /* usage volume increase */ \
//        0x09, 0xea,  /* usage volume decrease */ \
//        0x09, 0xe2,  /* usage mute */ \
//        0x09, 0xcd,  /* uasge play/pause */ \
//        0x09, 0xb5,  /* uasge next track */ \
//        0x09, 0xb6,  /* uasge Previous track */ \
//        0x09, 0xb7,  /* uasge stop */ \
//        0x09, 0xb0,  /* usage */ \
//        0x81, 0x02,  /* output */ \
//        \
//        0xc0,                           /* END_COLLECTION                                 */\
//}

//#define APP_USBD_HID_KBD_REPORT_DSC() {                                                    \
//        0x05, 0x01,                    /* USAGE_PAGE (Generic Desktop)                   */\
//        0x09, 0x06,                    /* USAGE (Keyboard)                               */\
//        0xa1, 0x01,                    /* COLLECTION (Application)                       */\
//        /* Modifier */ \
//        0x05, 0x07,                    /*   USAGE_PAGE (KeyCodes)                        */\
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */\
//        0x95, 0x08,                    /*   REPORT_COUNT (8)                             */\
//        0x19, 0xe0,                    /*   USAGE_MINIMUM (Keyboard LeftControl)         */\
//        0x29, 0xe7,                    /*   USAGE_MAXIMUM (Keyboard Right GUI)           */\
//        0x15, 0x00,                    /*   LOGICAL_MINIMUM (0)                          */\
//        0x25, 0x01,                    /*   LOGICAL_MAXIMUM (1)                          */\
//        0x81, 0x02,                    /*   INPUT (Data,Var,Abs)                         */\
//        /* Keys */ \
//        0x95, (keyboard_rep_byte - 1)*8,                    /*   REPORT_COUNT (120)    KEYMAP                 */\
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */\
//        0x15, 0x00,                    /*   LOGICAL_MINIMUM (0)                          */\
//        0x25, 0x01,                    /*   LOGICAL_MAXIMUM (1)                          */\
//        0x05, 0x07,                    /*   USAGE_PAGE (KeyCodes)                        */\
//        0x19, 0x00,                    /*   USAGE_MINIMUM (0)                            */\
//        0x29, (keyboard_rep_byte - 2)*8,                    /*   USAGE_MAXIMUM ()                             */\
//        0x81, 0x02,                    /*   IUTPUT (Data,Var,Abs)                        */\
//        /* LEDs */\
//        0x95, 0x05,                    /*   REPORT_COUNT (5)      LEDs                   */\
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */\
//        0x05, 0x08,                    /*   USAGE_PAGE (LEDs)                            */\
//        0x19, 0x01,                    /*   USAGE_MINIMUM (1)                            */\
//        0x29, 0x05,                    /*   USAGE_MAXIMUM (5)                            */\
//        0x91, 0x02,                    /*   ONPUT (Data,Var,Abs)                         */\
//        \
//        0x95, 0x01,                    /*   REPORT_COUNT (1)                             */\
//        0x75, 0x03,                    /*   REPORT_SIZE (3)                              */\
//        0x91, 0x03,                    /*   OUTPUT (Cnst,Var,Abs)                        */\
//        \
//        0xc0                           /* END_COLLECTION                                 */\
//}


///* testing - using Report Collection */
//#define APP_USBD_HID_KBD_REPORT_DSC() {                                                     \
//        0xa1, 0x01,                    /* COLLECTION (Report)                       */ \
//        0x05, 0x01,                    /* USAGE_PAGE (Generic Desktop Control)                   */ \
//        0x09, 0x06,                    /* USAGE (Keyboard)                               */ \
//        0x05, 0x0c,                    /* USAGE_PAGE (Generic Desktop Control)                   */ \
//        0x85, 0x01, \
//        0x75, 0x12,                    /*   REPORT_SIZE (1)                              */ \
//        0x95, 0x08,                    /*   REPORT_COUNT (8)                             */ \
//        0x81, 0x02,                    /*   INPUT (Data,Var,Abs)                         */ \
//        /* Modifier */ \
//        0x05, 0x07,                    /*   USAGE_PAGE (KeyCodes)                        */ \
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
//        0x95, 0x08,                    /*   REPORT_COUNT (8)                             */ \
//        0x19, 0xe0,                    /*   USAGE_MINIMUM (Keyboard LeftControl)         */ \
//        0x29, 0xe7,                    /*   USAGE_MAXIMUM (Keyboard Right GUI)           */ \
//        0x15, 0x00,                    /*   LOGICAL_MINIMUM (0)                          */ \
//        0x25, 0x01,                    /*   LOGICAL_MAXIMUM (1)                          */ \
//        0x81, 0x02,                    /*   INPUT (Data,Var,Abs)                         */ \
//        /* Keys */ \
//        0x95, 0x78,                    /*   REPORT_COUNT (120)    KEYMAP                 */ \
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
//        0x15, 0x00,                    /*   LOGICAL_MINIMUM (0)                          */ \
//        0x25, 0x01,                    /*   LOGICAL_MAXIMUM (1)                          */ \
//        0x05, 0x07,                    /*   USAGE_PAGE (KeyCodes)                        */ \
//        0x19, 0x00,                    /*   USAGE_MINIMUM (0)                            */ \
//        0x29, 0x78,                    /*   USAGE_MAXIMUM ()                             */ \
//        0x81, 0x02,                    /*   IUTPUT (Data,Var,Abs)                        */ \
//        \
//        /* Volume */ \
//        0x05, 0x0c,  /* Usage Page (Consumer) */ \
//        0x15, 0x00,  /* logic min */ \
//        0x25, 0x01,  /* logic max */ \
//        0x75, 0x01,  /* rep size 1 */ \
//        0x95, 0x08,  /* rep count (07) */ \
//        0x09, 0xe9,  /* usage volume increase */ \
//        0x09, 0xea,  /* usage volume decrease */ \
//        0x09, 0xe2,  /* usage mute */ \
//        0x09, 0xcd,  /* uasge play/pause */ \
//        0x09, 0xb5,  /* uasge next track */ \
//        0x09, 0xb6,  /* uasge Previous track */ \
//        0x09, 0xb7,  /* uasge stop */ \
//        0x09, 0xb0,  /* usage */ \
//        0x81, 0x02,  /* output */ \
//        \
//        /* LEDs */ \
//        0x95, 0x05,                    /*   REPORT_COUNT (5)      LEDs                   */ \
//        0x75, 0x01,                    /*   REPORT_SIZE (1)                              */ \
//        0x05, 0x08,                    /*   USAGE_PAGE (LEDs)                            */ \
//        0x19, 0x01,                    /*   USAGE_MINIMUM (1)                            */ \
//        0x29, 0x05,                    /*   USAGE_MAXIMUM (5)                            */ \
//        0x91, 0x02,                    /*   ONPUT (Data,Var,Abs)                         */ \
//        \
//        0x95, 0x01,                    /*   REPORT_COUNT (1)                             */ \
//        0x75, 0x03,                    /*   REPORT_SIZE (3)                              */ \
//        0x91, 0x03,                    /*   OUTPUT (Cnst,Var,Abs)                        */ \
//        \
//        0xc0,                           /* END_COLLECTION                                 */\
//}

/* testing - using Report Collection */
//#define APP_USBD_HID_KBD_REPORT_DSC() {                                                     \
//    0x05, 0x01,                    /* Usage Page (Generic Desktop) */ \
//    0x09, 0x06,                    /* Usage (Keyboard) */ \
//    0xA1, 0x01,                    /* Collection (Application) */ \
//    \
//    0x05, 0x07,                    /*   Usage Page (Keyboard) */ \
//    0x19, 0xE0,                    /*   Usage Minimum (224) */ \
//    0x29, 0xE7,                    /*   Usage Maximum (231) */ \
//    0x15, 0x00,                    /*   Logical Minimum (0) */ \
//    0x25, 0x01,                    /*   Logical Maximum (1) */ \
//    0x75, 0x01,                    /*   Report Size (1) */ \
//    0x95, 0x08,                    /*   Report Count (8) */ \
//    0x81, 0x02,                    /*   Input (Data, Variable, Absolute) */ \
//    \
//    0x95, 0x01,                    /*   Report Count (1) */ \
//    0x75, 0x08,                    /*   Report Size (8) */ \
//    0x81, 0x03,                    /*   Input (Constant, Variable, Absolute) */ \
//    \
//    0x95, 0x78,                    /*   Report Count (6) */ \
//    0x75, 0x01,                    /*   Report Size (8) */ \
//    0x15, 0x00,                    /*   Logical Minimum (0) */ \
//    0x25, 0x01,                    /*   Logical Maximum (255) */ \
//    0x05, 0x07,                    /*   Usage Page (Keyboard) */ \
//    0x19, 0x00,                    /*   Usage Minimum (0) */ \
//    0x29, 0x78,                    /*   Usage Maximum (255) */ \
//    0x81, 0x02,                    /*   Input (Data, Array) */ \
//    
//    0x95, 0x01,                    /*   Report Count (1) */ \
//    0x75, 0x08,                    /*   Report Size (8) */ \
//    0x15, 0x00,                    /*   Logical Minimum (0) */ \
//    0x25, 0x01,                    /*   Logical Maximum (1) */ \
//    0x05, 0x0C,                    /*   Usage Page (Consumer Devices) */ \
//    0x09, 0xE9,                    /*   Usage (Volume Increment) */ \
//    0x09, 0xEA,                    /*   Usage (Volume Decrement) */ \
//    0x81, 0x02,                    /*   Input (Data, Variable, Absolute) */ \
//    \
//    0x95, 0x02,                    /*   Report Count (2) */ \
//    0x75, 0x08,                    /*   Report Size (8) */ \
//    0x15, 0x00,                    /*   Logical Minimum (0) */ \
//    0x25, 0x01,                    /*   Logical Maximum (1) */ \
//    0x81, 0x01,                    /*   Input (Constant, Variable, Absolute) */ \
//    \
//    0x95, 0x01,                    /*   Report Count (1) */ \
//    0x75, 0x08,                    /*   Report Size (8) */ \
//    0x05, 0x08,                    /*   Usage Page (LEDs) */ \
//    0x19, 0x01,                    /*   Usage Minimum (1) */ \
//    0x29, 0x08,                    /*   Usage Maximum (8) */ \
//    0x91, 0x02,                    /*   Output (Data, Variable, Absolute) */ \
//    \
//    0xC0                           /* End Collection */ \
//}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* APP_USBD_HID_KBD_DESC_H__ */
