/*

Ws2812 based LED module

Copyright (C) 2020 by Indu Prakash

*/

#pragma once

#if WS2812_SUPPORT

#include <ArduinoJson.h>

#if !defined(WS2812_DATA_PIN)
#error "No data pin (WS2812_DATA_PIN) defined."
#endif

#define MAX_NUM_LEDS 100

#define MIN_PATTERN_DURATION       5
#define MAX_PATTERN_DURATION       60

namespace WS2812Controller {

void buildDiscoveryFxList(JsonObject &json);
void setup();

} // namespace WS2812Controller

#endif