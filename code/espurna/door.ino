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
enum DoorState : uint32_t
{
    DoorState_unknown = 0,
    DoorState_open = 1,
    DoorState_closed = 2,
    DoorState_opening = 3,
    DoorState_closing = 4,

    DoorState_open_command = 9,
    DoorState_closed_command = 10
};

DebounceEvent *_openSensor;
DebounceEvent *_closedSensor;
Ticker _buzzerTicker;
int _buzzerTickerCount;
unsigned int _doorState = DoorState_unknown;
unsigned int _lastDoorState = DoorState_unknown;
time_t _doorStateChangedAt;
bool _performingDoorCommand;
Ticker _doorRelayPulseTicker;
bool _doorRelayPulseTickerActive;
unsigned int _queuedCmd = DoorState_unknown;

String _stateToString(unsigned int state)
{
    switch (state)
    {
    case DoorState_unknown:
        return "unknown";
    case DoorState_open:
        return "open";
    case DoorState_closed:
        return "closed";
    case DoorState_opening:
        return "opening";
    case DoorState_closing:
        return "closing";
    }
    return "";
}

#if WEB_SUPPORT

bool _doorSensorWebSocketOnReceive(const char *key, JsonVariant &value)
{
    return (strncmp(key, "door", 4) == 0);
}

void _sendDoorStatusJSON(JsonObject &root)
{
    JsonObject &status = root.createNestedObject("doorStatus");
    status["state"] = (uint32_t)_doorState;
    status["last"] = _doorStateChangedAt;
    status["lastState"] = (uint32_t)_lastDoorState;
}

void _doorSensorWebSocketOnStart(JsonObject &root)
{
    root["doorVisible"] = 1;
    JsonObject &config = root.createNestedObject("doorConfig");
    config["doorOpenSensor"] = getSetting("doorOpenSensor");
    config["doorClosedSensor"] = getSetting("doorClosedSensor");
    config["openPin"] = DOOR_OPEN_SENSOR_PIN;
    config["closedPin"] = DOOR_CLOSED_SENSOR_PIN;
    config["doorCmd"] = getSetting("doorCmd");

    _sendDoorStatusJSON(root);
}
#endif

void _initializeDoorState()
{
    if (_closedSensor->pressed())
    {
        _doorState = DoorState_closed;

        if (_openSensor->pressed())
        {
            //malfunction
        }
    }
    else if (_openSensor->pressed())
    {
        _doorState = DoorState_open;
    }
    else
    {
        _doorState = DoorState_opening;
    }
}

void _sendMqttRaw(const char *key, bool sensorClosed)
{
    String t = getSetting(key);
    if (t.length() > 0)
    {
        String payload = sensorClosed ? "closed" : "open";
        mqttSendRaw(t.c_str(), payload.c_str());
    }
}

//The event can only be EVENT_CHANGED
bool _openSensorEvent()
{
    if (!_openSensor->loop())
    {
        return false;
    }

    _lastDoorState = _doorState;
    bool sensorClosed = _openSensor->pressed();

#if DOOR_OPEN_SENSOR_TYPE == DOOR_SENSOR_NORMALLY_CLOSED
    sensorClosed = !sensorClosed;
#endif

    //if sensor is closed, then door is completely open otherwise door is closing
    _doorState = sensorClosed ? DoorState_open : DoorState_closing;
    _doorStateChangedAt = now();
    DEBUG_MSG_P(PSTR("[DOOR] Open sensor, %s, doorState=%u\n"), sensorClosed ? "closed" : "open", _doorState);
    _sendMqttRaw("doorOpenSensor", sensorClosed);

    if (sensorClosed)
    {
        //_sendMqttRaw("doorCmd", "open");
    }

    return true;
}

bool _closedSensorEvent()
{
    if (!_closedSensor->loop())
    {
        return false;
    }

    _lastDoorState = _doorState;
    bool sensorClosed = _closedSensor->pressed();

#if DOOR_CLOSED_SENSOR_TYPE == DOOR_SENSOR_NORMALLY_CLOSED
    sensorClosed = !sensorClosed;
#endif

    //if sensor is closed, then door is completely closed otherwise opening
    _doorState = sensorClosed ? DoorState_closed : DoorState_opening;
    _doorStateChangedAt = now();
    DEBUG_MSG_P(PSTR("[DOOR] Closed sensor, %s, doorState=%u\n"), sensorClosed ? "closed" : "open", _doorState);
    _sendMqttRaw("doorClosedSensor", sensorClosed);

    if (sensorClosed)
    {
        //_sendMqttRaw("doorCmd", "closed");
    }

    return true;
}

void _doorSensorLoop()
{
    if (_openSensorEvent() || _closedSensorEvent())
    {
#if WEB_SUPPORT
        wsSend(_sendDoorStatusJSON);
#endif
    }
}


void _doorRelayOff()
{
    _doorRelayPulseTickerActive = false;
    relayStatus(0, false);

    if (_queuedCmd == DoorState_closed_command)
    {
        _queuedCmd = DoorState_unknown;
        _doorRelayPulseTicker.once_ms(1000, close);
    }
    else if (_queuedCmd == DoorState_open_command)
    {
        _queuedCmd = DoorState_unknown;
        _doorRelayPulseTicker.once_ms(1000, open);
    }
}
void _pulseRelay(unsigned int queuedCmd)
{
    if (_doorRelayPulseTickerActive)
    {
        relayStatus(0, false);
        _doorRelayPulseTicker.detach();
    }

    _doorRelayPulseTickerActive = true;
    relayStatus(0, true);
    _queuedCmd = queuedCmd;
    _doorRelayPulseTicker.once_ms(1000, _doorRelayOff);
}
void _pulseRelay()
{
    _pulseRelay(DoorState_unknown);
}

void _updateState(unsigned int state)
{
    _doorState = state;

    String t = getSetting("doorCmd");
    if (t.length() > 0)
    {
        String stateStr = _stateToString(state);
        if (stateStr.length() > 0)
        {
            mqttSendRaw(t.c_str(), stateStr.c_str());
        }
    }
}

Ticker _doorRelayStateTicker;
void _delayChangeState(unsigned int state)
{
    _doorRelayStateTicker.detach();
    _doorRelayStateTicker.once_ms(1000, _updateState, state);
}

Ticker _doorRelayCheckTicker;
void _checkOpenClosed(bool cmdOpen)
{
    const char *subMessage = NULL;

    if (cmdOpen)
    {
        if (_doorState != DoorState_open)
        {
            subMessage = PSTR("Door failed to open, it is currently ");
        }
    }
    else
    {
        if (_doorState != DoorState_closed)
        {
            subMessage = PSTR("Door failed to close, it is currently ");
        }
    }

    if (subMessage != NULL)
    {
        char buffer[11];
        time_t t = now();
        snprintf_P(buffer, sizeof(buffer), PSTR(" at %02d:%02d"), hour(t), minute(t));

        //Appending time to prevent message being marked as a duplicate
        String msg = String(subMessage) + _stateToString(_doorState) + String(buffer);
        mqttSendRaw("smartthings/System/notify", msg.c_str());
    }
}
void _verifyDoorOperation(bool cmdOpen)
{
    _doorRelayCheckTicker.detach();
    _doorRelayCheckTicker.once(20, _checkOpenClosed, cmdOpen);
}

void open()
{
    if (_doorState == DoorState_open_command || _doorState == DoorState_opening)
    {
        return;
    }
    else if (_doorState == DoorState_closing)
    {
        _updateState(DoorState_unknown);
        _pulseRelay(DoorState_open_command); //Stop, open
    }
    else
    {
        _updateState(DoorState_open_command);
        _delayChangeState(DoorState_opening);
        _pulseRelay();
        _verifyDoorOperation(true);
    }
}
void close()
{
    if (_doorState == DoorState_closed_command || _doorState == DoorState_closing)
    {
        return;
    }
    else if (_doorState == DoorState_opening)
    {
        _updateState(DoorState_unknown);
        _pulseRelay(DoorState_closed_command); //Stop, close
    }
    else
    {
        _updateState(DoorState_closed_command);
        _delayChangeState(DoorState_closing);
        _pulseRelay();
        _verifyDoorOperation(false);
    }
}

void _doorMQTTCallback(unsigned int type, const char *topic, const char *payload)
{
    String t = getSetting("doorCmd");
    if (t.length() == 0)
        return;

    if (type == MQTT_CONNECT_EVENT)
    {
        mqttSubscribeRaw(t.c_str());
    }
    else if (type == MQTT_MESSAGE_EVENT)
    {
        DEBUG_MSG_P(PSTR("[DOOR] MQTT payload=%s\n"), payload);
        if (strcmp(payload, "open") == 0)
        {
            open();
        }
        else if (strcmp(payload, "closed") == 0)
        {
            close();
        }
    }
}

void doorSetup()
{
    //mode=BUTTON_SWITCH -> EVENT_CHANGED -> BUTTON_EVENT_CLICK
    //mode=BUTTON_PUSHBUTTON -> EVENT_PRESSED/EVENT_RELEASED -> BUTTON_EVENT_PRESSED/(BUTTON_EVENT_CLICK or BUTTON_EVENT_LNGCLICK or BUTTON_EVENT_LNGLNGCLICK)

#if DOOR_BUZZER_PIN != GPIO_NONE
    pinMode(DOOR_BUZZER_PIN, OUTPUT);
    digitalWrite(DOOR_BUZZER_PIN, LOW);
#endif

    _openSensor = new DebounceEvent(DOOR_OPEN_SENSOR_PIN, BUTTON_SWITCH | DOOR_OPEN_SENSOR_PULLUP);
    _closedSensor = new DebounceEvent(DOOR_CLOSED_SENSOR_PIN, BUTTON_SWITCH | DOOR_CLOSED_SENSOR_PULLUP);
    _initializeDoorState();
    DEBUG_MSG_P(PSTR("[DOOR] Sensors registered\n"));

// Websocket Callbacks
#if WEB_SUPPORT
    wsOnSendRegister(_doorSensorWebSocketOnStart);
    wsOnReceiveRegister(_doorSensorWebSocketOnReceive);
#endif

    mqttRegister(_doorMQTTCallback);

    // Register loop
    espurnaRegisterLoop(_doorSensorLoop);
}

#if DOOR_BUZZER_PIN != GPIO_NONE
void _buzzerOnOff()
{
    _buzzerTickerCount++;
    digitalWrite(DOOR_BUZZER_PIN, (_buzzerTickerCount % 2) == 0 ? LOW : HIGH);

    if (_buzzerTickerCount > 10)
    {
        digitalWrite(DOOR_BUZZER_PIN, LOW);
        _buzzerTicker.detach();
    }
}
#endif

// Called from relay module
void onDoorOperated(unsigned char pin, bool status)
{
#if DOOR_BUZZER_PIN != GPIO_NONE
    if (status && (pin == RELAY1_PIN))
    {
        _buzzerTicker.detach();

        DEBUG_MSG_P(PSTR("[DOOR] Buzzer on\n"));
        digitalWrite(DOOR_BUZZER_PIN, HIGH);

        _buzzerTickerCount = 1;
        _buzzerTicker.attach_ms(750, _buzzerOnOff);
    }
#endif
}

#endif // DOOR_SUPPORT
