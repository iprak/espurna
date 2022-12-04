/*

WS2812 based LED module

Copyright (C) 2022 by Indu Prakash

*/

#include "espurna.h"

#if WS2812_SUPPORT

#include "FastLED.h"
#include "api.h"
#include "mqtt.h"
#include "ota.h"
#include "ws.h"
#include "ws2812.h"
#include <ArduinoJson.h>

namespace WS2812Controller {

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

PROGMEM_STRING(COMMAND_WS2812, "WS2812");
PROGMEM_STRING(COMMAND_WS2812_NUM_LEDS, "WS2812.numLEDs");
PROGMEM_STRING(COMMAND_WS2812_ON, "WS2812.on");
PROGMEM_STRING(COMMAND_WS2812_PATTERN, "WS2812.pattern");
PROGMEM_STRING(COMMAND_WS2812_PLAYLIST, "WS2812.playlist");
PROGMEM_STRING(COMMAND_WS2812_PATTERNDURATION, "WS2812.patternduration");

PROGMEM_STRING(SETTING_ON, "ws2812.o");
PROGMEM_STRING(SETTING_NUM_LEDS, "ws2812.n");
PROGMEM_STRING(SETTING_PATTERN, "ws2812.p");
PROGMEM_STRING(SETTING_PLAYLIST, "ws2812.pl");
PROGMEM_STRING(SETTING_PATTERNDURATION, "ws2812.pd");

#define DEFAULT_NUM_LEDS 50
#define DEFAULT_DELAY 20
#define DEFAULT_PATTERN 0
#define DEFAULT_PLAYLIST false
#define DEFAULT_PATTERN_DURATION 30 // seconds
#define DEFAULT_ON_STATE true
#define DEFAULT_BRIGHTNESS 200

#define DEBUG_LOG_PERIOD 15000

/// @brief Total patters
uint8_t totalPatterns;
/// @brief Current pattern
uint8_t currentPatternIndex = DEFAULT_PATTERN;
/// @brief First display call for a pattern
bool currentPatternFirstCall;
/// @brief Totla number of LEDs
uint8_t numLEDs = 0;
/// @brief LED array
CRGB leds[MAX_NUM_LEDS];
/// @brief LEDs on
bool lightOn;
/// @brief Pattern playlist enabled
bool playlistEnabled;
/// @brief Pattern display duration in playlist mode (seconds)
uint8_t patternDuration;
/// @brief Our timer
espurna::timer::SystemTimer _timer;
/// @brief Pattern start time
unsigned long pattern_start_time = 0;
/// @brief Current loop time
unsigned long current_time = 0;
// unsigned long last_msg_time = 0;
// bool showLoopMessage = false;

/// @brief Last frame render time for a pattern
unsigned long pattern_last_frame_time;

void nextPattern();

/// @brief Pattern generation functions
namespace Patterns {

typedef void (*PatternList[])(bool);

uint8_t current_pattern_hue = 0;
uint8_t current_pattern_led_index = 0;

void blink(bool);
void dropFill(bool);
void outline(bool);
void rainbow(bool);
void rainbowLoop(bool);
void random(bool);
void redBlueBounce(bool);
void rotatingRedBlue(bool);
void propeller(bool);

const PatternList patternFns = {blink,       dropFill, outline,       propeller,      rainbow,
                                rainbowLoop, random,   redBlueBounce, rotatingRedBlue};

const char *patternNames[] = {"blink",       "dropFill", "outline",       "propeller",      "rainbow",
                              "rainbowLoop", "random",   "redBlueBounce", "rotatingRedBlue"};


/// @brief Clear all LEDs
void clearAll() { FastLED.clear(true); }

uint8_t getOppositeIndex(uint8_t i) {
    uint8_t topIndex = numLEDs / 2;
    uint8_t invertIndex = i + topIndex;
    if (i >= topIndex) {
        invertIndex = (i + topIndex) % numLEDs;
    }
    return invertIndex;
}

CRGB getNextColor() {
    static uint8_t color_index = 0;
    static const CRGB colors[] = {CRGB::DarkRed, CRGB::Green, CRGB::Blue, CRGB::White};
    static const uint8_t colors_size = ARRAY_SIZE(colors);

    color_index = (color_index + 1) % colors_size;
    return colors[color_index];
}

void fillArray(CRGB color) {
    for (uint8_t i = 0; i < numLEDs; i++) {
        leds[i] = color;
    }
}

void rainbow(bool firstTime) {
    if (firstTime) {
        current_pattern_hue = 0;
    }

    if ((current_time - pattern_last_frame_time) >= 15) {
        pattern_last_frame_time = current_time;

        current_pattern_hue++;
        fill_rainbow(leds, numLEDs, current_pattern_hue);
        FastLED.show();
    }
}

void rainbowLoop(bool firstTime) {
    if (firstTime) {
        current_pattern_hue = 0;
    }

    if ((current_time - pattern_last_frame_time) >= 25) {
        pattern_last_frame_time = current_time;

        current_pattern_led_index++;
        current_pattern_hue += 10;
        if (current_pattern_led_index >= numLEDs) {
            current_pattern_led_index = 0;
        }
        if (current_pattern_hue > 255) {
            current_pattern_hue = 0;
        }
        leds[current_pattern_led_index] = CHSV(current_pattern_hue, 255, 255);

        FastLED.show();
    }
}

void random(bool firstTime) {
    if ((current_time - pattern_last_frame_time) >= 25) {
        pattern_last_frame_time = current_time;

        leds[::random(0, numLEDs)] = CHSV(::random(0, 255), 255, 255);
        FastLED.show();
    }
}

void redBlueBounce(bool firstTime) {
    static bool bouncedRight = false;

    if ((current_time - pattern_last_frame_time) >= 25) {
        pattern_last_frame_time = current_time;

        if (!bouncedRight) {
            current_pattern_led_index++;
            if (current_pattern_led_index == numLEDs) {
                bouncedRight = true;
                current_pattern_led_index--;
            }
        }
        if (bouncedRight) {
            current_pattern_led_index--;
            if (current_pattern_led_index == 0) {
                bouncedRight = false;
            }
        }
        for (uint8_t i = 0; i < numLEDs; i++) {
            if (current_pattern_led_index == i) {
                leds[i] = CHSV(HUE_BLUE, 255, 255);
            } else {
                leds[i] = CHSV(0, 0, 0);
            }

            uint8_t otherIndex = getOppositeIndex(current_pattern_led_index);
            leds[otherIndex] = CHSV(HUE_RED, 255, 255);
        }

        FastLED.show();
    }
}

void rotatingRedBlue(bool firstTime) {
    if ((current_time - pattern_last_frame_time) >= 25) {
        pattern_last_frame_time = current_time;

        current_pattern_led_index++;
        if (current_pattern_led_index >= numLEDs) {
            current_pattern_led_index = 0;
        }
        leds[current_pattern_led_index] = CHSV(HUE_RED, 255, 255);
        leds[getOppositeIndex(current_pattern_led_index)] = CHSV(HUE_BLUE, 255, 255);

        FastLED.show();
    }
}

void propeller(bool firstTime) {
    if ((current_time - pattern_last_frame_time) >= 25) {
        pattern_last_frame_time = current_time;

        current_pattern_led_index++;
        int N3 = int(numLEDs / 3);
        int N12 = int(numLEDs / 12);

        for (int i = 0; i < N3; i++) {
            int j0 = (current_pattern_led_index + i + numLEDs - N12) % numLEDs;
            int j1 = (j0 + N3) % numLEDs;
            int j2 = (j1 + N3) % numLEDs;
            leds[j0] = CHSV(HUE_RED, 255, 255);
            leds[j1] = CHSV(HUE_GREEN, 255, 255);
            leds[j2] = CHSV(HUE_BLUE, 255, 255);
        }

        FastLED.show();
    }
}

void blink(bool firstTime) {
    static uint8_t step = 0;

    if (firstTime) {
        step = 0;
        FastLED.showColor(getNextColor());
    } else if ((current_time - pattern_last_frame_time) >= 1000) {
        pattern_last_frame_time = current_time;
        step = (step + 1) % 2;

        if (step == 0) {
            FastLED.showColor(getNextColor());
        } else {
            FastLED.showColor(CRGB::Black);
        }
    }
}

/// @brief Start filling from the middle to the ends. Restart with a new color.
/// @param firstTime
void dropFill(bool firstTime) {
    static uint8_t left = 0;
    static uint8_t right = 0;
    bool show = true;

    if (firstTime) {
        left = right = numLEDs / 2;
        fillArray(CRGB::Black);
        leds[left] = getNextColor();
    } else if ((current_time - pattern_last_frame_time) >= 100) {
        left--;
        right++;

        if (left < 0 || right > (numLEDs - 1)) {                    // We have reached the edge
            if ((current_time - pattern_last_frame_time) >= 1500) { // Wait a bit longer upon completion
                left = right = numLEDs / 2;
                pattern_last_frame_time = current_time;
                fillArray(CRGB::Black);
                leds[left] = getNextColor();
            } else {
                show = false;
            }
        } else {
            leds[left] = leds[left + 1]; // Copy the color
            leds[right] = leds[right - 1];
            pattern_last_frame_time = current_time;
        }

        if (show) {
            FastLED.show();
        }
    }
}

/// @brief Fill with alternating colors shifting by a pixel
/// @param firstTime
void outline(bool firstTime) {
    static uint8_t offset;

    if (firstTime) {
        offset = 0;
    }

    if ((current_time - pattern_last_frame_time) >= 100) {
        pattern_last_frame_time = current_time;
        offset = (offset + 1) % 3;

        for (uint8_t i = 0; i < numLEDs; i++) {
            switch ((i + offset) % 3) {
            case 1:
                leds[i] = CRGB::Green;
                break;
            case 2:
                leds[i] = CRGB::Blue;
                break;
            default:
                leds[i] = CRGB::Red;
                break;
            }
        }

        FastLED.show();
    }
}

} // namespace Patterns

const char *getPatternName(uint8_t index) {
    return (index < 0 || index >= totalPatterns) ? "" : Patterns::patternNames[index];
}

void mqttSendLightOn() { mqttSend(MQTT_TOPIC_WS2812_LIGHT, lightOn ? "1" : "0"); }
void mqttSendPattern() { mqttSend(MQTT_TOPIC_WS2812_PATTERN, getPatternName(currentPatternIndex)); }
void mqttSendNumLEDs() { mqttSend(MQTT_TOPIC_WS2812_NUMLEDS, String(numLEDs).c_str()); }
void mqttSendPlaylist() { mqttSend(MQTT_TOPIC_WS2812_PLAYLIST, playlistEnabled ? "1" : "0"); }
void mqttSendPatternDuration() { mqttSend(MQTT_TOPIC_WS2812_PATTERNDURATION, String(patternDuration).c_str()); }

void initialize(uint8_t countValue, bool onValue, uint8_t patternValue) {
    bool needToSaveSettings = false;

    totalPatterns = ARRAY_SIZE(Patterns::patternNames);

    if (lightOn != onValue) {
        lightOn = onValue;
        setSetting(SETTING_ON, lightOn);
        needToSaveSettings = true;
    }

    countValue = constrain(countValue, 0, MAX_NUM_LEDS);
    if (numLEDs != countValue) {
        numLEDs = countValue;
        setSetting(SETTING_NUM_LEDS, numLEDs);
        needToSaveSettings = true;
    }

    patternValue = constrain(patternValue, 0, totalPatterns - 1);
    if (currentPatternIndex != patternValue) {
        currentPatternIndex = patternValue;
        setSetting(SETTING_PATTERN, currentPatternIndex);
        needToSaveSettings = true;
    }

    if (needToSaveSettings) {
        saveSettings();
    }

    FastLED.addLeds<WS2812, WS2812_DATA_PIN, RGB>(leds, numLEDs);

    if (lightOn) {
        FastLED.setBrightness(DEFAULT_BRIGHTNESS);
        FastLED.show();
    } else {
        Patterns::clearAll(); // All off
    }

    DEBUG_MSG_P(PSTR("[WS2812] initialized numLEDS=%d, totalPatterns=%d, on=%d, currentPattern=%d\n"), numLEDs,
                totalPatterns, lightOn, currentPatternIndex);
}

void loop() {
    if (!lightOn || Update.isRunning() || numLEDs == 0)
        return;

    current_time = millis();

    if (playlistEnabled) {
        if (pattern_start_time == 0) {
            pattern_start_time = current_time;
        } else if (abs(current_time - pattern_start_time) > (patternDuration * 1000)) {
            pattern_start_time = current_time;
            nextPattern();
        }
    } else {
        pattern_start_time = 0;
    }

    // if (!showLoopMessage || (abs(current_time - last_msg_time) > DEBUG_LOG_PERIOD)) {
    //     showLoopMessage = true;
    //     last_msg_time = current_time;
    //      DEBUG_MSG_P(PSTR("[WS2812] %s\n"), getPatternName(currentPatternIndex));
    // }

    if (currentPatternIndex >= 0 && currentPatternIndex < totalPatterns) {
        auto patternFx = Patterns::patternFns[currentPatternIndex];
        patternFx(currentPatternFirstCall);
    }

    currentPatternFirstCall = false;
}

/// @brief Sets new pattern. Sends MQTT update.
/// @param newValue
void setPattern(uint8_t newValue) {
    newValue = constrain(newValue, 0, totalPatterns - 1);
    DEBUG_MSG_P(PSTR("[WS2812] setPattern(%d) current=%d\n"), newValue, currentPatternIndex);
    if (currentPatternIndex != newValue) {
        currentPatternIndex = newValue;

        currentPatternFirstCall = true;
        pattern_last_frame_time = 0; // Reset pattern frame time

        setSetting(SETTING_PATTERN, currentPatternIndex);
        saveSettings();
        mqttSendPattern();
    }
}

/// @brief Sets new pattern by name. Sends MQTT update.
/// @param patternName
void setPatternByName(const char *patternName) {
    DEBUG_MSG_P(PSTR("[WS2812] setPatternByName(%s)\n"), patternName);
    for (uint8_t i = 0; i < totalPatterns; i++) {
        if (strcmp(Patterns::patternNames[i], patternName) == 0) {
            setPattern(i);
            return;
        }
    }
}

/// @brief Turn strip on/off. Sends MQTT update.
/// @param newState
void turnOnOff(bool newState) {
    DEBUG_MSG_P(PSTR("[WS2812] turnOnOff(%d) state=%d\n"), newState, lightOn);
    if (lightOn != newState) {
        lightOn = newState;
        setSetting(SETTING_ON, lightOn);
        saveSettings();
        mqttSendLightOn();

        if (!newState) {
            Patterns::clearAll();
        }
    }
}

/// @brief Enable/disable playlist. Sends MQTT update.
/// @param newState
void enablePlaylist(bool newState) {
    DEBUG_MSG_P(PSTR("[WS2812] enablePlaylist(%d) state=%d\n"), newState, playlistEnabled);
    if (playlistEnabled != newState) {
        playlistEnabled = newState;
        setSetting(SETTING_PLAYLIST, playlistEnabled);
        saveSettings();
        mqttSendPlaylist();
    }
}

/// @brief Sets new patternDuration. Sends MQTT update.
/// @param newValue
void setPatternDuration(uint8_t newValue) {
    newValue = constrain(newValue, MIN_PATTERN_DURATION, MAX_PATTERN_DURATION);
    DEBUG_MSG_P(PSTR("[WS2812] patternDuration(%d) state=%d\n"), newValue, patternDuration);
    if (patternDuration != newValue) {
        patternDuration = newValue;
        setSetting(SETTING_PATTERNDURATION, patternDuration);
        saveSettings();
        mqttSendPatternDuration();
    }
}

void nextPattern() {
    uint8_t newPattern = currentPatternIndex + 1;
    if (newPattern == totalPatterns) {
        newPattern = 0;
    }

    setPattern(newPattern);
}

void buildDiscoveryFxList(JsonObject &json) {
    JsonArray &effects = json.createNestedArray(F("fx_list"));
    for (uint8_t i = 0; i < totalPatterns; i++) {
        effects.add(Patterns::patternNames[i]);
    }
}

/// @brief Commands related functions
namespace commands {

static void showInfo(::terminal::CommandContext &ctx) {
    ctx.output.printf_P(PSTR("WS2812: %s, %d leds, %s, playList %s, patternDuration=%d\n"),
                        getPatternName(WS2812Controller::currentPatternIndex), numLEDs, lightOn ? "on" : "off",
                        playlistEnabled ? "on" : "off", patternDuration);
}

static void onInfo(::terminal::CommandContext &&ctx) { showInfo(ctx); }

static void onPattern(::terminal::CommandContext &&ctx) {
    if (ctx.argv.size() == 2) {
        if (ctx.argv[1].equals(F("next"))) {
            nextPattern();
            terminalOK(ctx);
        } else {
            const auto result = parseUnsigned(ctx.argv[1], 10);
            if (result.ok) {
                setPattern(result.value);
                showInfo(ctx);
                terminalOK(ctx);
            } else {
                terminalError(ctx, F("invalid argument"));
            }
        }
    } else {
        terminalError(ctx, F("WS2812.pattern patternIndex"));
    }
}

static void onNumLEDs(::terminal::CommandContext &&ctx) {
    if (ctx.argv.size() == 2) {
        const auto result = parseUnsigned(ctx.argv[1], 10);
        if (result.ok) {
            uint8_t newCount = constrain(result.value, 0, MAX_NUM_LEDS);
            if (newCount != numLEDs) {
                initialize(newCount, lightOn, currentPatternIndex);
                mqttSendNumLEDs();
            }

            showInfo(ctx);
            terminalOK(ctx);
        } else {
            terminalError(ctx, F("invalid argument"));
        }
    } else {
        terminalError(ctx, F("WS2812.numLEDs count"));
    }
}

static void onLightOn(::terminal::CommandContext &&ctx) {
    if (ctx.argv.size() == 2) {
        const auto result = parseUnsigned(ctx.argv[1], 10);
        if (result.ok) {
            turnOnOff(result.value == 1);
            showInfo(ctx);
            terminalOK(ctx);
        } else {
            terminalError(ctx, F("invalid argument"));
        }
    } else {
        terminalError(ctx, F("WS2812.on 1/0"));
    }
}

static void onPlaylist(::terminal::CommandContext &&ctx) {
    if (ctx.argv.size() == 2) {
        const auto result = parseUnsigned(ctx.argv[1], 10);
        if (result.ok) {
            enablePlaylist(result.value == 1);
            showInfo(ctx);
            terminalOK(ctx);
        } else {
            terminalError(ctx, F("invalid argument"));
        }
    } else {
        terminalError(ctx, F("WS2812.playlist 1/0"));
    }
}

static void onPatternDuration(::terminal::CommandContext &&ctx) {
    if (ctx.argv.size() == 2) {
        const auto result = parseUnsigned(ctx.argv[1], 10);
        if (result.ok) {
            setPatternDuration(result.value);
            showInfo(ctx);
            terminalOK(ctx);
        } else {
            terminalError(ctx, F("invalid argument"));
        }
    } else {
        terminalError(ctx, F("WS2812.patternDuration count"));
    }
}

} // namespace commands

void setupMQTT() {
    mqttSubscribe(MQTT_TOPIC_WS2812_LIGHT);
    mqttSubscribe(MQTT_TOPIC_WS2812_PATTERN);
    mqttSubscribe(MQTT_TOPIC_WS2812_NUMLEDS);
    mqttSubscribe(MQTT_TOPIC_WS2812_PLAYLIST);
    mqttSubscribe(MQTT_TOPIC_WS2812_PATTERNDURATION);

    mqttSendLightOn(); // Send updates once MQTT has connected
    mqttSendPattern();
    mqttSendNumLEDs();
    mqttSendPlaylist();
    mqttSendPatternDuration();
}

void mqttCallback(uint type, espurna::StringView topic, espurna::StringView payload) {
    if (type == MQTT_CONNECT_EVENT) {
        _timer.once(espurna::duration::Milliseconds(250), setupMQTT); // Do this after HomeAssistant has been setup
        return;
    }

    if (type == MQTT_MESSAGE_EVENT) {
        const auto t = mqttMagnitude(topic);
        DEBUG_MSG_P(PSTR("[WS2812] %s\n"), t.c_str());

        if (t.equals(MQTT_TOPIC_WS2812_LIGHT)) { // light/set
            turnOnOff(payload.equals("1"));
        } else if (t.equals(MQTT_TOPIC_WS2812_PLAYLIST)) {
            enablePlaylist(payload.equals("1"));
        } else if (t.equals(MQTT_TOPIC_WS2812_PATTERN)) {
            setPatternByName(payload.c_str());
        } else {
            const auto result = parseUnsigned(payload, 10);
            if (result.ok) {
                if (t.equals(MQTT_TOPIC_WS2812_NUMLEDS)) {
                    uint8_t newCount = constrain(result.value, 0, MAX_NUM_LEDS);
                    if (newCount != numLEDs) {
                        initialize(newCount, lightOn, currentPatternIndex);
                        mqttSendNumLEDs();
                    }
                } else if (t.equals(MQTT_TOPIC_WS2812_PATTERNDURATION)) {
                    setPatternDuration(result.value);
                }
            }
        }
    }
}

static constexpr ::terminal::Command Commands[] PROGMEM{{COMMAND_WS2812, commands::onInfo},
                                                        {COMMAND_WS2812_NUM_LEDS, commands::onNumLEDs},
                                                        {COMMAND_WS2812_ON, commands::onLightOn},
                                                        {COMMAND_WS2812_PATTERN, commands::onPattern},
                                                        {COMMAND_WS2812_PLAYLIST, commands::onPlaylist},
                                                        {COMMAND_WS2812_PATTERNDURATION, commands::onPatternDuration}};

void setup() {
    mqttRegister(mqttCallback);
    espurna::terminal::add(Commands);
    espurnaRegisterLoop(loop);

    playlistEnabled = getSetting(SETTING_PLAYLIST, DEFAULT_PLAYLIST);
    patternDuration = constrain(getSetting(SETTING_PATTERNDURATION, DEFAULT_PATTERN_DURATION), MIN_PATTERN_DURATION,
                                MAX_PATTERN_DURATION);

    FastLED.showColor(CRGB::Orange); // Start with Orange fill

    initialize(getSetting(SETTING_NUM_LEDS, DEFAULT_NUM_LEDS), getSetting(SETTING_ON, DEFAULT_ON_STATE),
               getSetting(SETTING_PATTERN, DEFAULT_PATTERN));
}
} // namespace WS2812Controller

#endif