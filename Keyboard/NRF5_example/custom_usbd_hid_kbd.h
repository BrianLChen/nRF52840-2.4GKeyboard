/**
 * Copyright (c) 2017 - 2021, Nordic Semiconductor ASA
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
#ifndef KEYBOARD_H__
#define KEYBOARD_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_usbd.h"
#include "app_usbd_class_base.h"
#include "app_usbd_core.h"
#include "app_usbd_descriptor.h"
#include "app_usbd_hid.h"
#include "app_usbd_hid_types.h"
#include "custom_usbd_hid_kbd_desc.h"
#include "custom_usbd_hid_kbd_internal.h"
#include "nrf_drv_usbd.h"
#include "nrfx_usbd.h"

#define bInterval 1U
#define USB_CUSTOM_HID_CONFIG_DESC_SIZE 41U

    /**
     * @defgroup app_usbd_hid_kbd USB HID keyboard
     * @ingroup app_usbd_hid
     *
     * @brief @tagAPI52840 Module with types, definitions, and API used by the HID keyboard class.
     * @{
     */

    /**
     * @brief HID keyboard codes.
     */
    typedef enum
    {
        // Modifier Button keycode + offset(8)
        MODIFIER_LEFT_CTRL = 0 + 8,   /**< MODIFIER_LEFT_CTRL   bit*/
        MODIFIER_LEFT_SHIFT = 1 + 8,  /**< MODIFIER_LEFT_SHIFT  bit*/
        MODIFIER_LEFT_ALT = 2 + 8,    /**< MODIFIER_LEFT_ALT    bit*/
        MODIFIER_LEFT_UI = 3 + 8,     /**< MODIFIER_LEFT_UI     bit*/
        MODIFIER_RIGHT_CTRL = 4 + 8,  /**< MODIFIER_RIGHT_CTRL  bit*/
        MODIFIER_RIGHT_SHIFT = 5 + 8, /**< MODIFIER_RIGHT_SHIFT bit*/
        MODIFIER_RIGHT_ALT = 6 + 8,   /**< MODIFIER_RIGHT_ALT   bit*/
        MODIFIER_RIGHT_UI = 7 + 8,    /**< MODIFIER_RIGHT_UI    bit*/
        // Keyboard Button keycode + offset(16)
        KEYBOARD_RESERVED = 0 + 16,
        KEYBOARD_ERROR_ROLL_OVER = 1 + 16,
        KEYBOARD_POST_FAIL = 2 + 16,
        KEYBOARD_ERROR_UNDEFINED = 3 + 16,
        KEYBOARD_A = 4 + 16,                /**<KBD_A               code*/
        KEYBOARD_B = 5 + 16,                /**<KBD_B               code*/
        KEYBOARD_C = 6 + 16,                /**<KBD_C               code*/
        KEYBOARD_D = 7 + 16,                /**<KBD_D               code*/
        KEYBOARD_E = 8 + 16,                /**<KBD_E               code*/
        KEYBOARD_F = 9 + 16,                /**<KBD_F               code*/
        KEYBOARD_G = 10 + 16,               /**<KBD_G               code*/
        KEYBOARD_H = 11 + 16,               /**<KBD_H               code*/
        KEYBOARD_I = 12 + 16,               /**<KBD_I               code*/
        KEYBOARD_J = 13 + 16,               /**<KBD_J               code*/
        KEYBOARD_K = 14 + 16,               /**<KBD_K               code*/
        KEYBOARD_L = 15 + 16,               /**<KBD_L               code*/
        KEYBOARD_M = 16 + 16,               /**<KBD_M               code*/
        KEYBOARD_N = 17 + 16,               /**<KBD_N               code*/
        KEYBOARD_O = 18 + 16,               /**<KBD_O               code*/
        KEYBOARD_P = 19 + 16,               /**<KBD_P               code*/
        KEYBOARD_Q = 20 + 16,               /**<KBD_Q               code*/
        KEYBOARD_R = 21 + 16,               /**<KBD_R               code*/
        KEYBOARD_S = 22 + 16,               /**<KBD_S               code*/
        KEYBOARD_T = 23 + 16,               /**<KBD_T               code*/
        KEYBOARD_U = 24 + 16,               /**<KBD_U               code*/
        KEYBOARD_V = 25 + 16,               /**<KBD_V               code*/
        KEYBOARD_W = 26 + 16,               /**<KBD_W               code*/
        KEYBOARD_X = 27 + 16,               /**<KBD_X               code*/
        KEYBOARD_Y = 28 + 16,               /**<KBD_Y               code*/
        KEYBOARD_Z = 29 + 16,               /**<KBD_Z               code*/
        KEYBOARD_1 = 30 + 16,               /**<KBD_1               code*/
        KEYBOARD_2 = 31 + 16,               /**<KBD_2               code*/
        KEYBOARD_3 = 32 + 16,               /**<KBD_3               code*/
        KEYBOARD_4 = 33 + 16,               /**<KBD_4               code*/
        KEYBOARD_5 = 34 + 16,               /**<KBD_5               code*/
        KEYBOARD_6 = 35 + 16,               /**<KBD_6               code*/
        KEYBOARD_7 = 36 + 16,               /**<KBD_7               code*/
        KEYBOARD_8 = 37 + 16,               /**<KBD_8               code*/
        KEYBOARD_9 = 38 + 16,               /**<KBD_9               code*/
        KEYBOARD_0 = 39 + 16,               /**<KBD_0               code*/
        KEYBOARD_ENTER = 40 + 16,           /**<KBD_ENTER           code*/
        KEYBOARD_ESCAPE = 41 + 16,          /**<KBD_ESCAPE          code*/
        KEYBOARD_BACKSPACE = 42 + 16,       /**<KBD_BACKSPACE       code*/
        KEYBOARD_TAB = 43 + 16,             /**<KBD_TAB             code*/
        KEYBOARD_SPACEBAR = 44 + 16,        /**<KBD_SPACEBAR        code*/
        KEYBOARD_MINUS = 45 + 16,           /**<KBD_MINUS           code*/
        KEYBOARD_PLUS = 46 + 16,            /**<KBD_PLUS            code*/
        KEYBOARD_OPEN_BRACKET = 47 + 16,    /**<KBD_OPEN_BRACKET    code*/
        KEYBOARD_CLOSE_BRACKET = 48 + 16,   /**<KBD_CLOSE_BRACKET   code*/
        KEYBOARD_BACKSLASH = 49 + 16,       /**<KBD_BACKSLASH       code*/
        KEYBOARD_NONE_US = 50 + 16,         /**<KBD_ASH             code*/
        KEYBOARD_SEMI_COLON = 51 + 16,      /**<KBD_COLON           code*/
        KEYBOARD_QUOTE = 52 + 16,           /**<KBD_QUOTE           code*/
        KEYBOARD_TILDE = 53 + 16,           /**<KBD_TILDE           code*/
        KEYBOARD_COMMA = 54 + 16,           /**<KBD_COMMA           code*/
        KEYBOARD_PERIOD = 55 + 16,          /**<KBD_DOT             code*/
        KEYBOARD_SLASH = 56 + 16,           /**<KBD_SLASH           code*/
        KEYBOARD_CAPS_LOCK = 57 + 16,       /**<KBD_CAPS_LOCK       code*/
        KEYBOARD_F1 = 58 + 16,              /**<KBD_F1              code*/
        KEYBOARD_F2 = 59 + 16,              /**<KBD_F2              code*/
        KEYBOARD_F3 = 60 + 16,              /**<KBD_F3              code*/
        KEYBOARD_F4 = 61 + 16,              /**<KBD_F4              code*/
        KEYBOARD_F5 = 62 + 16,              /**<KBD_F5              code*/
        KEYBOARD_F6 = 63 + 16,              /**<KBD_F6              code*/
        KEYBOARD_F7 = 64 + 16,              /**<KBD_F7              code*/
        KEYBOARD_F8 = 65 + 16,              /**<KBD_F8              code*/
        KEYBOARD_F9 = 66 + 16,              /**<KBD_F9              code*/
        KEYBOARD_F10 = 67 + 16,             /**<KBD_F10             code*/
        KEYBOARD_F11 = 68 + 16,             /**<KBD_F11             code*/
        KEYBOARD_F12 = 69 + 16,             /**<KBD_F12             code*/
        KEYBOARD_PRINTSCREEN = 70 + 16,     /**<KBD_PRINTSCREEN     code*/
        KEYBOARD_SCROLL_LOCK = 71 + 16,     /**<KBD_SCROLL_LOCK     code*/
        KEYBOARD_PAUSE = 72 + 16,           /**<KBD_PAUSE           code*/
        KEYBOARD_INSERT = 73 + 16,          /**<KBD_INSERT          code*/
        KEYBOARD_HOME = 74 + 16,            /**<KBD_HOME            code*/
        KEYBOARD_PAGEUP = 75 + 16,          /**<KBD_PAGEUP          code*/
        KEYBOARD_DELETE = 76 + 16,          /**<KBD_DELETE          code*/
        KEYBOARD_END = 77 + 16,             /**<KBD_END             code*/
        KEYBOARD_PAGEDOWN = 78 + 16,        /**<KBD_PAGEDOWN        code*/
        KEYBOARD_RIGHT = 79 + 16,           /**<KBD_RIGHT           code*/
        KEYBOARD_LEFT = 80 + 16,            /**<KBD_LEFT            code*/
        KEYBOARD_DOWN = 81 + 16,            /**<KBD_DOWN            code*/
        KEYBOARD_UP = 82 + 16,              /**<KBD_UP              code*/
        KEYBOARD_KEYPAD_NUM_LOCK = 83 + 16, /**<KBD_KEYPAD_NUM_LOCK code*/
        KEYBOARD_KEYPAD_DIVIDE = 84 + 16,   /**<KBD_KEYPAD_DIVIDE   code*/
        KEYBOARD_KEYPAD_MULTIPLY = 85 + 16, /**<KBD_KEYPAD_MULTIPLY code*/
        KEYBOARD_KEYPAD_MINUS = 86 + 16,    /**<KBD_KEYPAD_MINUS    code*/
        KEYBOARD_KEYPAD_PLUS = 87 + 16,     /**<KBD_KEYPAD_PLUS     code*/
        KEYBOARD_KEYPAD_ENTER = 88 + 16,    /**<KBD_KEYPAD_ENTER    code*/
        KEYBOARD_KEYPAD_1 = 89 + 16,        /**<KBD_KEYPAD_1        code*/
        KEYBOARD_KEYPAD_2 = 90 + 16,        /**<KBD_KEYPAD_2        code*/
        KEYBOARD_KEYPAD_3 = 91 + 16,        /**<KBD_KEYPAD_3        code*/
        KEYBOARD_KEYPAD_4 = 92 + 16,        /**<KBD_KEYPAD_4        code*/
        KEYBOARD_KEYPAD_5 = 93 + 16,        /**<KBD_KEYPAD_5        code*/
        KEYBOARD_KEYPAD_6 = 94 + 16,        /**<KBD_KEYPAD_6        code*/
        KEYBOARD_KEYPAD_7 = 95 + 16,        /**<KBD_KEYPAD_7        code*/
        KEYBOARD_KEYPAD_8 = 96 + 16,        /**<KBD_KEYPAD_8        code*/
        KEYBOARD_KEYPAD_9 = 97 + 16,        /**<KBD_KEYPAD_9        code*/
        KEYBOARD_KEYPAD_0 = 98 + 16,        /**<KBD_KEYPAD_0        code*/
        KEYBOARD_KEYPAD_PERIOD = 99 + 16,
        KEYBOARD_NONUS_BACKSLASH = 100 + 16,
        KEYBOARD_APPLICATION = 101 + 16,
        KEYBOARD_POWER = 102 + 16,
        KEYBOARD_PAD_EQUAL = 103 + 16,
        KEYBOARD_F13 = 104 + 16,
        KEYBOARD_F14 = 105 + 16,
        KEYBOARD_F15 = 106 + 16,
        KEYBOARD_F16 = 107 + 16,
        KEYBOARD_F17 = 108 + 16,
        KEYBOARD_F18 = 109 + 16,
        KEYBOARD_F19 = 110 + 16,
        KEYBOARD_F20 = 111 + 16,
        KEYBOARD_F21 = 112 + 16,
        KEYBOARD_F22 = 113 + 16,
        KEYBOARD_F23 = 114 + 16,
        KEYBOARD_F24 = 115 + 16,
        KEYBOARD_EXECUTE = 116 + 16,
        KEYBOARD_HELP = 117 + 16,
        KEYBOARD_MENU = 118 + 16,
        KEYBOARD_SELECT = 119 + 16,
        /* not support */
        //KEYBOARD_STOP = 120 + 16,
        //KEYBOARD_AGAIN = 121 + 16,
        //KEYBOARD_UNDO = 122 + 16,
        //KEYBOARD_CUT = 123 + 16,
        //KEYBOARD_COPY = 124 + 16,
        //KEYBOARD_PASTE = 125 + 16,
        //KEYBOARD_FIND = 126 + 16,
        //KEYBOARD_MUTE = 127 + 16,
        //KEYBOARD_VOLUME_UP = 128 + 16,
        //KEYBOARD_VOLUME_DOWN = 129 + 16,
        
        // CONSUMER_PAGE_CODE
        CONSUMER_VOLUME_INCREASE = 136,
        CONSUMER_VOLUME_DECREASE = 137,
        CONSUMER_MUTE = 138,
        CONSUMER_PLAY_PAUSE = 139,
        CONSUMER_STOP = 140,
        CONSUMER_NEXT_TRACK = 141,
        CONSUMER_PREVIOUS_TRACK = 142,
        CONSUMER_AL_CALCULATOR = 143,

        // Custom Function
        Fn = 144,
        Light_Down = 145,
        Light_UP = 146,
    } custom_key_codes_t;

    /**
     * @brief HID keyboard modifier.
     */
    typedef enum
    {
        APP_USBD_HID_KBD_MODIFIER_NONE = 0x00,        /**< MODIFIER_NONE        bit*/
        APP_USBD_HID_KBD_MODIFIER_LEFT_CTRL = 0x01,   /**< MODIFIER_LEFT_CTRL   bit*/
        APP_USBD_HID_KBD_MODIFIER_LEFT_SHIFT = 0x02,  /**< MODIFIER_LEFT_SHIFT  bit*/
        APP_USBD_HID_KBD_MODIFIER_LEFT_ALT = 0x04,    /**< MODIFIER_LEFT_ALT    bit*/
        APP_USBD_HID_KBD_MODIFIER_LEFT_UI = 0x08,     /**< MODIFIER_LEFT_UI     bit*/
        APP_USBD_HID_KBD_MODIFIER_RIGHT_CTRL = 0x10,  /**< MODIFIER_RIGHT_CTRL  bit*/
        APP_USBD_HID_KBD_MODIFIER_RIGHT_SHIFT = 0x20, /**< MODIFIER_RIGHT_SHIFT bit*/
        APP_USBD_HID_KBD_MODIFIER_RIGHT_ALT = 0x40,   /**< MODIFIER_RIGHT_ALT   bit*/
        APP_USBD_HID_KBD_MODIFIER_RIGHT_UI = 0x80,    /**< MODIFIER_RIGHT_UI    bit*/
    } app_usbd_hid_kbd_modifier_t;

    /**
     * @brief HID keyboard LEDs.
     */
    typedef enum
    {
        // APP_USBD_HID_KBD_LED_NUM_LOCK     = 0x00,  /**< LED_NUM_LOCK    id*/
        // APP_USBD_HID_KBD_LED_CAPS_LOCK    = 0x01,  /**< LED_CAPS_LOCK   id*/
        // APP_USBD_HID_KBD_LED_SCROLL_LOCK  = 0x02,  /**< LED_SCROLL_LOCK id*/
        // APP_USBD_HID_KBD_LED_COMPOSE      = 0x03,  /**< LED_COMPOSE     id*/
        // APP_USBD_HID_KBD_LED_KANA         = 0x04,  /**< LED_KANA        id*/
        APP_USBD_HID_KBD_LED_NUM_LOCK = 0x01,    /**< LED_NUM_LOCK    id*/
        APP_USBD_HID_KBD_LED_CAPS_LOCK = 0x02,   /**< LED_CAPS_LOCK   id*/
        APP_USBD_HID_KBD_LED_SCROLL_LOCK = 0x04, /**< LED_SCROLL_LOCK id*/
        APP_USBD_HID_KBD_LED_COMPOSE = 0x08,     /**< LED_COMPOSE     id*/
        APP_USBD_HID_KBD_LED_KANA = 0x10,        /**< LED_KANA        id*/
    } app_usbd_hid_kbd_led_t;


#ifdef DOXYGEN
    /**
     * @brief HID keyboard class instance type.
     *
     * @ref APP_USBD_CLASS_TYPEDEF
     */
    typedef struct
    {
    } app_usbd_hid_kbd_t;
#else
/*lint -save -e10 -e26 -e123 -e505 */
APP_USBD_CLASS_TYPEDEF(app_usbd_hid_kbd,
    APP_USBD_HID_KBD_CONFIG(0, NRF_DRV_USBD_EPIN1),
    APP_USBD_HID_KBD_INSTANCE_SPECIFIC_DEC,
    APP_USBD_HID_KBD_DATA_SPECIFIC_DEC);
/*lint -restore*/
#endif

/**
 * @brief Global definition of app_usbd_hid_kbd_t class.
 *
 * @param instance_name     Name of global instance.
 * @param interface_number  Unique interface index.
 * @param endpoint          Input endpoint (@ref nrf_drv_usbd_ep_t).
 * @param user_ev_handler   User event handler (optional parameter: NULL might be passed here).
 * @param subclass_boot     Subclass boot (@ref app_usbd_hid_subclass_t).
 *
 * Example class definition:
 * @code
   APP_USBD_HID_KBD_GLOBAL_DEF(my_awesome_kbd, 0, NRF_DRV_USBD_EPIN1, NULL, APP_USBD_HID_SUBCLASS_BOOT);
 * @endcode
 */
#define APP_USBD_HID_KBD_GLOBAL_DEF(instance_name, interface_number, endpoint, user_ev_handler, subclass_boot) \
    APP_USBD_HID_GENERIC_SUBCLASS_REPORT_DESC(keyboard_desc, APP_USBD_HID_KBD_REPORT_DSC());                   \
    static const app_usbd_hid_subclass_desc_t *keyboard_descs[] = {&keyboard_desc};                            \
    APP_USBD_HID_KBD_GLOBAL_DEF_INTERNAL(instance_name,                                                        \
        interface_number,                                                                                      \
        endpoint,                                                                                              \
        user_ev_handler,                                                                                       \
        subclass_boot)

    /**
     * @brief Helper function to get class instance from HID keyboard internals.
     *
     * @param[in] p_kbd Keyboard instance (declared by @ref APP_USBD_HID_KBD_GLOBAL_DEF).
     *
     * @return Base class instance.
     */
    static inline app_usbd_class_inst_t const *
    app_usbd_hid_kbd_class_inst_get(app_usbd_hid_kbd_t const *p_kbd)
    {
        return &p_kbd->base;
    }

    /**
     * @brief Helper function to get HID keyboard from base class instance.
     *
     * @param[in] p_inst Base class instance.
     *
     * @return HID keyboard class handle.
     */
    static inline app_usbd_hid_kbd_t const *
    app_usbd_hid_kbd_class_get(app_usbd_class_inst_t const *p_inst)
    {
        return (app_usbd_hid_kbd_t const *)p_inst;
    }

    /**
     * @brief Set HID keyboard modifier state.
     *
     * @param[in] p_kbd     Keyboard instance (declared by @ref APP_USBD_HID_KBD_GLOBAL_DEF).
     * @param[in] modifier  Type of modifier.
     * @param[in] state     State, true active, false inactive.
     *
     * @return Standard error code.
     */
    ret_code_t app_usbd_hid_kbd_modifier_state_set(app_usbd_hid_kbd_t const *p_kbd,
        app_usbd_hid_kbd_modifier_t modifier,
        bool state);

    /**
     * @brief Press/release HID keyboard key.
     *
     * @param[in] p_kbd     Keyboard instance (declared by @ref APP_USBD_HID_KBD_GLOBAL_DEF).
     * @param[in] key       Keyboard key code.
     * @param[in] press     True -> key press, false -> release.
     *
     * @return Standard error code.
     */
    ret_code_t app_usbd_hid_kbd_key_control(app_usbd_hid_kbd_t const *p_kbd,
        custom_key_codes_t key,
        bool press);
    /**
     * @brief HID Keyboard LEDs state get.
     *
     * @param[in] p_kbd Keyboard instance (declared by @ref APP_USBD_HID_KBD_GLOBAL_DEF).
     * @param[in] led   LED code.
     *
     * @retval true     LED is set.
     * @retval false    LED is not set.
     */
    bool app_usbd_hid_kbd_led_state_get(app_usbd_hid_kbd_t const *p_kbd,
        app_usbd_hid_kbd_led_t led);

    /**
     * @brief Function handling SET_PROTOCOL command.
     *
     *
     * @param[in] p_kbd         Keyboard instance.
     * @param[in] ev            User event.
     *
     * @return Standard error code.
     */
    ret_code_t hid_kbd_on_set_protocol(app_usbd_hid_kbd_t const *p_kbd,
        app_usbd_hid_user_event_t ev);

    /**
     * @brief Function that clears HID keyboard buffers and sends an empty report.
     *
     * @param[in] p_inst Base class instance.
     *
     * @return Standard error code.
     */
    ret_code_t hid_kbd_clear_buffer(app_usbd_class_inst_t const *p_inst);

    void Press(custom_key_codes_t _key);

    /* Scan the Key state */
    void Key_Scan();

    /* Apply Debounce Filter */
    void Debounce_filter();

    void remap();

    /* Function to press a sigle key */
    ret_code_t custom_key_press(app_usbd_hid_kbd_t const *p_kbd, custom_key_codes_t _key, bool press);
    ret_code_t custom_media_press(app_usbd_hid_kbd_t const *p_kbd, uint8_t _key, bool press);
    uint8_t custom_LED_state_get(app_usbd_hid_kbd_t const * p_kbd);
    void custom_LED_state_off(app_usbd_hid_kbd_t const *p_kbd);
    ret_code_t buffer_clear(app_usbd_hid_kbd_t const *p_kbd, uint8_t report_id);
    ret_code_t send(app_usbd_hid_kbd_t const *p_kbd);
    
    /* Directly Send report Buffer */
    ret_code_t KBD_Send(app_usbd_hid_kbd_t const *p_kbd, uint8_t *rep_buff);
    ret_code_t Dongle_send(app_usbd_hid_kbd_t const *p_kbd, uint8_t *key_code);


    /** @} */

#ifdef __cplusplus
}
#endif

#endif /* APP_USBD_HID_KBD_H__ */