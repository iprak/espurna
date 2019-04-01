/*

DOOR CONTROLLER MODULE

Copyright (C) 2018

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
    DoorState_closing = 4
};

#if DOOR_OPEN_SENSOR_PIN != GPIO_NONE
DebounceEvent * _openSensor;
#endif

#if DOOR_CLOSED_SENSOR_PIN != GPIO_NONE
DebounceEvent * _closedSensor;
#endif

DoorState _doorState = DoorState_unknown;
DoorState _lastDoorState = DoorState_unknown;
time_t _doorStateChangedAt;
bool _performingDoorCommand;

String _doorStateToString(uint32_t state){
    switch(state){
        case DoorState_open: return "open";
        case DoorState_closed: return "closed";
        case DoorState_opening: return "opening";
        case DoorState_closing: return "closing";
        default: return "unknown";
    }
}

#if WEB_SUPPORT

bool _doorSensorWebSocketOnReceive(const char * key, JsonVariant& value) {
    return (strncmp(key, "door", 4) == 0);
}

void _addDoorStatusJSON(JsonObject& root) {
    JsonObject& status = root.createNestedObject("doorStatus");
    status["state"] = (uint32_t) _doorState;
    status["last"] = _doorStateChangedAt;
    status["lastState"] = (uint32_t) _lastDoorState;
}

void _doorSensorWebSocketOnStart(JsonObject& root) {
    root["doorVisible"] = 1;
    JsonObject& config = root.createNestedObject("doorConfig");
    config["doorMqttOpen"] = getSetting("doorMqttOpen");
    config["doorMqttClosed"] = getSetting("doorMqttClosed");
    config["openPin"] = DOOR_OPEN_SENSOR_PIN;
    config["closedPin"] = DOOR_CLOSED_SENSOR_PIN;

    _addDoorStatusJSON(root);
}
#endif

void _initializeDoorState() {
    if (_closedSensor->pressed()) {
        _doorState = DoorState_closed;

        if (_openSensor->pressed()) {
            //malfunction
        }
    }
    else if (_openSensor->pressed()) {
        _doorState = DoorState_open;
    }
    else {
        _doorState = DoorState_opening;
    }
}

void _sendMqttRaw(const char * key, bool sensorClosed) {
    String t = getSetting(key);
    if (t.length() > 0) {
        String payload = sensorClosed ? "closed":"open";
        mqttSendRaw(t.c_str(), payload.c_str());
    }
}

//The event can only be EVENT_CHANGED
bool _openSensorEvent() {
    if (!_openSensor->loop()) {
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
    DEBUG_MSG_P(PSTR("[DOOR] Open sensor, %s, doorState=%u\n"), sensorClosed ? "closed":"open", _doorState); 
    _sendMqttRaw("doorMqttOpen", sensorClosed);
    return true;
}

bool _closedSensorEvent() {
    if (!_closedSensor->loop()) {
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
    DEBUG_MSG_P(PSTR("[DOOR] Closed sensor, %s, doorState=%u\n"), sensorClosed ? "closed":"open", _doorState);
    _sendMqttRaw("doorMqttClosed", sensorClosed);
    return true;
}

void _doorSensorLoop() {
    if (_openSensorEvent() || _closedSensorEvent()) {
        #if WEB_SUPPORT
            wsSend(_addDoorStatusJSON);
        #endif
    }
}

void doorSetup() {
    //mode=BUTTON_SWITCH -> EVENT_CHANGED -> BUTTON_EVENT_CLICK
    //mode=BUTTON_PUSHBUTTON -> EVENT_PRESSED/EVENT_RELEASED -> BUTTON_EVENT_PRESSED/(BUTTON_EVENT_CLICK or BUTTON_EVENT_LNGCLICK or BUTTON_EVENT_LNGLNGCLICK)

    _openSensor = new DebounceEvent(DOOR_OPEN_SENSOR_PIN, BUTTON_SWITCH | DOOR_OPEN_SENSOR_PULLUP);
    _closedSensor = new DebounceEvent(DOOR_CLOSED_SENSOR_PIN, BUTTON_SWITCH | DOOR_CLOSED_SENSOR_PULLUP);
    _initializeDoorState();

    DEBUG_MSG_P(PSTR("[DOOR] Sensors registered\n"));

    // Websocket Callbacks
    #if WEB_SUPPORT
        wsOnSendRegister(_doorSensorWebSocketOnStart);
        wsOnReceiveRegister(_doorSensorWebSocketOnReceive);
    #endif

    // Register loop
    espurnaRegisterLoop(_doorSensorLoop);
}

// Called from relay module
void onDoorOperated(unsigned char pin, bool status) {
    if (status && (pin == RELAY1_PIN)) {
        //#if DOOR_BUZZER_PIN != GPIO_NONE
        //    DEBUG_MSG_P(PSTR("[DOOR] Buzzer on\n"));
        //    digitalWrite(DOOR_BUZZER_PIN, true);
        //    _buzzerTicker.once_ms(1500, _buzzerOff);
        //#endif
    }
}

#endif // DOOR_SUPPORT
