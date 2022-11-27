/*

Ws2812 based LED module

Copyright (C) 2020 by Indu Prakash

*/

#pragma once

#if WS2812_SUPPORT

#include <ArduinoJson.h>

#define MAX_NUM_LEDS 100

namespace WS2812Controller {

void buildDiscoveryFxList(JsonObject &json);
void setup();

} // namespace WS2812Controller

#endif