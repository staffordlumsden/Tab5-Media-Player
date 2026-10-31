/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Stafford Lumsden
 */

#pragma once

#include <cstdint>

#include "lvgl.h"

/* Route the M5Stack Tab5 clip-on keyboard through one LVGL keypad group.
 * Start after the display and widget font are initialized. */
void physical_keyboard_start();
bool physical_keyboard_connected();
