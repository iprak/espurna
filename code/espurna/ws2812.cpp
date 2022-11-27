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
PROGMEM_STRING(COMMAND_WS2812_PLAYDURATION, "WS2812.playduration");

PROGMEM_STRING(SETTING_ON, "ws2812.o");
PROGMEM_STRING(SETTING_NUM_LEDS, "ws2812.n");
PROGMEM_STRING(SETTING_PATTERN, "ws2812.p");
PROGMEM_STRING(SETTING_PLAYLIST, "ws2812.pl");
PROGMEM_STRING(SETTING_PLAYDURATION, "ws2812.pd");

#define DEFAULT_NUM_LEDS 50
#define DEFAULT_DELAY 20
#define DEFAULT_PATTERN 0
#define DEFAULT_PLAYLIST false
#define DEFAULT_PLAYDURATION 30 // seconds
#define DEFAULT_ON_STATE true
#define DEFAULT_BRIGHTNESS 200

#define DEBUG_LOG_PERIOD 15000

uint8_t totalPatterns;
uint8_t currentPattern = DEFAULT_PATTERN;
uint8_t numLEDs = 0;
CRGB leds[MAX_NUM_LEDS];
bool lightOn;
bool playlistEnabled;
uint8_t playDuration; // seconds

espurna::timer::SystemTimer _timer;

unsigned long last_msg_time = 0;
unsigned long last_pattern_start = 0;
bool showLoopMessage = false;

namespace Patterns {

typedef void (*PatternList[])();

uint8_t ihue = 0;
uint8_t index = 0;
bool bouncedRight = false;

void rainbow();
void rainbowLoop();
void random();
void redBlueBounce();
void rotatingRedBlue();
void propeller();
void red();
void blue();
void green();

const PatternList patternFns = {blue,   green, propeller,     rainbow,        rainbowLoop,
                                random, red,   redBlueBounce, rotatingRedBlue};

const char *patternNames[] = {"blue",   "green", "propeller",     "rainbow",        "rainbowLoop",
                              "random", "red",   "redBlueBounce", "rotatingRedBlue"};

void clearAll() { FastLED.clear(true); }

void showDelay(unsigned long ms) {
    FastLED.show();
    delay(ms);
}

uint8_t antipodal_index(uint8_t i) {
    uint8_t topIndex = numLEDs / 2;
    uint8_t invertIndex = i + topIndex;
    if (i >= topIndex) {
        invertIndex = (i + topIndex) % numLEDs;
    }
    return invertIndex;
}

void rainbow() {
    ihue++;
    fill_rainbow(leds, numLEDs, ihue);
    showDelay(10);
}

void rainbowLoop() {
    index++;
    ihue = ihue + 10;
    if (index >= numLEDs) {
        index = 0;
    }
    if (ihue > 255) {
        ihue = 0;
    }
    leds[index] = CHSV(ihue, 255, 255);
    showDelay(20);
}

void random() {
    leds[::random(0, numLEDs)] = CHSV(::random(0, 255), 255, 255);
    showDelay(20);
}

void redBlueBounce() {
    if (!bouncedRight) {
        index++;
        if (index == numLEDs) {
            bouncedRight = true;
            index--;
        }
    }
    if (bouncedRight) {
        index--;
        if (index == 0) {
            bouncedRight = false;
        }
    }
    for (uint8_t i = 0; i < numLEDs; i++) {
        if (i == index) {
            leds[i] = CHSV(HUE_BLUE, 255, 255);
        } else {
            leds[i] = CHSV(0, 0, 0);
        }

        uint8_t otherIndex = antipodal_index(index);
        leds[otherIndex] = CHSV(HUE_RED, 255, 255);
    }
    showDelay(20);
}

void rotatingRedBlue() {
    index++;
    if (index >= numLEDs) {
        index = 0;
    }
    leds[index] = CHSV(HUE_RED, 255, 255);
    leds[antipodal_index(index)] = CHSV(HUE_BLUE, 255, 255);
    showDelay(20);
}

void propeller() {
    index++;
    int N3 = int(numLEDs / 3);
    int N12 = int(numLEDs / 12);

    for (int i = 0; i < N3; i++) {
        int j0 = (index + i + numLEDs - N12) % numLEDs;
        int j1 = (j0 + N3) % numLEDs;
        int j2 = (j1 + N3) % numLEDs;
        leds[j0] = CHSV(HUE_RED, 255, 255);
        leds[j1] = CHSV(HUE_GREEN, 255, 255);
        leds[j2] = CHSV(HUE_BLUE, 255, 255);
    }
    showDelay(25);
}

void red() { FastLED.showColor(CRGB(255, 0, 0)); }
void blue() { FastLED.showColor(CRGB(0, 0, 255)); }
void green() { FastLED.showColor(CRGB(0, 255, 0)); }

} // namespace Patterns

void nextPattern();

const char *getPatternName(uint8_t index) {
    return (index < 0 || index >= totalPatterns) ? "" : Patterns::patternNames[index];
}

void mqttSendLightOn() { mqttSend(MQTT_TOPIC_WS2812_LIGHT, lightOn ? "1" : "0"); }
void mqttSendPattern() { mqttSend(MQTT_TOPIC_WS2812_PATTERN, getPatternName(currentPattern)); }
void mqttSendNumLEDs() { mqttSend(MQTT_TOPIC_WS2812_NUMLEDS, String(numLEDs).c_str()); }
void mqttSendPlaylist() { mqttSend(MQTT_TOPIC_WS2812_PLAYLIST, playlistEnabled ? "1" : "0"); }
void mqttSendPlayduration() { mqttSend(MQTT_TOPIC_WS2812_PLAYDURATION, String(playDuration).c_str()); }

void initialize(uint8_t countValue, bool onValue, uint8_t patternValue, bool playlistValue, uint8_t playdurationValue) {
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
    if (currentPattern != patternValue) {
        currentPattern = patternValue;
        setSetting(SETTING_PATTERN, currentPattern);
        needToSaveSettings = true;
    }

    if (playlistEnabled != playlistValue) {
        playlistEnabled = playlistValue;
        setSetting(SETTING_PLAYLIST, playlistEnabled);
        needToSaveSettings = true;
    }

    playdurationValue = constrain(playdurationValue, 0, 100);
    if (playDuration != playdurationValue) {
        playDuration = playdurationValue;
        setSetting(SETTING_PLAYDURATION, playDuration);
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
                totalPatterns, lightOn, currentPattern);
}

void loop() {
    if (!lightOn || Update.isRunning()) //! wifiConnected()
        return;

    unsigned long current_time = millis();

    if (playlistEnabled) {
        if (last_pattern_start == 0) {
            last_pattern_start = current_time;
        } else if (abs(current_time - last_pattern_start) > (playDuration * 1000)) {
            last_pattern_start = current_time;
            nextPattern();
        }
    }

    if (!showLoopMessage || (abs(current_time - last_msg_time) > DEBUG_LOG_PERIOD)) {
        showLoopMessage = true;
        last_msg_time = current_time;
        DEBUG_MSG_P(PSTR("[WS2812] %s\n"), getPatternName(currentPattern));
    }

    if (currentPattern >= 0 && currentPattern < totalPatterns) {
        auto patternFx = Patterns::patternFns[currentPattern];
        patternFx();
    }
}

/// @brief Sets new pattern. Sends MQTT update.
/// @param newValue
void setPattern(uint8_t newValue) {
    newValue = constrain(newValue, 0, totalPatterns - 1);
    DEBUG_MSG_P(PSTR("[WS2812] setPattern(%d) current=%d\n"), newValue, currentPattern);
    if (currentPattern != newValue) {
        currentPattern = newValue;
        setSetting(SETTING_PATTERN, currentPattern);
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

/// @brief Sets new playDuration. Sends MQTT update.
/// @param newValue
void setPlayDuration(uint8_t newValue) {
    newValue = constrain(newValue, MIN_PLAY_DURATION, MAX_PLAY_DURATION);
    DEBUG_MSG_P(PSTR("[WS2812] playDuration(%d) state=%d\n"), newValue, playDuration);
    if (playDuration != newValue) {
        playDuration = newValue;
        setSetting(SETTING_PLAYDURATION, playDuration);
        saveSettings();
        mqttSendPlayduration();
    }
}

void nextPattern() {
    uint8_t newPattern = currentPattern + 1;
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

namespace commands {

static void showInfo(::terminal::CommandContext &ctx) {
    ctx.output.printf_P(PSTR("WS2812: %s, %d leds, %s\n"), getPatternName(WS2812Controller::currentPattern), numLEDs,
                        lightOn ? "on" : "off");
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
                initialize(newCount, lightOn, currentPattern, playlistEnabled, playDuration);
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

static void onPlayDuration(::terminal::CommandContext &&ctx) {
    if (ctx.argv.size() == 2) {
        const auto result = parseUnsigned(ctx.argv[1], 10);
        if (result.ok) {
            setPlayDuration(result.value);
            showInfo(ctx);
            terminalOK(ctx);
        } else {
            terminalError(ctx, F("invalid argument"));
        }
    } else {
        terminalError(ctx, F("WS2812.playDuration count"));
    }
}

} // namespace commands

void setupMQTT() {
    mqttSubscribe(MQTT_TOPIC_WS2812_LIGHT);
    mqttSubscribe(MQTT_TOPIC_WS2812_PATTERN);
    mqttSubscribe(MQTT_TOPIC_WS2812_NUMLEDS);
    mqttSubscribe(MQTT_TOPIC_WS2812_PLAYLIST);
    mqttSubscribe(MQTT_TOPIC_WS2812_PLAYDURATION);

    mqttSendLightOn(); // Send updates once MQTT has connected
    mqttSendPattern();
    mqttSendNumLEDs();
    mqttSendPlaylist();
    mqttSendPlayduration();
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
        } else if (t.equals(MQTT_TOPIC_WS2812_NUMLEDS)) {
            const auto result = parseUnsigned(payload, 10);
            if (result.ok) {
                uint8_t newCount = constrain(result.value, 0, MAX_NUM_LEDS);
                if (newCount != numLEDs) {
                    initialize(newCount, lightOn, currentPattern, playlistEnabled, playDuration);
                    mqttSendNumLEDs();
                }
            }
        } else if (t.equals(MQTT_TOPIC_WS2812_PLAYDURATION)) {
            const auto result = parseUnsigned(payload, 10);
            if (result.ok) {
                setPlayDuration(result.value);
            }
        }
    }
}

static constexpr ::terminal::Command Commands[] PROGMEM{{COMMAND_WS2812, commands::onInfo},
                                                        {COMMAND_WS2812_NUM_LEDS, commands::onNumLEDs},
                                                        {COMMAND_WS2812_ON, commands::onLightOn},
                                                        {COMMAND_WS2812_PATTERN, commands::onPattern},
                                                        {COMMAND_WS2812_PLAYLIST, commands::onPlaylist},
                                                        {COMMAND_WS2812_PLAYDURATION, commands::onPlayDuration}};

void setup() {
    mqttRegister(mqttCallback);
    espurna::terminal::add(Commands);
    espurnaRegisterLoop(loop);

    initialize(getSetting(SETTING_NUM_LEDS, DEFAULT_NUM_LEDS), getSetting(SETTING_ON, DEFAULT_ON_STATE),
               getSetting(SETTING_PATTERN, DEFAULT_PATTERN), getSetting(SETTING_PLAYLIST, DEFAULT_PLAYLIST),
               getSetting(SETTING_PLAYDURATION, DEFAULT_PLAYDURATION));
}
} // namespace WS2812Controller

#endif