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
    DoorState_closing = 4
};

DebounceEvent *_openSensor, *_closedSensor;
Ticker _buzzerTicker, _doorRelayPulseTicker;
int _buzzerTickerCount;
unsigned int _doorState = DoorState_unknown;
time_t _doorStateChangedAt;
bool _doorRelayPulseTickerActive, _notificationSent, _openSensorPressed, _closedSensorPressed;

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
    status["closedSensorP"] = _closedSensorPressed;
    status["openSensorP"] = _openSensorPressed;
}

void _doorSensorWebSocketOnStart(JsonObject &root)
{
    root["doorVisible"] = 1;
    JsonObject &config = root.createNestedObject("doorConfig");
    config["doorOpenSensor"] = getSetting("doorOpenSensor");
    config["doorClosedSensor"] = getSetting("doorClosedSensor");
    config["openPin"] = DOOR_OPEN_SENSOR_PIN;
    config["closedPin"] = DOOR_CLOSED_SENSOR_PIN;
    config["doorSchHour"] = getSetting("doorSchHour");
    config["doorSchMin"] = getSetting("doorSchMin");

    _sendDoorStatusJSON(root);
}
#endif

void _initializeDoorState()
{
    _openSensorPressed = _isOpenSensorPressed();
    _closedSensorPressed = _isClosedSensorPressed();
    if (_closedSensorPressed)
    {
        _doorState = DoorState_closed;

        if (_openSensorPressed)
        {
            //malfunction
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

void _sendNotification(bool clear)
{
    //Prevent duplicates
    if ((!_notificationSent && clear) || (_notificationSent && !clear))    return;
    
    String topic = getSetting("mqttTopic", MQTT_TOPIC);            
    if (topic.length() > 0)
    {                
        if (topic.endsWith("/")) topic.remove(topic.length() - 1);
        topic.concat("/notify");
        mqttSendRaw(topic.c_str(), clear ? "": "Performing scheduled door closing");
    }
}

bool _isOpenSensorPressed(){
    bool pressed = _openSensor->pressed();

#if DOOR_OPEN_SENSOR_TYPE == DOOR_SENSOR_NORMALLY_CLOSED
    pressed = !pressed;
#endif
    return pressed;
}

bool _isClosedSensorPressed(){
    bool pressed = _closedSensor->pressed();

#if DOOR_CLOSED_SENSOR_TYPE == DOOR_SENSOR_NORMALLY_CLOSED
    pressed = !pressed;
#endif

    return pressed;
}

//The event can only be EVENT_CHANGED
bool _openSensorEvent()
{
    if (!_openSensor->loop())
    {
        return false;
    }
    
    _openSensorPressed = _isOpenSensorPressed();

    //if sensor is closed, then door is completely open otherwise door is closing
    _doorState = _openSensorPressed ? DoorState_open : DoorState_closing;
    _doorStateChangedAt = now();    
    DEBUG_MSG_P(PSTR("[DOOR] Open sensor, %s, doorState=%u\n"), _openSensorPressed ? "closed" : "open", _doorState);
    _sendMqttRaw("doorOpenSensor", _openSensorPressed);
    return true;
}

bool _closedSensorEvent()
{
    if (!_closedSensor->loop())
    {
        return false;
    }
    
    _closedSensorPressed = _isClosedSensorPressed();

    //if sensor is closed, then door is completely closed otherwise opening
    _doorState = _closedSensorPressed ? DoorState_closed : DoorState_opening;
    _doorStateChangedAt = now();
    DEBUG_MSG_P(PSTR("[DOOR] Closed sensor, %s, doorState=%u\n"), _closedSensorPressed ? "closed" : "open", _doorState);
    _sendMqttRaw("doorClosedSensor", _closedSensorPressed); 
    return true;
}

/*
void _doorRelayOff()
{
    _doorRelayPulseTickerActive = false;
    relayStatus(0, false);    
}
void _pulseRelay()
{
    if (_doorRelayPulseTickerActive)
    {
        relayStatus(0, false);
        _doorRelayPulseTicker.detach();
    }

    _doorRelayPulseTickerActive = true;
    relayStatus(0, true);
    _doorRelayPulseTicker.once_ms(1000, _doorRelayOff);
}
 */

void _checkSchedule()
{    
    if (!ntpSynced()) return;   //Time not synced
    if (_doorState != DoorState_open) return; //Door not open

    int doorSchHour = getSetting("doorSchHour", 0).toInt();
    int doorSchMin = getSetting("doorSchMin", 0).toInt();
    if ((doorSchHour == 0) && (doorSchMin == 0)) return; //No schedule defined

    // Check schedules every minute at hh:mm:00
    static unsigned long last_minute = 60;
    unsigned char current_minute = minute();
    if (current_minute != last_minute) {
        last_minute = current_minute;

        time_t t = now();
        unsigned char nowHour = hour(t);
        unsigned char nowMinute = minute(t);
        int timeLeft = (doorSchHour - nowHour) * 60 + (doorSchMin - nowMinute);
        if (timeLeft == 0)
        {
            DEBUG_MSG_P(PSTR("[DOOR] Automatically closing the door\n"));
            _sendNotification(false);
            relayStatus(0, true);
            //_pulseRelay();
        }
    }
}

void _doorSensorLoop()
{
    if (_openSensorEvent() || _closedSensorEvent())
    {
#if WEB_SUPPORT
        wsSend(_sendDoorStatusJSON);
#endif
    }

    _checkSchedule();
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
    }
}

//Called from heartbeat
void doorMQTT() {
    _sendMqttRaw("doorOpenSensor", _openSensorPressed);
    _sendMqttRaw("doorClosedSensor", _closedSensorPressed);
    _sendNotification(true);    //Clear previous notification message
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
        DEBUG_MSG_P(PSTR("[DOOR] Buzzer off\n"));
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
