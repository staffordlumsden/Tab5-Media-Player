/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Stafford Lumsden
 */

#include "physical_keyboard.hpp"

#include "bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {

constexpr uint32_t kHidEnter = 0x28;
constexpr uint32_t kHidEsc = 0x29;
constexpr uint32_t kHidBackspace = 0x2A;
constexpr uint32_t kHidTab = 0x2B;
constexpr uint32_t kHidHome = 0x4A;
constexpr uint32_t kHidPageUp = 0x4B;
constexpr uint32_t kHidDelete = 0x4C;
constexpr uint32_t kHidEnd = 0x4D;
constexpr uint32_t kHidPageDown = 0x4E;
constexpr uint32_t kHidRight = 0x4F;
constexpr uint32_t kHidLeft = 0x50;
constexpr uint32_t kHidDown = 0x51;
constexpr uint32_t kHidUp = 0x52;
constexpr uint32_t kNoKey = UINT32_MAX;

struct Printable {
    char normal;
    char shifted;
};

constexpr Printable kPrintable[0x39] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {'a','A'}, {'b','B'}, {'c','C'}, {'d','D'}, {'e','E'},
    {'f','F'}, {'g','G'}, {'h','H'}, {'i','I'}, {'j','J'},
    {'k','K'}, {'l','L'}, {'m','M'}, {'n','N'}, {'o','O'},
    {'p','P'}, {'q','Q'}, {'r','R'}, {'s','S'}, {'t','T'},
    {'u','U'}, {'v','V'}, {'w','W'}, {'x','X'}, {'y','Y'},
    {'z','Z'}, {'1','!'}, {'2','@'}, {'3','#'}, {'4','$'},
    {'5','%'}, {'6','^'}, {'7','&'}, {'8','*'}, {'9','('},
    {'0',')'}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {' ',' '},
    {'-','_'}, {'=','+'}, {'[','{'}, {']','}'}, {'\\','|'},
    {0, 0}, {';',':'}, {'\'','"'}, {'`','~'}, {',','<'},
    {'.','>'}, {'/','?'},
};

const char *TAG = "physical_keyboard";
QueueHandle_t s_keys;
lv_group_t *s_group;
bool s_connected;
uint32_t s_last_key;
bool s_release_pending;

uint32_t translate(const bsp_keyboard_event_t &event) {
    if (event.type != BSP_KEYBOARD_EVENT_HID || event.hid.keycode == 0) {
        return kNoKey;
    }
    const uint32_t code = event.hid.keycode;
    const bool shift = (event.hid.modifiers &
                        (BSP_KEYBOARD_MOD_LSHIFT | BSP_KEYBOARD_MOD_RSHIFT)) != 0;
    switch (code) {
    case kHidEnter: return LV_KEY_ENTER;
    case kHidEsc: return LV_KEY_ESC;
    case kHidBackspace: return LV_KEY_BACKSPACE;
    case kHidTab: return shift ? LV_KEY_PREV : LV_KEY_NEXT;
    case kHidHome: return LV_KEY_HOME;
    case kHidPageUp: return LV_KEY_PREV;
    case kHidDelete: return LV_KEY_DEL;
    case kHidEnd: return LV_KEY_END;
    case kHidPageDown: return LV_KEY_NEXT;
    case kHidRight: return LV_KEY_RIGHT;
    case kHidLeft: return LV_KEY_LEFT;
    case kHidDown: return LV_KEY_DOWN;
    case kHidUp: return LV_KEY_UP;
    default: break;
    }
    if (code >= 0x04 && code < 0x39) {
        const char value = shift ? kPrintable[code].shifted : kPrintable[code].normal;
        return value ? static_cast<uint32_t>(static_cast<unsigned char>(value)) : kNoKey;
    }
    return kNoKey;
}

void keyboard_event(const bsp_keyboard_event_t *event, void *) {
    if (!event) return;
    if (event->type == BSP_KEYBOARD_EVENT_CONNECTED) {
        s_connected = true;
        ESP_LOGI(TAG, "Tab5 Keyboard connected");
        return;
    }
    if (event->type == BSP_KEYBOARD_EVENT_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "Tab5 Keyboard disconnected");
        return;
    }
    const uint32_t key = translate(*event);
    if (key != kNoKey && s_keys) xQueueSend(s_keys, &key, 0);
}

void read_keypad(lv_indev_t *, lv_indev_data_t *data) {
    if (s_release_pending) {
        data->key = s_last_key;
        data->state = LV_INDEV_STATE_RELEASED;
        s_release_pending = false;
        return;
    }
    uint32_t key = kNoKey;
    if (s_keys && xQueueReceive(s_keys, &key, 0) == pdTRUE) {
        s_last_key = key;
        s_release_pending = true;
        data->key = key;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }
    data->key = s_last_key;
    data->state = LV_INDEV_STATE_RELEASED;
}

}  // namespace

void physical_keyboard_start() {
    if (s_group) return;
    s_keys = xQueueCreate(32, sizeof(uint32_t));
    s_group = lv_group_create();
    if (!s_keys || !s_group) {
        ESP_LOGE(TAG, "cannot create keyboard input queue");
        return;
    }
    lv_group_set_default(s_group);
    lv_group_set_wrap(s_group, true);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, read_keypad);
    lv_indev_set_group(indev, s_group);
    lv_indev_set_display(indev, lv_display_get_default());

    bsp_keyboard_set_event_cb(keyboard_event, nullptr);
    s_connected = bsp_keyboard_is_connected();
    ESP_LOGI(TAG, "Tab5 Keyboard input %s", s_connected ? "ready" : "waiting for connection");
}

bool physical_keyboard_connected() {
    return s_connected;
}
