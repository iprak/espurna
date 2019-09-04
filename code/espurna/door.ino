/*

DOOR CONTROLLER MODULE

Copyright (C) 2019 Indu Prakash

*/

// -----------------------------------------------------------------------------
// DOOR CONTROLLER
// -----------------------------------------------------------------------------

#if DOOR_SUPPORT
#include <TimeLib.h>
#include <DebounceEvent.h>
#include <Ticker.h>

//https://docs.smartthings.com/en/latest/capabilities-reference.html#door-control
enum DoorState : uint32_t {
    DoorState_unknown = 0,
    DoorState_open = 1,
    DoorState_closed = 2,
    DoorState_opening = 3,
    DoorState_closing = 4,
    DoorState_stopped = 5
};

DebounceEvent *_openSensor, *_closedSensor;
Ticker _buzzerTicker, _doorOperateTicker, _relayTicker, _verifyTicker;
int _buzzerTickerCount, _doorSchHour, _doorSchMin, _doorDelayCheck = 20;
unsigned int _doorState;
time_t _doorStateChangedAt;
bool _notificationSent, _openSensorPressed, _closedSensorPressed, _attemptedStop, _processingCommand;

const char *_formattedDoorState() {
    switch (_doorState)
    {
        case DoorState_open: return "open";
        case DoorState_closed: return "closed";
        case DoorState_opening: return "opening";
        case DoorState_closing: return "closing";
        case DoorState_stopped: return "stopped";
        default: return "unknown";
    }
}

#if WEB_SUPPORT

bool _doorWebSocketOnKeyCheck(const char *key, JsonVariant &value) {
    return (strncmp(key, "door", 4) == 0);
}

void _doorWebSocketOnData(JsonObject &root) {
    JsonObject &status = root.createNestedObject("doorStatus");
    status["state"] = (uint32_t)_doorState;
    status["last"] = _doorStateChangedAt;
    status["closedSensorP"] = _closedSensorPressed; //There seems to some limit on the length of keys
    status["openSensorP"] = _openSensorPressed;
}

void _doorWebSocketOnVisible(JsonObject &root) {
    root["doorVisible"] = 1; //This is a special key
}

void _doorWebSocketOnConnected(JsonObject &root) {
    JsonObject &config = root.createNestedObject("doorConfig");
    config["openPin"] = DOOR_OPEN_SENSOR_PIN;
    config["closedPin"] = DOOR_CLOSED_SENSOR_PIN;
    config["doorSchHour"] = getSetting("doorSchHour");
    config["doorSchMin"] = getSetting("doorSchMin");
    config["doorDelayCheck"] = getSetting("doorDelayCheck");
}
#endif

void _initializeDoorState() {
    //DEBUG_MSG_P(PSTR("[DOOR] _initializeDoorState\n"));

    _openSensorPressed = _isOpenSensorPressed();
    _closedSensorPressed = _isClosedSensorPressed();
    if (_closedSensorPressed)
    {
        _doorState = DoorState_closed;

        if (_openSensorPressed)
        {
            //malfunction?
            _sendNotification("Invalid sensor setup, both sensors are pressed.");
        }
    }
    else if (_openSensorPressed)
    {
        _doorState = DoorState_open;
    }
    else
    {
        _doorState = DoorState_opening;
    }

    _attemptedStop = false;
    _processingCommand = false;
}

void _doorConfigure() {
    _doorDelayCheck = getSetting("doorDelayCheck", 20).toInt();
    if (_doorDelayCheck < 20)
    {
        _doorDelayCheck = 20;
    }

    _doorSchHour = getSetting("doorSchHour", 0).toInt();
    _doorSchMin = getSetting("doorSchMin", 0).toInt();

    //DEBUG_MSG_P(PSTR("[DOOR] _doorConfigure %d %d %d\n"), _doorDelayCheck,_doorSchHour,_doorSchMin);
}

void _updateDoorState(uint32_t state) {
    _doorState = state;
    mqttSend("door", _formattedDoorState());

#if WEB_SUPPORT
    wsPost(_doorWebSocketOnData);
#endif
}

void _clearNotification() {
    //Prevent empty notify messages
    if (!_notificationSent)
        return;
    mqttSend("notify", "");
}

void _sendNotification(const char *msg) {
    //Append time to force messages
    time_t t = now();
    char buffer[strlen(msg) + 12];
    snprintf_P(buffer, sizeof(buffer), PSTR("%s (%02d%02d%02d)"), msg, hour(t), minute(t), second(t));
    mqttSend("notify", buffer);
    _notificationSent = true;
}

bool _isOpenSensorPressed() {
    bool pressed = _openSensor->pressed();

#if DOOR_OPEN_SENSOR_TYPE == DOOR_SENSOR_NORMALLY_CLOSED
    pressed = !pressed;
#endif
    return pressed;
}

bool _isClosedSensorPressed() {
    bool pressed = _closedSensor->pressed();

#if DOOR_CLOSED_SENSOR_TYPE == DOOR_SENSOR_NORMALLY_CLOSED
    pressed = !pressed;
#endif

    return pressed;
}

//The event can only be EVENT_CHANGED
bool _openSensorEvent() {
    if (!_openSensor->loop())
    {
        return false;
    }

    _openSensorPressed = _isOpenSensorPressed();

    //if sensor is closed, then door is completely open otherwise door is closing
    _doorState = _openSensorPressed ? DoorState_open : DoorState_closing;
    if (_doorState == DoorState_open)
    {
        _attemptedStop = false;

        if (!_processingCommand) {
            _sendNotification("Door opened");
        }
    }
    _doorStateChangedAt = now();
    _processingCommand = false;

    DEBUG_MSG_P(PSTR("[DOOR] Open sensor, %s, doorState=%u %s\n"), _openSensorPressed ? "closed" : "open", _doorState, _formattedDoorState());
    doorMQTT();
    return true;
}

bool _closedSensorEvent() {
    if (!_closedSensor->loop())
    {
        return false;
    }

    _closedSensorPressed = _isClosedSensorPressed();

    //if sensor is closed, then door is completely closed otherwise opening
    _doorState = _closedSensorPressed ? DoorState_closed : DoorState_opening;
    if (_doorState == DoorState_closed)
    {
        _attemptedStop = false;

        if (!_processingCommand) {
            _sendNotification("Door closed");
        }
    }
    _doorStateChangedAt = now();
    _processingCommand = false;
    
    DEBUG_MSG_P(PSTR("[DOOR] Closed sensor, %s, doorState=%u %s\n"), _closedSensorPressed ? "closed" : "open", _doorState, _formattedDoorState());
    doorMQTT();
    return true;
}

void _relayOff() {
    _relayTicker.detach();
    relayStatus(0, false);
}
void _actuate() {
    relayStatus(0, true);
    _relayTicker.once_ms(DOOR_RELAY_PULSE_DELAY, _relayOff);
}

void _checkSchedule() {
    if (!ntpSynced())
        return; //Time not synced
    if (_doorState == DoorState_closed)
        return; //Door not open

    // Check schedules every minute at hh:mm:00
    static unsigned long last_minute = 60;
    unsigned char current_minute = minute();
    if (current_minute != last_minute)
    {
        last_minute = current_minute;

        if ((_doorSchHour == 0) && (_doorSchMin == 0))
            return; //No schedule defined

        time_t t = now();
        unsigned char nowHour = hour(t);
        unsigned char nowMinute = minute(t);
        int timeLeft = (_doorSchHour - nowHour) * 60 + (_doorSchMin - nowMinute);
        if (timeLeft == 0)
        {
            DEBUG_MSG_P(PSTR("[DOOR] Automatically closing the door\n"));
            _sendNotification("Performing scheduled door closing");
            _close(true);
        }
    }
}

void _doorSensorLoop() {
    if (_openSensorEvent() || _closedSensorEvent())
    {
#if WEB_SUPPORT
        wsPost(_doorWebSocketOnData);
#endif
    }

    _checkSchedule();
}

void _verifyOpen() {
    //DEBUG_MSG_P(PSTR("[DOOR] _verifyOpen\n"));
    if (_doorState != DoorState_open)
    {
        //Door did not close within 20 seconds - 37 characters
        char buffer[40];
        snprintf_P(buffer, sizeof(buffer), PSTR("Door did not open within %d seconds"));
        _sendNotification(buffer);
    }

    _processingCommand = false; //The command should have processed by the time verification is performed
}

void _openDelayed() {
    _open();
}

bool _open() {
    //DEBUG_MSG_P(PSTR("[DOOR] _open\n"));
    _doorOperateTicker.detach();

    //check if opened/closed too quickly.
    if (_doorState == DoorState_open)
    {
        _sendNotification("Door already open");
        return false;
    }
    else if (_doorState == DoorState_opening)
    {
        _sendNotification("Door already opening");
        return false;
    }
    else if (_doorState == DoorState_closing)
    {
        if (_attemptedStop)
        {
            _sendNotification("Door sensor malfunctioning, open sensor triggered too quickly");
            return false;
        }
        else
        {
            _updateDoorState(DoorState_stopped); //Update and send web notification
            _actuate();                          //cancel closing

            //if door was stopped and it went to opening again (sensor fired too fast), then when
            //_doorOperateTicker fires this function will again set door state to opening.
            _attemptedStop = true;

            _doorOperateTicker.once_ms(DOOR_RELAY_PULSE_DELAY + 1000, _openDelayed);
        }
    }
    else
    {
        _attemptedStop = false;
        _updateDoorState(DoorState_opening);
        _actuate();
        _verifyTicker.once(_doorDelayCheck, _verifyOpen);
    }

    return true;
}

void _verifyClose() {
    //DEBUG_MSG_P(PSTR("[DOOR] _verifyClose\n"));
    if (_doorState != DoorState_closed)
    {
        char buffer[40];
        snprintf_P(buffer, sizeof(buffer), PSTR("Door did not close within %d seconds"), _doorDelayCheck);
        _sendNotification(buffer);
    }

    _processingCommand = false; //The command should have processed by the time verification is performed
}

void _closeDelayed(bool fromSchedule) {
    //DEBUG_MSG_P(PSTR("[DOOR] _closeDelayed %s\n"), fromSchedule? "fromSchedule" : "");
    _close(fromSchedule);
}

bool _close(bool fromSchedule) {
    //DEBUG_MSG_P(PSTR("[DOOR] _close %s\n"), fromSchedule? "fromSchedule" : "");
    _doorOperateTicker.detach();

    if (_doorState == DoorState_closed)
    {
        if (!fromSchedule)
        {
            _sendNotification("Door already closed");
        }

        return false;
    }
    else if (_doorState == DoorState_closing)
    {
        if (!fromSchedule)
        {
            _sendNotification("Door already closing");
        }

        return false;
    }
    else if (_doorState == DoorState_opening)
    {
        if (_attemptedStop)
        {
            _sendNotification("Door sensor malfunctioning, closed sensor triggered too quickly");
            return false;
        }
        else
        {
            _updateDoorState(DoorState_stopped); //Update and send web notification
            _actuate();                          //cancel opening

            //if door was stopped and it went to opening again (sensor fired too fast), then when
            //_doorOperateTicker fires this function will again set door state to opening.
            _attemptedStop = true;

            _doorOperateTicker.once_ms(DOOR_RELAY_PULSE_DELAY + 1000, _closeDelayed, fromSchedule);
        }
    }
    else
    {
        _attemptedStop = false;
        _updateDoorState(DoorState_closing);
        _actuate();
        _verifyTicker.once(_doorDelayCheck, _verifyClose);
    }

    return true;
}

void doorSetupAPI() {
    apiRegister(MQTT_TOPIC_DOOR,
                [](char *buffer, size_t len) {
                    //Send critical details through API since the bridge may prevent message from being sent if the value did not change
                    snprintf_P(buffer, len, PSTR("%d/%d/%d"), _doorState, _openSensorPressed ? 1 : 0, _closedSensorPressed ? 1 : 0);
                },
                [](const char *payload) {
                    //DEBUG_MSG_P(PSTR("[DOOR] Received set command\n"));
                    if (strlen(payload) > 0)
                    {
                        if (payload[0] == '1')
                        {
                            _processingCommand = _open();
                        }
                        else if (payload[0] == '2')
                        {
                            _processingCommand = _close(false);
                        }
                        else if (payload[0] == '9')
                        {
                            _initializeDoorState(); //reset

#if WEB_SUPPORT
                            wsPost(_doorWebSocketOnData);
#endif
                        }
                    }
                });
}

//Door heartbeat
void doorMQTT() {
    mqttSend("door", _formattedDoorState());
    mqttSend("opensensor", _openSensorPressed ? "closed" : "open");
    mqttSend("closedsensor", _closedSensorPressed ? "closed" : "open");
}

void doorSetup() {
    //mode=BUTTON_SWITCH -> EVENT_CHANGED -> BUTTON_EVENT_CLICK
    //mode=BUTTON_PUSHBUTTON -> EVENT_PRESSED/EVENT_RELEASED -> BUTTON_EVENT_PRESSED/(BUTTON_EVENT_CLICK or BUTTON_EVENT_LNGCLICK or BUTTON_EVENT_LNGLNGCLICK)

#if DOOR_BUZZER_PIN != GPIO_NONE
    pinMode(DOOR_BUZZER_PIN, OUTPUT);
    digitalWrite(DOOR_BUZZER_PIN, LOW);
#endif

    _openSensor = new DebounceEvent(DOOR_OPEN_SENSOR_PIN, BUTTON_SWITCH | DOOR_OPEN_SENSOR_PULLUP);
    _closedSensor = new DebounceEvent(DOOR_CLOSED_SENSOR_PIN, BUTTON_SWITCH | DOOR_CLOSED_SENSOR_PULLUP);
    _initializeDoorState();

// Websocket Callbacks
#if WEB_SUPPORT   
     wsRegister()
        .onVisible(_doorWebSocketOnVisible)
        .onConnected(_doorWebSocketOnConnected)
        .onData(_doorWebSocketOnData)
        .onKeyCheck(_doorWebSocketOnKeyCheck);
#endif

    doorSetupAPI();
    doorMQTT();

    DEBUG_MSG_P(PSTR("[DOOR] Ready\n"));

    // Register loop
    espurnaRegisterLoop(_doorSensorLoop);
    espurnaRegisterReload(_doorConfigure);
}

#if DOOR_BUZZER_PIN != GPIO_NONE
void _buzzerOnOff() {
    _buzzerTickerCount++;
    digitalWrite(DOOR_BUZZER_PIN, (_buzzerTickerCount % 2) == 0 ? LOW : HIGH);

    if (_buzzerTickerCount > DOOR_BUZZER_PULSE_CYCLES)
    {
        digitalWrite(DOOR_BUZZER_PIN, LOW);
        //DEBUG_MSG_P(PSTR("[DOOR] Buzzer off\n"));
        _buzzerTicker.detach();
    }
}
#endif

// Called from relay module
void onDoorOperated(unsigned char pin, bool status) {
#if DOOR_BUZZER_PIN != GPIO_NONE
    if (status && (pin == RELAY1_PIN))
    {
        _buzzerTicker.detach();
        //DEBUG_MSG_P(PSTR("[DOOR] Buzzer on\n"));
        digitalWrite(DOOR_BUZZER_PIN, HIGH);
        _buzzerTickerCount = 1;
        _buzzerTicker.attach_ms(DOOR_BUZZER_PULSE_DURATION, _buzzerOnOff);
    }
#endif
}

#if RELAY1_PIN == GPIO_NONE
#error "No relay configured!!"
#endif

#if RELAY2_PIN != GPIO_NONE
#error "Only first relay should be configured!!"
#endif
#if RELAY3_PIN != GPIO_NONE
#error "Only first relay should be configured!!"
#endif
#if RELAY4_PIN != GPIO_NONE
#error "Only first relay should be configured!!"
#endif
#if RELAY5_PIN != GPIO_NONE
#error "Only first relay should be configured!!"
#endif
#if RELAY6_PIN != GPIO_NONE
#error "Only first relay should be configured!!"
#endif
#if RELAY7_PIN != GPIO_NONE
#error "Only first relay should be configured!!"
#endif
#if RELAY8_PIN != GPIO_NONE
#error "Only first relay should be configured!!"
#endif

#endif // DOOR_SUPPORT
