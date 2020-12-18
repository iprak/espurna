#include "FastLED.h"

#include "api.h"
#include "mqtt.h"
#include "ws.h"

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define MQTT_TOPIC_WS2811 "pattern"
#define DATA_PIN D5

#define MILLI_AMPS 2000
#define BRIGHTNESS 255
#define DEFAULT_FRAME_WAIT 100
#define PATTERN_DURATION 10
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

// Clockwise
uint8_t edge_indices[] = {0,  1,  2,  3, 4,  5,  6,  12, 13, 14, 15, 16, 17, 21, 22, 23, 24, 25, 26, 27, 18, 28, 29,
                          30, 31, 32, 7, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 50, 51, 52};
const uint8_t NUM_LEDS_EDGE = ARRAY_SIZE(edge_indices);

void clear();
void fillIndicesArray(uint8_t indices_array[], uint8_t length, CRGB color);

long blink(bool);
long chase(bool);
long double_chase(bool);
long drop(bool);
long drop_fill(bool);
long outline(bool);
long rainbow(bool);
long tree(bool);
long tree_steps(bool);

typedef long (*Pattern)(bool);
typedef long (*SimplePatternList[])(bool);

Pattern currentPattern;
char currentPatternName[16]; // sufficient to fit the longest pattern name
uint8_t currentAutoPatternIndex = 0;
bool auto_mode;
bool pattern_changed = false;
unsigned long last_pattern_time;

const SimplePatternList ALL_PATTERNS = {blink, drop_fill, rainbow, chase,     double_chase,
                                        drop,  outline,   tree,    tree_steps};
const char ALL_PATTERN_NAMES[][16] = {"blink", "drop_fill", "rainbow", "chase",     "double_chase",
                                      "drop",  "outline",   "tree",    "tree_steps"};
const SimplePatternList AUTO_PATTERNS = {blink, drop_fill, rainbow}; // The first 3 of the ALL_PATTERNS
const uint8_t AUTO_PATTERNS_SIZE = ARRAY_SIZE(AUTO_PATTERNS);

void clear() {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
    }
}

void fillIndicesArray(uint8_t indices_array[], uint8_t length, CRGB color) {
    for (uint8_t i = 0; i < length; i++) {
        leds[indices_array[i]] = color;
    }
}

CRGB getNextColor() {
    static uint8_t color_index = 0;
    static const CRGB OutlineColors[] = {CRGB::DarkRed, CRGB::Green, CRGB::Blue};

    color_index = (color_index + 1) % 3;
    CRGB color = OutlineColors[color_index];
    return color;
}

long chase(bool newStart) {
    static uint8_t chase_index = NUM_LEDS_EDGE;
    static CRGB chase_color;

    if (newStart) {
        clear();
        chase_color = getNextColor();
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

long double_chase(bool newStart) {
    static uint8_t chase_index = NUM_LEDS_EDGE;
    uint8_t second_index;

    if (newStart) {
        clear();
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
    leds[edge_indices[chase_index]] = leds[edge_indices[second_index]] = getNextColor();

    return DEFAULT_FRAME_WAIT;
}

long drop(bool newStart) {
    static uint8_t drop_index = 255;
    static CRGB drop_color;

    if (newStart) {
        clear();
        drop_color = getNextColor();
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

long drop_fill(bool newStart) {
    static uint8_t drop_index;
    static CRGB drop_color;

    if (newStart) // first time pattern was executed
    {
        clear();
        drop_index = 0;
        drop_color = getNextColor();
    } else {
        drop_index++;

        if (drop_index == (NUM_LEDS_LEFT_EDGE + 3)) // wrap around
        {
            clear();
            drop_index = 0;
            drop_color = getNextColor();
        }
    }

    if (drop_index == 3) {
        fillIndicesArray(level_one, NUM_LEDS_LEVEL_ONE, drop_color);
    } else if (drop_index == 9) {
        fillIndicesArray(level_two, NUM_LEDS_LEVEL_TWO, drop_color);
    } else if (drop_index == 17) {
        // Fill the rest of the LEDs
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

long tree(bool newStart) {
    clear();
    CRGB color = getNextColor();

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = color;
    }

    return 0;
}

long blink(bool newStart) {
    static uint8_t step = 0;

    if (newStart) {
        step = 0;
    }

    clear();

    if (step == 0) {
        CRGB color = getNextColor();

        for (uint8_t i = 0; i < NUM_LEDS; i++) {
            leds[i] = color;
        }
    }

    step = (step + 1) % 2;
    return 1000; // Blink every 1000 ms
}

long outline(bool newStart) {
    clear();
    fillIndicesArray(edge_indices, NUM_LEDS_EDGE, getNextColor());
    return 0;
}

long tree_steps(bool newStart) {
    static uint8_t step = 0;
    static CRGB tree_steps_color;

    if (newStart) {
        step = 0;
    }

    uint8_t i;

    if (step == 0) {
        clear();
        tree_steps_color = getNextColor();

        for (i = 18; i < 28; i++) {
            leds[i] = tree_steps_color;
        }
    } else if (step == 1) {
        for (i = 7; i < 18; i++) {
            leds[i] = tree_steps_color;
        }
        for (i = 28; i < 33; i++) {
            leds[i] = tree_steps_color;
        }
    } else if (step == 2) {
        for (i = 0; i < 7; i++) {
            leds[i] = tree_steps_color;
        }
        for (i = 33; i < 53; i++) {
            leds[i] = tree_steps_color;
        }

        for (i = 44; i < 49; i++) {
            leds[i] = CRGB::Gray;
        }
    }

    step = (step + 1) % 3;
    return 1000;
}

long rainbow(bool newStart) {
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

void setPatternMode(const char *pattern) { // uint8_t result
    if (strcmp(pattern, "auto") == 0) {
        if (!auto_mode) {
            auto_mode = true;
            currentAutoPatternIndex = 0;
            currentPattern = AUTO_PATTERNS[currentAutoPatternIndex];
            pattern_changed = true;
            last_pattern_time = millis();

            strcpy_P(currentPatternName, ALL_PATTERN_NAMES[currentAutoPatternIndex]);
            mqttSend(MQTT_TOPIC_WS2811, "auto");

            DEBUG_MSG_P(PSTR("[WS2811] auto mode\n"));
        }
    } else {

        bool using_defaultPattern = false;
        Pattern newPattern;

        if (strcmp(pattern, "blink") == 0) {
            newPattern = blink;
        } else if (strcmp(pattern, "chase") == 0) {
            newPattern = chase;
        } else if (strcmp(pattern, "double_chase") == 0) {
            newPattern = double_chase;
        } else if (strcmp(pattern, "drop") == 0) {
            newPattern = drop;
        } else if (strcmp(pattern, "drop_fill") == 0) {
            newPattern = drop_fill;
        } else if (strcmp(pattern, "outline") == 0) {
            newPattern = outline;
        } else if (strcmp(pattern, "rainbow") == 0) {
            newPattern = rainbow;
        } else if (strcmp(pattern, "tree") == 0) {
            newPattern = tree;
        } else if (strcmp(pattern, "tree_steps") == 0) {
            newPattern = tree_steps;
        } else {
            newPattern = blink;
            using_defaultPattern = true;
        }

        if (newPattern != currentPattern) {
            currentPattern = newPattern;

            if (using_defaultPattern) {
                strcpy_P(currentPatternName, "blink");
            } else {
                strcpy_P(currentPatternName, pattern);
            }

            auto_mode = false; // manual
            pattern_changed = true;

            mqttSend(MQTT_TOPIC_WS2811, currentPatternName);
            DEBUG_MSG_P(PSTR("[WS2811] %s\n"), currentPatternName);
        }
    }
}

void ws2811Loop() {
    static unsigned long last_frame_time;
    static unsigned long frame_duration = 0;

    unsigned long current_time = millis();

    if (pattern_changed) {
        frame_duration = currentPattern(true);
        FastLED.show();
        pattern_changed = false;
        last_frame_time = current_time;
    } else if (frame_duration > 0) { // Not static
        if ((current_time - last_frame_time) > frame_duration) {
            frame_duration = currentPattern(false);
            FastLED.show();
            last_frame_time = current_time;
        }
    }

    // change patterns periodically in auto mode
    if (auto_mode && ((current_time - last_pattern_time) > (PATTERN_DURATION * 1000))) {
        currentAutoPatternIndex = (currentAutoPatternIndex + 1) % AUTO_PATTERNS_SIZE;
        currentPattern = AUTO_PATTERNS[currentAutoPatternIndex];
        pattern_changed = true;
        last_pattern_time = current_time;

        strcpy_P(currentPatternName, ALL_PATTERN_NAMES[currentAutoPatternIndex]);
        mqttSend(MQTT_TOPIC_WS2811, currentPatternName);

        DEBUG_MSG_P(PSTR("[WS2811] pattern %s\n"), currentPatternName);
    }
}

void ws2811TerminalSetup() {
    terminalRegisterCommand(F(MQTT_TOPIC_WS2811), [](const terminal::CommandContext &ctx) {
        // int result = ctx.argv[1].toInt();
        setPatternMode(ctx.argv[1].c_str());
        terminalOK();
    });
}

void ws2811MQTTCallback(unsigned int type, const char *topic, const char *payload) {
    if (type == MQTT_CONNECT_EVENT) {
        mqttSubscribe(MQTT_TOPIC_WS2811);
        mqttSend(MQTT_TOPIC_WS2811, currentPatternName);
    }

    if (type == MQTT_MESSAGE_EVENT) {
        String t = mqttMagnitude((char *)topic);

        if (t.equals(MQTT_TOPIC_WS2811)) {
            // setPatternMode(atoi(payload));
            setPatternMode(payload);
        }
    }
}

void ws2811Setup() {
    FastLED.addLeds<UCS1903, DATA_PIN, RGB>(leds, NUM_LEDS);

    // FastLED.setDither(false);
    FastLED.setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);

    clear();
    FastLED.show();
    setPatternMode("auto"); // Start with the first auto pattern

    espurnaRegisterLoop(ws2811Loop);
    mqttRegister(ws2811MQTTCallback);
    ws2811TerminalSetup();
}

// https://github.com/FastLED/FastLED/blob/d5ddf40d3f3731adb36c122abba29cbf80654be3/src/colorpalettes.h
// https://github.com/FastLED/FastLED/blob/dcbf39933f51a2a0e4dfa0a2b3af4f50040df5c9/examples/Fire2012WithPalette/Fire2012WithPalette.ino
// https://github.com/FastLED/FastLED/blob/dcbf39933f51a2a0e4dfa0a2b3af4f50040df5c9/examples/DemoReel100/DemoReel100.ino
// https://gist.github.com/headphones81/d3474359f1b3744ce203b16905f93d5f