/*

UCS1903 based LED module

Copyright (C) 2020 by Indu Prakash

*/

#include "FastLED.h"

#include "api.h"
#include "mqtt.h"
#include "relay.h"
#include "ws.h"
#include <Ticker.h>

enum class PatternMode { Auto = 0, Manual = 1 };
enum class MqttData { Pattern = 0, Duration = 1, Both = 2 };

#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define MQTT_TOPIC_PATTERN "pattern"
#define MQTT_TOPIC_PATTERN_DURATION "duration"

const char *SETTING_PATTERN_DURATION = "duration";
const char *SETTING_PATTERN = "pattern";

#define MILLI_AMPS 2000
#define BRIGHTNESS 255
#define DEFAULT_FRAME_WAIT 100
#define DEFAULT_PATTERN_DURATION 30
#define PATTERN_DURATION_MIN 5
#define PATTERN_DURATION_MAX 60

#define NUM_LEDS 53
CRGB leds[NUM_LEDS];

// Top to bottom
const uint8_t left_edge_indices[] = {24, 23, 22, 17, 16, 15, 14, 13, 6, 5, 4, 3, 2, 1, 0};
const uint8_t right_edge_indices[] = {25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39};
const uint8_t NUM_LEDS_LEFT_EDGE = ARRAY_SIZE(left_edge_indices);

#define NUM_LEDS_LEVEL_ONE 4
#define NUM_LEDS_LEVEL_TWO 6
uint8_t level_one[NUM_LEDS_LEVEL_ONE] = {21, 20, 19, 18};
uint8_t level_two[NUM_LEDS_LEVEL_TWO] = {12, 11, 10, 9, 8, 7};

// Clockwise LED positions
#define NUM_LEDS_EDGE 45
uint8_t edge_indices[] = {0,  1,  2,  3, 4,  5,  6,  12, 13, 14, 15, 16, 17, 21, 22, 23, 24, 25, 26, 27, 18, 28, 29,
                          30, 31, 32, 7, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 50, 51, 52};

#define NUM_LEDS_NON_EDGE 8
uint8_t non_edge_indices[NUM_LEDS_NON_EDGE] = {21, 20, 11, 10, 9, 8, 49, 43};

void __clear();
void __fillIndicesArray(uint8_t indices_array[], uint8_t length, CRGB color);

long _blink(bool);
long _chase(bool);
long _double_chase(bool);
long _drop(bool);
long _drop_fill(bool);
long _outline(bool);
long _rainbow(bool);
long _tree(bool);
long _tree_steps(bool);

typedef long (*Pattern)(bool);
typedef long (*SimplePatternList[])(bool);

Pattern current_pattern;
PatternMode pattern_mode;
char pattern_name[16]; // sufficient to fit the longest pattern name
uint8_t current_auto_pattern_index = 0;
bool pattern_changed = false;
unsigned long last_pattern_time = 0;
uint8_t pattern_duration;
unsigned long frame_duration = 0;

const SimplePatternList ALL_PATTERNS = {_blink, _drop_fill,    _outline, _tree_steps, _rainbow,
                                        _chase, _double_chase, _drop,    _tree};
const char ALL_PATTERN_NAMES[][16] = {"blink", "drop_fill",    "outline", "tree_steps", "rainbow",
                                      "chase", "double_chase", "drop",    "tree"};

// These are listed first in ALL_PATTERNS
const SimplePatternList AUTO_PATTERNS = {_blink, _drop_fill, _outline, _tree_steps};
const uint8_t AUTO_PATTERNS_SIZE = ARRAY_SIZE(AUTO_PATTERNS);

Ticker _mqtt_ticker;

/**
 * Returns the pattern function by name. NULL is returned in case of mismatch.
 * @param name Lower cased pattern name
 */
Pattern _getPatternByName(const char *name) {
    if (strcmp(name, "blink") == 0) {
        return _blink;
    }
    if (strcmp(name, "chase") == 0) {
        return _chase;
    }
    if (strcmp(name, "double_chase") == 0) {
        return _double_chase;
    }
    if (strcmp(name, "drop") == 0) {
        return _drop;
    }
    if (strcmp(name, "drop_fill") == 0) {
        return _drop_fill;
    }
    if (strcmp(name, "outline") == 0) {
        return _outline;
    }
    if (strcmp(name, "rainbow") == 0) {
        return _rainbow;
    }
    if (strcmp(name, "tree") == 0) {
        return _tree;
    }
    if (strcmp(name, "tree_steps") == 0) {
        return _tree_steps;
    }

    return NULL;
}

/**
 * Set all LEDs to the specified color.
 * @param color
 */
void _fill(CRGB color) {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = color;
    }
}

/**
 * Set all LEDs to Black color.
 */
void _clear() { _fill(CRGB::Black); }

/**
 * Fill indices array with the specified color.
 */
void _fillIndicesArray(uint8_t indices_array[], uint8_t array_length, CRGB color) {
    for (uint8_t i = 0; i < array_length; i++) {
        leds[indices_array[i]] = color;
    }
}

/**
 * Returns the next pre-selected color.
 */
CRGB _getNextColor() {
    static uint8_t color_index = 0;
    static const CRGB colors[] = {CRGB::DarkRed, CRGB::Green, CRGB::Blue, CRGB::White};
    static const uint8_t colors_size = ARRAY_SIZE(colors);

    color_index = (color_index + 1) % colors_size;
    return colors[color_index];
}

/**
 * Chasing effect
 */
long _chase(bool newStart) {
    static uint8_t chase_index = NUM_LEDS_EDGE;
    static CRGB chase_color;

    if (newStart) {
        _clear();
        chase_color = _getNextColor();
    }

    if (chase_index == NUM_LEDS_EDGE) // first time pattern was executed
    {
        chase_index = 0;
    } else {
        leds[edge_indices[chase_index]] = CRGB::Black; // last position

        chase_index++;
        if (chase_index == NUM_LEDS_EDGE) // wrap around
        {
            chase_index = 0;
        }
    }

    leds[edge_indices[chase_index]] = chase_color;
    return DEFAULT_FRAME_WAIT;
}

/**
 * Dual chasing effect
 */
long _double_chase(bool newStart) {
    static uint8_t chase_index = NUM_LEDS_EDGE;
    uint8_t second_index;

    if (newStart) {
        _clear();
    }

    if (chase_index == NUM_LEDS_EDGE) // first time pattern was executed
    {
        chase_index = 0;
    } else {
        second_index = NUM_LEDS_EDGE - chase_index - 1;

        // last position
        leds[edge_indices[chase_index]] = leds[edge_indices[second_index]] = CRGB::Black;

        chase_index++;
        if (chase_index == NUM_LEDS_EDGE) // wrap around
        {
            chase_index = 0;
        }
    }

    second_index = NUM_LEDS_EDGE - chase_index - 1;
    leds[edge_indices[chase_index]] = leds[edge_indices[second_index]] = _getNextColor();

    return DEFAULT_FRAME_WAIT;
}

/**
 * LEDs dropping on the edges
 */
long _drop(bool newStart) {
    static uint8_t drop_index = 255;
    static CRGB drop_color;

    if (newStart) {
        _clear();
        drop_color = _getNextColor();
    }

    if (drop_index == 255) // first time pattern was executed
    {
        drop_index = 0;
    } else {
        // last position
        leds[left_edge_indices[drop_index]] = leds[right_edge_indices[drop_index]] = CRGB::Black;

        drop_index++;
        if (drop_index == NUM_LEDS_LEFT_EDGE) // wrap around
        {
            drop_index = 0;
        }
    }

    leds[left_edge_indices[drop_index]] = leds[right_edge_indices[drop_index]] = drop_color;
    return DEFAULT_FRAME_WAIT;
}

/**
 * LEDs dropping on the edges and _filling
 */
long _drop_fill(bool newStart) {
    static uint8_t drop_index;
    static CRGB drop_color;

    if (newStart) // first time pattern was executed
    {
        _clear();
        drop_index = 0;
        drop_color = _getNextColor();
    } else {
        drop_index++;

        if (drop_index == (NUM_LEDS_LEFT_EDGE + 3)) // wrap around
        {
            _clear();
            drop_index = 0;
            drop_color = _getNextColor();
        }
    }

    if (drop_index == 3) {
        _fillIndicesArray(level_one, NUM_LEDS_LEVEL_ONE, drop_color);
    } else if (drop_index == 9) {
        _fillIndicesArray(level_two, NUM_LEDS_LEVEL_TWO, drop_color);
    } else if (drop_index == 17) {
        // _fill the rest of the LEDs
        for (uint8_t i = 40; i < NUM_LEDS; i++) {
            leds[i] = drop_color;
        }

        // Wait a bit longer when the tree is complete
        return 1500;

    } else if (drop_index > 9) {
        leds[left_edge_indices[drop_index - 2]] = leds[right_edge_indices[drop_index - 2]] = drop_color;
    } else if (drop_index > 3) {
        leds[left_edge_indices[drop_index - 1]] = leds[right_edge_indices[drop_index - 1]] = drop_color;
    } else {
        leds[left_edge_indices[drop_index]] = leds[right_edge_indices[drop_index]] = drop_color;
    }

    return DEFAULT_FRAME_WAIT;
}

/**
 * Complete tree
 */
long _tree(bool newStart) {
    _fill(_getNextColor());
    return 0;
}

/**
 * Blinking tree
 */
long _blink(bool newStart) {
    static uint8_t step = 0;

    if (newStart) {
        step = 0;
    }

    _clear();

    if (step == 0) {
        _fill(_getNextColor());
    }

    step = (step + 1) % 2;
    return 1000; // Blink every 1000 ms
}

/**
 * Tree with animating outline.
 */
long _outline(bool newStart) {
    static uint8_t offset;

    if (newStart) {
        offset = 0;
    }

    uint8_t i;

    for (i = 0; i < NUM_LEDS_NON_EDGE; i++) {
        leds[non_edge_indices[i]] = CRGB::Black;
    }

    for (i = 0; i < NUM_LEDS_EDGE; i++) {
        uint8_t index = edge_indices[i];

        switch ((i + offset) % 3) {
        case 1:
            leds[index] = CRGB::Green;
            break;
        case 2:
            leds[index] = CRGB::Blue;
            break;
        default:
            leds[index] = CRGB::Red;
            break;
        }
    }

    offset = (offset + 1) % 3;
    return DEFAULT_FRAME_WAIT;
}

/**
 * Tree with steps.
 */
long _tree_steps(bool newStart) {
    static uint8_t step = 0;

    if (newStart) {
        step = 0;
    }

    uint8_t i;
    CRGB color = _getNextColor();

    if (step == 0) {
        _clear();

        for (i = 18; i < 28; i++) {
            leds[i] = color;
        }
    } else if (step == 1) {
        for (i = 7; i < 18; i++) {
            leds[i] = color;
        }
        for (i = 28; i < 33; i++) {
            leds[i] = color;
        }
    } else if (step == 2) {
        for (i = 0; i < 7; i++) {
            leds[i] = color;
        }
        for (i = 33; i < 53; i++) {
            leds[i] = color;
        }

        for (i = 44; i < 49; i++) {
            leds[i] = CRGB::Gray;
        }
    }

    step = (step + 1) % 3;
    return 750;
}

/**
 * Rainbow
 */
long _rainbow(bool newStart) {
    static uint8_t step = 0;

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        double hue = 0;
        hue = 360.0 * (((i * 256 / NUM_LEDS) + step) & 0xFF) / 255;
        CRGB color = CHSV(hue, 255, 255);
        leds[i] = color;
    }

    step = (step + 1) & 0xFF;
    return DEFAULT_FRAME_WAIT;
}

/**
 * Send MQTT updates for all values.
 * @param dataValues Data values to send
 */
void _sendMqttUpdates(MqttData dataValues) {
    if ((dataValues == MqttData::Pattern) || (dataValues == MqttData::Both)) {
        mqttSend(MQTT_TOPIC_PATTERN, pattern_mode == PatternMode::Auto ? "auto" : pattern_name);
    }

    if ((dataValues == MqttData::Duration) || (dataValues == MqttData::Both)) {
        char buffer[5];
        snprintf_P(buffer, sizeof(buffer), PSTR("%d"), pattern_duration);
        mqttSend(MQTT_TOPIC_PATTERN_DURATION, buffer);
    }
}

/**
 * Set the current pattern.
 * @param name Name of the pattern
 */
void _setPattern(const char *name) {
    // Send MQTT update even if value changed.

    if (strcmp(name, "auto") == 0) {
        if (pattern_mode != PatternMode::Auto) {
            pattern_mode = PatternMode::Auto;
            current_auto_pattern_index = 0;
            current_pattern = AUTO_PATTERNS[current_auto_pattern_index];
            strcpy_P(pattern_name, "auto");
            pattern_changed = true;
        }
    } else {
        Pattern pattern = _getPatternByName(name);

        // Fall back to "auto" if pattern is invalid
        if (pattern == NULL) {
            _setPattern("auto");
            return;
        }

        // Switch to pattern if not already in that pattern
        if (pattern != current_pattern) {
            pattern_mode = PatternMode::Manual;
            current_pattern = pattern;
            strcpy_P(pattern_name, name);
            pattern_changed = true;
        }
    }

    if (pattern_changed) {
        last_pattern_time = millis();

        setSetting(SETTING_PATTERN, name);
        saveSettings();

        _mqtt_ticker.once_ms(100, _sendMqttUpdates, MqttData::Pattern);
        DEBUG_MSG_P(PSTR("[WS2811] pattern=%s\n"), pattern_name);
    }
}

/**
 * Set the pattern duration.
 */
void _setPatternDuration(uint8_t value) {
    value = constrain(value, PATTERN_DURATION_MIN, PATTERN_DURATION_MAX);

    if (value != pattern_duration) {
        pattern_duration = value;
        setSetting(SETTING_PATTERN_DURATION, pattern_duration);
        saveSettings();

        _mqtt_ticker.once_ms(100, _sendMqttUpdates, MqttData::Duration);
        DEBUG_MSG_P(PSTR("[WS2811] duration=%d\n"), pattern_duration);
    }
}

void _ws2811Loop() {
    static unsigned long last_frame_time;

    if (!relayStatus(0)) // Strip is turned off
    {
        return;
    }

    unsigned long current_time = millis();

    // Invoke the effect method if the pattern changed or we need a new frame
    if (pattern_changed) {
        frame_duration = current_pattern(true);
        FastLED.show();
        pattern_changed = false;
        last_frame_time = current_time;
    } else if (frame_duration > 0) { // Not static
        if ((current_time - last_frame_time) > frame_duration) {
            frame_duration = current_pattern(false);
            FastLED.show();
            last_frame_time = current_time;
        }
    }

    // change patterns periodically in auto mode
    if ((pattern_mode == PatternMode::Auto) &&
        ((last_pattern_time == 0) || ((current_time - last_pattern_time) > (pattern_duration * 1000)))) {
        current_auto_pattern_index = (current_auto_pattern_index + 1) % AUTO_PATTERNS_SIZE;
        current_pattern = AUTO_PATTERNS[current_auto_pattern_index];
        pattern_changed = true;
        last_pattern_time = current_time;

        char active_pattern_name[16];
        strcpy_P(active_pattern_name, ALL_PATTERN_NAMES[current_auto_pattern_index]);
        DEBUG_MSG_P(PSTR("[WS2811] running %s\n"), active_pattern_name);
    }
}

void _ws2811TerminalSetup() {
    terminalRegisterCommand(F(MQTT_TOPIC_PATTERN), [](::terminal::CommandContext&& ctx) {
        if (ctx.argv.size() == 2) {
            _setPattern(ctx.argv[1].c_str());
            terminalOK(ctx);
        } else {
            terminalError(ctx, F("PATTERN name"));
        }        
    });

    terminalRegisterCommand(F(MQTT_TOPIC_PATTERN_DURATION), [](::terminal::CommandContext&& ctx) {
        if (ctx.argv.size() == 2) {
            _setPatternDuration(ctx.argv[1].toInt());
            terminalOK(ctx);
        } else {
            terminalError(ctx, F("DURATION value"));
        }
    });
}

void _ws2811MQTTCallback(unsigned int type, const char *topic, const char *payload) {
    if (type == MQTT_CONNECT_EVENT) {
        mqttSubscribe(MQTT_TOPIC_PATTERN);
        mqttSubscribe(MQTT_TOPIC_PATTERN_DURATION);

        _mqtt_ticker.once_ms(100, _sendMqttUpdates, MqttData::Both);
    }

    if (type == MQTT_MESSAGE_EVENT) {
        String t = mqttMagnitude((char *)topic);

        if (t.equals(MQTT_TOPIC_PATTERN)) {
            _setPattern(payload);
        } else if (t.equals(MQTT_TOPIC_PATTERN_DURATION)) {
            _setPatternDuration(atoi(payload));
        }
    }
}

void ws2811Setup() {
    FastLED.addLeds<UCS1903, WS2811_DATA_PIN, RGB>(leds, NUM_LEDS);

    // FastLED.setDither(false);
    // FastLED.setCorrection(TypicalLEDStrip);
    // FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
    FastLED.setBrightness(BRIGHTNESS);

    _fill(CRGB::Orange);
    FastLED.show();

    strcpy_P(pattern_name, getSetting(SETTING_PATTERN, "auto").c_str());
    _setPattern(pattern_name);

    _setPatternDuration(getSetting(SETTING_PATTERN_DURATION, DEFAULT_PATTERN_DURATION));

    espurnaRegisterLoop(_ws2811Loop);
    mqttRegister(_ws2811MQTTCallback);
    _ws2811TerminalSetup();
}