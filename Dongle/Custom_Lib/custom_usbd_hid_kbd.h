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
#ifndef CUSTOM_USBD_HID_KBD_H__
#define CUSTOM_USBD_HID_KBD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "nrf_drv_usbd.h"
#include "app_usbd_class_base.h"
#include "app_usbd_hid_types.h"
#include "app_usbd_hid.h"
#include "app_usbd.h"
#include "app_usbd_core.h"
#include "app_usbd_descriptor.h"
#include "custom_usbd_hid_kbd_desc.h"
#include "custom_usbd_hid_kbd_internal.h"

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
typedef enum {
    CUSTOM_USBD_HID_KBD_RESERVED        = 0,
    CUSTOM_USBD_HID_KBD_ERROR_ROLL_OVER = 1,
    CUSTOM_USBD_HID_KBD_POST_FAIL       = 2,
    CUSTOM_USBD_HID_KBD_ERROR_UNDEFINED = 3,
    CUSTOM_USBD_HID_KBD_A               = 4,   /**<KBD_A               code*/
    CUSTOM_USBD_HID_KBD_B               = 5,   /**<KBD_B               code*/
    CUSTOM_USBD_HID_KBD_C               = 6,   /**<KBD_C               code*/
    CUSTOM_USBD_HID_KBD_D               = 7,   /**<KBD_D               code*/
    CUSTOM_USBD_HID_KBD_E               = 8,   /**<KBD_E               code*/
    CUSTOM_USBD_HID_KBD_F               = 9,   /**<KBD_F               code*/
    CUSTOM_USBD_HID_KBD_G               = 10,  /**<KBD_G               code*/
    CUSTOM_USBD_HID_KBD_H               = 11,  /**<KBD_H               code*/
    CUSTOM_USBD_HID_KBD_I               = 12,  /**<KBD_I               code*/
    CUSTOM_USBD_HID_KBD_J               = 13,  /**<KBD_J               code*/
    CUSTOM_USBD_HID_KBD_K               = 14,  /**<KBD_K               code*/
    CUSTOM_USBD_HID_KBD_L               = 15,  /**<KBD_L               code*/
    CUSTOM_USBD_HID_KBD_M               = 16,  /**<KBD_M               code*/
    CUSTOM_USBD_HID_KBD_N               = 17,  /**<KBD_N               code*/
    CUSTOM_USBD_HID_KBD_O               = 18,  /**<KBD_O               code*/
    CUSTOM_USBD_HID_KBD_P               = 19,  /**<KBD_P               code*/
    CUSTOM_USBD_HID_KBD_Q               = 20,  /**<KBD_Q               code*/
    CUSTOM_USBD_HID_KBD_R               = 21,  /**<KBD_R               code*/
    CUSTOM_USBD_HID_KBD_S               = 22,  /**<KBD_S               code*/
    CUSTOM_USBD_HID_KBD_T               = 23,  /**<KBD_T               code*/
    CUSTOM_USBD_HID_KBD_U               = 24,  /**<KBD_U               code*/
    CUSTOM_USBD_HID_KBD_V               = 25,  /**<KBD_V               code*/
    CUSTOM_USBD_HID_KBD_W               = 26,  /**<KBD_W               code*/
    CUSTOM_USBD_HID_KBD_X               = 27,  /**<KBD_X               code*/
    CUSTOM_USBD_HID_KBD_Y               = 28,  /**<KBD_Y               code*/
    CUSTOM_USBD_HID_KBD_Z               = 29,  /**<KBD_Z               code*/
    CUSTOM_USBD_HID_KBD_1               = 30,  /**<KBD_1               code*/
    CUSTOM_USBD_HID_KBD_2               = 31,  /**<KBD_2               code*/
    CUSTOM_USBD_HID_KBD_3               = 32,  /**<KBD_3               code*/
    CUSTOM_USBD_HID_KBD_4               = 33,  /**<KBD_4               code*/
    CUSTOM_USBD_HID_KBD_5               = 34,  /**<KBD_5               code*/
    CUSTOM_USBD_HID_KBD_6               = 35,  /**<KBD_6               code*/
    CUSTOM_USBD_HID_KBD_7               = 36,  /**<KBD_7               code*/
    CUSTOM_USBD_HID_KBD_8               = 37,  /**<KBD_8               code*/
    CUSTOM_USBD_HID_KBD_9               = 38,  /**<KBD_9               code*/
    CUSTOM_USBD_HID_KBD_0               = 39,  /**<KBD_0               code*/
    CUSTOM_USBD_HID_KBD_ENTER           = 40,  /**<KBD_ENTER           code*/
    CUSTOM_USBD_HID_KBD_ESCAPE          = 41,  /**<KBD_ESCAPE          code*/
    CUSTOM_USBD_HID_KBD_BACKSPACE       = 42,  /**<KBD_BACKSPACE       code*/
    CUSTOM_USBD_HID_KBD_TAB             = 43,  /**<KBD_TAB             code*/
    CUSTOM_USBD_HID_KBD_SPACEBAR        = 44,  /**<KBD_SPACEBAR        code*/
    CUSTOM_USBD_HID_KBD_UNDERSCORE      = 45,  /**<KBD_UNDERSCORE      code*/
    CUSTOM_USBD_HID_KBD_PLUS            = 46,  /**<KBD_PLUS            code*/
    CUSTOM_USBD_HID_KBD_OPEN_BRACKET    = 47,  /**<KBD_OPEN_BRACKET    code*/
    CUSTOM_USBD_HID_KBD_CLOSE_BRACKET   = 48,  /**<KBD_CLOSE_BRACKET   code*/
    CUSTOM_USBD_HID_KBD_BACKSLASH       = 49,  /**<KBD_BACKSLASH       code*/
    CUSTOM_USBD_HID_KBD_NONE_US         = 50,  /**<KBD_ASH             code*/
    CUSTOM_USBD_HID_KBD_SEMI_COLON      = 51,  /**<KBD_COLON           code*/
    CUSTOM_USBD_HID_KBD_QUOTE           = 52,  /**<KBD_QUOTE           code*/
    CUSTOM_USBD_HID_KBD_GRAVE_ACCENT    = 53,  /**<KBD_TILDE           code*/
    CUSTOM_USBD_HID_KBD_COMMA           = 54,  /**<KBD_COMMA           code*/
    CUSTOM_USBD_HID_KBD_PERIOD          = 55,  /**<KBD_DOT             code*/
    CUSTOM_USBD_HID_KBD_SLASH           = 56,  /**<KBD_SLASH           code*/
    CUSTOM_USBD_HID_KBD_CAPS_LOCK       = 57,  /**<KBD_CAPS_LOCK       code*/
    CUSTOM_USBD_HID_KBD_F1              = 58,  /**<KBD_F1              code*/
    CUSTOM_USBD_HID_KBD_F2              = 59,  /**<KBD_F2              code*/
    CUSTOM_USBD_HID_KBD_F3              = 60,  /**<KBD_F3              code*/
    CUSTOM_USBD_HID_KBD_F4              = 61,  /**<KBD_F4              code*/
    CUSTOM_USBD_HID_KBD_F5              = 62,  /**<KBD_F5              code*/
    CUSTOM_USBD_HID_KBD_F6              = 63,  /**<KBD_F6              code*/
    CUSTOM_USBD_HID_KBD_F7              = 64,  /**<KBD_F7              code*/
    CUSTOM_USBD_HID_KBD_F8              = 65,  /**<KBD_F8              code*/
    CUSTOM_USBD_HID_KBD_F9              = 66,  /**<KBD_F9              code*/
    CUSTOM_USBD_HID_KBD_F10             = 67,  /**<KBD_F10             code*/
    CUSTOM_USBD_HID_KBD_F11             = 68,  /**<KBD_F11             code*/
    CUSTOM_USBD_HID_KBD_F12             = 69,  /**<KBD_F12             code*/
    CUSTOM_USBD_HID_KBD_PRINTSCREEN     = 70,  /**<KBD_PRINTSCREEN     code*/
    CUSTOM_USBD_HID_KBD_SCROLL_LOCK     = 71,  /**<KBD_SCROLL_LOCK     code*/
    CUSTOM_USBD_HID_KBD_PAUSE           = 72,  /**<KBD_PAUSE           code*/
    CUSTOM_USBD_HID_KBD_INSERT          = 73,  /**<KBD_INSERT          code*/
    CUSTOM_USBD_HID_KBD_HOME            = 74,  /**<KBD_HOME            code*/
    CUSTOM_USBD_HID_KBD_PAGEUP          = 75,  /**<KBD_PAGEUP          code*/
    CUSTOM_USBD_HID_KBD_DELETE          = 76,  /**<KBD_DELETE          code*/
    CUSTOM_USBD_HID_KBD_END             = 77,  /**<KBD_END             code*/
    CUSTOM_USBD_HID_KBD_PAGEDOWN        = 78,  /**<KBD_PAGEDOWN        code*/
    CUSTOM_USBD_HID_KBD_RIGHT           = 79,  /**<KBD_RIGHT           code*/
    CUSTOM_USBD_HID_KBD_LEFT            = 80,  /**<KBD_LEFT            code*/
    CUSTOM_USBD_HID_KBD_DOWN            = 81,  /**<KBD_DOWN            code*/
    CUSTOM_USBD_HID_KBD_UP              = 82,  /**<KBD_UP              code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_NUM_LOCK = 83,  /**<KBD_KEYPAD_NUM_LOCK code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_DIVIDE   = 84,  /**<KBD_KEYPAD_DIVIDE   code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_MULTIPLY = 85,  /**<KBD_KEYPAD_MULTIPLY code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_MINUS    = 86,  /**<KBD_KEYPAD_MINUS    code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_PLUS     = 87,  /**<KBD_KEYPAD_PLUS     code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_ENTER    = 88,  /**<KBD_KEYPAD_ENTER    code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_1        = 89,  /**<KBD_KEYPAD_1        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_2        = 90,  /**<KBD_KEYPAD_2        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_3        = 91,  /**<KBD_KEYPAD_3        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_4        = 92,  /**<KBD_KEYPAD_4        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_5        = 93,  /**<KBD_KEYPAD_5        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_6        = 94,  /**<KBD_KEYPAD_6        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_7        = 95,  /**<KBD_KEYPAD_7        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_8        = 96,  /**<KBD_KEYPAD_8        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_9        = 97,  /**<KBD_KEYPAD_9        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_0        = 98,  /**<KBD_KEYPAD_0        code*/
    CUSTOM_USBD_HID_KBD_KEYPAD_PERIOD   = 99,
    CUSTOM_USBD_HID_KBD_NONUS_BACKSLASH = 100,
    CUSTOM_USBD_HID_KBD_APPLICATION     = 101,
    CUSTOM_USBD_HID_KBD_POWER           = 102,
    CUSTOM_USBD_HID_KBD_PAD_EQUAL       = 103,
    CUSTOM_USBD_HID_KBD_F13             = 104,
    CUSTOM_USBD_HID_KBD_F14             = 105,
    CUSTOM_USBD_HID_KBD_F15             = 106,
    CUSTOM_USBD_HID_KBD_F16             = 107,
    CUSTOM_USBD_HID_KBD_F17             = 108,
    CUSTOM_USBD_HID_KBD_F18             = 109,
    CUSTOM_USBD_HID_KBD_F19             = 110,
    CUSTOM_USBD_HID_KBD_F20             = 111,
    CUSTOM_USBD_HID_KBD_F21             = 112,
    CUSTOM_USBD_HID_KBD_F22             = 113,
    CUSTOM_USBD_HID_KBD_F23             = 114,
    CUSTOM_USBD_HID_KBD_F24             = 115,
    CUSTOM_USBD_HID_KBD_EXECUTE         = 116,
    CUSTOM_USBD_HID_KBD_HELP            = 117,
    CUSTOM_USBD_HID_KBD_MENU            = 118,
    CUSTOM_USBD_HID_KBD_SELECT          = 119,
    CUSTOM_USBD_HID_KBD_STOP            = 120,
    CUSTOM_USBD_HID_KBD_AGAIN           = 121,
    CUSTOM_USBD_HID_KBD_UNDO            = 122,
    CUSTOM_USBD_HID_KBD_CUT             = 123,
    CUSTOM_USBD_HID_KBD_COPY            = 124,
    CUSTOM_USBD_HID_KBD_PASTE           = 125,
    CUSTOM_USBD_HID_KBD_FIND            = 126,
    CUSTOM_USBD_HID_KBD_MUTE            = 127,
    CUSTOM_USBD_HID_KBD_VOLUME_UP       = 128,
    CUSTOM_USBD_HID_KBD_VOLUME_DOWN     = 129,
} custom_key_codes_t;

/**
 * @brief HID keyboard modifier.
 */
typedef enum {
    APP_USBD_HID_KBD_MODIFIER_NONE          = 0x00,  /**< MODIFIER_NONE        bit*/
    APP_USBD_HID_KBD_MODIFIER_LEFT_CTRL     = 0x01,  /**< MODIFIER_LEFT_CTRL   bit*/
    APP_USBD_HID_KBD_MODIFIER_LEFT_SHIFT    = 0x02,  /**< MODIFIER_LEFT_SHIFT  bit*/
    APP_USBD_HID_KBD_MODIFIER_LEFT_ALT      = 0x04,  /**< MODIFIER_LEFT_ALT    bit*/
    APP_USBD_HID_KBD_MODIFIER_LEFT_UI       = 0x08,  /**< MODIFIER_LEFT_UI     bit*/
    APP_USBD_HID_KBD_MODIFIER_RIGHT_CTRL    = 0x10,  /**< MODIFIER_RIGHT_CTRL  bit*/
    APP_USBD_HID_KBD_MODIFIER_RIGHT_SHIFT   = 0x20,  /**< MODIFIER_RIGHT_SHIFT bit*/
    APP_USBD_HID_KBD_MODIFIER_RIGHT_ALT     = 0x40,  /**< MODIFIER_RIGHT_ALT   bit*/
    APP_USBD_HID_KBD_MODIFIER_RIGHT_UI      = 0x80,  /**< MODIFIER_RIGHT_UI    bit*/
} app_usbd_hid_kbd_modifier_t;

/**
 * @brief HID keyboard LEDs.
 */
typedef enum {
    //APP_USBD_HID_KBD_LED_NUM_LOCK     = 0x00,  /**< LED_NUM_LOCK    id*/
    //APP_USBD_HID_KBD_LED_CAPS_LOCK    = 0x01,  /**< LED_CAPS_LOCK   id*/
    //APP_USBD_HID_KBD_LED_SCROLL_LOCK  = 0x02,  /**< LED_SCROLL_LOCK id*/
    //APP_USBD_HID_KBD_LED_COMPOSE      = 0x03,  /**< LED_COMPOSE     id*/
    //APP_USBD_HID_KBD_LED_KANA         = 0x04,  /**< LED_KANA        id*/
        APP_USBD_HID_KBD_LED_NUM_LOCK     = 0x01,  /**< LED_NUM_LOCK    id*/
    APP_USBD_HID_KBD_LED_CAPS_LOCK    = 0x02,  /**< LED_CAPS_LOCK   id*/
    APP_USBD_HID_KBD_LED_SCROLL_LOCK  = 0x04,  /**< LED_SCROLL_LOCK id*/
    APP_USBD_HID_KBD_LED_COMPOSE      = 0x08,  /**< LED_COMPOSE     id*/
    APP_USBD_HID_KBD_LED_KANA         = 0x10,  /**< LED_KANA        id*/
} app_usbd_hid_kbd_led_t;



#ifdef DOXYGEN
/**
 * @brief HID keyboard class instance type.
 *
 * @ref APP_USBD_CLASS_TYPEDEF
 */
typedef struct { } app_usbd_hid_kbd_t;
#else
/*lint -save -e10 -e26 -e123 -e505 */
APP_USBD_CLASS_TYPEDEF(app_usbd_hid_kbd,                        \
            APP_USBD_HID_KBD_CONFIG(0, NRF_DRV_USBD_EPIN1),     \
            APP_USBD_HID_KBD_INSTANCE_SPECIFIC_DEC,             \
            APP_USBD_HID_KBD_DATA_SPECIFIC_DEC                  \
);
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
#define APP_USBD_HID_KBD_GLOBAL_DEF(instance_name, interface_number, endpoint, user_ev_handler, subclass_boot)  \
        APP_USBD_HID_GENERIC_SUBCLASS_REPORT_DESC(keyboard_desc, APP_USBD_HID_KBD_REPORT_DSC());                \
        static const app_usbd_hid_subclass_desc_t * keyboard_descs[] = {&keyboard_desc};                        \
        APP_USBD_HID_KBD_GLOBAL_DEF_INTERNAL(instance_name,                                                     \
                                             interface_number,                                                  \
                                             endpoint,                                                          \
                                             user_ev_handler,                                                   \
                                             subclass_boot)

/**
 * @brief Helper function to get class instance from HID keyboard internals.
 *
 * @param[in] p_kbd Keyboard instance (declared by @ref APP_USBD_HID_KBD_GLOBAL_DEF).
 *
 * @return Base class instance.
 */
static inline app_usbd_class_inst_t const *
app_usbd_hid_kbd_class_inst_get(app_usbd_hid_kbd_t const * p_kbd)
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
app_usbd_hid_kbd_class_get(app_usbd_class_inst_t const * p_inst)
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
ret_code_t app_usbd_hid_kbd_modifier_state_set(app_usbd_hid_kbd_t const * p_kbd,
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
ret_code_t app_usbd_hid_kbd_key_control(app_usbd_hid_kbd_t const * p_kbd,
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
bool app_usbd_hid_kbd_led_state_get(app_usbd_hid_kbd_t const * p_kbd,
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
ret_code_t hid_kbd_on_set_protocol(app_usbd_hid_kbd_t const * p_kbd,
                                   app_usbd_hid_user_event_t ev);

/**
 * @brief Function that clears HID keyboard buffers and sends an empty report.
 *
 * @param[in] p_inst Base class instance.
 *
 * @return Standard error code.
 */
ret_code_t hid_kbd_clear_buffer(app_usbd_class_inst_t const * p_inst);

void Press(custom_key_codes_t _key);

/* Scan the Key state */
void Key_Scan();

/* Apply Debounce Filter */
void Debounce_filter();

/* Function to press a sigle key */
ret_code_t custom_key_press(app_usbd_hid_kbd_t const * p_kbd,
                            uint8_t *key_code);

uint8_t custom_LED_state_get(app_usbd_hid_kbd_t const * p_kbd);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* APP_USBD_HID_KBD_H__ */
