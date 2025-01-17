#pragma once

// 1.13.3 added TELNET_PASSWORD build-only flag
// 1.13.4 replaces it with TELNET_AUTHENTICATION runtime setting default
// TODO warning should be removed eventually
#ifdef TELNET_PASSWORD
#warning TELNET_PASSWORD is deprecated! Please replace it with TELNET_AUTHENTICATION
#define TELNET_AUTHENTICATION TELNET_PASSWORD
#endif

// 1.14.0 combines RF_SUPPORT with RFB_DIRECT
#ifdef RF_PIN
#warning RF_PIN is deprecated! Please use RFB_RX_PIN instead
#define RFB_RX_PIN RF_PIN
#endif

// 1.14.0 allow multiple digitals
#ifdef DIGITAL_PIN
#warning DIGITAL_PIN is deprecated! Please use DIGITAL1_PIN instead
#define DIGITAL1_PIN DIGITAL_PIN
#endif

// 1.14.0 allow multiple events
#ifdef EVENTS_PIN
#warning EVENTS_PIN is deprecated! Please use EVENTS1_PIN instead
#define EVENTS1_PIN EVENTS_PIN
#endif

// 1.14.0 unifies mqtt payload options
#ifdef HOMEASSISTANT_PAYLOAD_ON
#warning HOMEASSISTANT_PAYLOAD_ON is deprecated! Global RELAY_MQTT_ON is used instead
#endif

#ifdef HOMEASSISTANT_PAYLOAD_OFF
#warning HOMEASSISTANT_PAYLOAD_OFF is deprecated! Global RELAY_MQTT_OFF is used instead
#endif

#ifdef HOMEASSISTANT_PAYLOAD_AVAILABLE
#warning HOMEASSISTANT_PAYLOAD_AVAILABLE is deprecated! Global MQTT_STATUS_ONLINE is used instead
#endif

#ifdef HOMEASSISTANT_PAYLOAD_NOT_AVAILABLE
#warning HOMEASSISTANT_PAYLOAD_NOT_AVAILABLE is deprecated! Global MQTT_STATUS_OFFLINE is used instead
#endif

// 1.14.0 adds SecureClient
#if MQTT_SUPPORT && MQTT_LIBRARY == MQTT_LIBRARY_ASYNCMQTT_CLIENT && ASYNC_TCP_SSL_ENABLED
#warning "Current implementation of AsyncMqttClient with axTLS is no longer supported. Consider switching to the SECURE_CLIENT configuration with MQTT_LIBRARY_ARDUINOMQTT or MQTT_LIBRARY_PUBSUBCLIENT. See: https://github.com/xoseperez/espurna/issues/1465"
#endif

// 1.15.0 changes preprocessor var name
#ifdef BUTTON_DBLCLICK_DELAY
#warning "BUTTON_DBLCLICK_DELAY is deprecated! Please use BUTTON_REPEAT_DELAY instead"
#define BUTTON_REPEAT_DELAY BUTTON_DBLCLICK_DELAY
#endif

#ifdef BUTTON1_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON1_CONFIG BUTTON1_MODE
#endif

#ifdef BUTTON2_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON2_CONFIG BUTTON2_MODE
#endif

#ifdef BUTTON3_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON3_CONFIG BUTTON3_MODE
#endif

#ifdef BUTTON4_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON4_CONFIG BUTTON4_MODE
#endif

#ifdef BUTTON4_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON4_CONFIG BUTTON4_MODE
#endif

#ifdef BUTTON5_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON5_CONFIG BUTTON5_MODE
#endif

#ifdef BUTTON6_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON6_CONFIG BUTTON6_MODE
#endif

#ifdef BUTTON7_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON7_CONFIG BUTTON7_MODE
#endif

#ifdef BUTTON8_MODE
#warning "BUTTON[1-8]_MODE is deprecated! Please use BUTTON[1-8]_CONFIG instead"
#define BUTTON8_CONFIG BUTTON8_MODE
#endif

#ifdef CSE7766_PIN
#warning "CSE7766_PIN is deprecated! Please use UART[1-3]_RX_PIN"
#define UART1_RX_PIN CSE7766_PIN
#endif

#ifdef WIFI_FALLBACK_APMODE
#warning "WIFI_FALLBACK_APMODE is deprecated! Please use WIFI_AP_MODE instead"
#define WIFI_AP_MODE ((1 == WIFI_FALLBACK_APMODE) ? WiFiApMode::Fallback : WiFiApMode::Disabled)
#endif

// 1.15.0 uses RFB_... instead of RF_...

#ifdef RFB_DIRECT
#warning "RFB_DIRECT is deprecated! Please use RFB_PROVIDER=RFB_PROVIDER_..."
#undef RFB_PROVIDER
#if RFB_DIRECT
#define RFB_PROVIDER RFB_PROVIDER_RCSWITCH
#else
#define RFB_PROVIDER RFB_PROVIDER_EFM8BB1
#endif
#endif

#ifdef RF_LEARN_TIMEOUT
#warning "RF_LEARN_TIMEOUT is deprecated! Please use RFB_LEARN_TIMEOUT"
#undef RFB_LEARN_TIMEOUT
#define RFB_LEARN_TIMEOUT RF_LEARN_TIMEOUT
#endif

#ifdef RF_SEND_TIMES
#warning "RF_SEND_TIMES is deprecated! Please use RFB_SEND_REPEATS"
#undef RFB_SEND_REPEATS
#define RFB_SEND_REPEATS RF_SEND_TIMES
#endif

#ifdef RF_SEND_DELAY
#warning "RF_SEND_DELAY is deprecated! Please use RFB_SEND_DELAY"
#undef RFB_SEND_DELAY
#define RFB_SEND_DELAY RF_SEND_DELAY
#endif

#ifdef RF_RECEIVE_DELAY
#warning "RF_RECEIVE_DELAY is deprecated! Please use RFB_RECEIVE_DELAY"
#undef RFB_RECEIVE_DELAY
#define RFB_RECEIVE_DELAY RF_RECEIVE_DELAY
#endif

#ifdef RF_SUPPORT
#warning "RF_SUPPORT is deprecated! Please use RFB_SUPPORT"
#undef RFB_SUPPORT
#define RFB_SUPPORT RF_SUPPORT
#endif

#ifdef PZEM004T_ADDRESSES
#warning "PZEM004T_ADDRESSES is deprecated! Addresses can be set by using individual flags PZEM004T_ADDRESS_{1,2,3}"
#endif

#if defined(IR_BUTTON_SET) && not defined(IT_RX_PRESET)
#define IR_RX_PRESET IR_BUTTON_SET
#warning "IR_BUTTON_SET was renamed to IR_RX_PRESET"
#endif

#if defined(TEMPERATURE_MIN_CHANGE) \
    || defined(HUMIDITY_MIN_CHANGE) \
    || defined(ENERGY_MAX_CHANGE)
#warning "Global MIN / MAX CHANGE is replaced with per-magnitude settings, please use ${prefix}MinDelta / ${prefix}MaxDelta"
#endif

#ifdef API_REAL_TIME_VALUES
#define SENSOR_REAL_TIME_VALUES API_REAL_TIME_VALUES
#warning "API_REAL_TIME_VALUES is deprecated! Please use SENSOR_REAL_TIME_VALUES"
#endif

#ifdef DEBUG_PORT
#warning "DEBUG_PORT is deprecated! Please set up the appropriate port as DEBUG_SERIAL_PORT=[1-3]"
#endif

#ifdef SERIAL_BAUDRATE
#warning "SERIAL_BAUDRATE is deprecated! Please use UART[1-3]_BAUDRATE"
#define UART1_BAUDRATE SERIAL_BAUDRATE
#endif

#ifdef SCHEDULER_RESTORE_LAST_SCHEDULE
#warning "SCHEDULER_RESTORE_LAST_SCHEDULE is deprecated! Please use SCHEDULER_RESTORE"
#define SCHEDULER_RESTORE SCHEDULER_RESTORE_LAST_SCHEDULE
#endif

#if ( defined(PZEM004TV30_HW_PORT) || \
    defined(PZEM004TV30_USE_SOFT) || \
    defined(PZEM004T_HW_PORT) || \
    defined(PZEM004T_USE_SOFT) || \
    defined(PMS_USE_SOFT) \
)
#warning "Software serial can be enabled with UART_SOFTWARE_SUPPORT"
#undef UART_SOFTWARE_SUPPORT
#define UART_SOFTWARE_SUPPORT 1
#endif

#if ( defined(CSE7766_RX_PIN) || \
    defined(CSE7766_BAUDRATE) || \
    defined(CSE7766_PIN_INVERSE) || \
    defined(MHZ19_RX_PIN) || \
    defined(MHZ19_TX_PIN) || \
    defined(PM1006_RX_PIN) || \
    defined(PM1006_BAUDRATE) || \
    defined(PMS_RX_PIN) || \
    defined(PMS_TX_PIN) || \
    defined(PMS_HW_PORT) || \
    defined(PZEM004T_RX_PIN) || \
    defined(PZEM004T_TX_PIN) || \
    defined(PZEM004TV30_RX_PIN) || \
    defined(PZEM004TV30_TX_PIN) || \
    defined(SDS011_RX_PIN) || \
    defined(SDS011_TX_PIN) || \
    defined(SENSEAIR_RX_PIN) || \
    defined(SENSEAIR_TX_PIN) || \
    defined(SM300D2_RX_PIN) || \
    defined(SM300D2_TX_PIN) || \
    defined(T6613_RX_PIN) || \
    defined(T6613_TX_PIN) || \
    defined(V9261F_PIN) || \
    defined(V9261F_PIN_INVERSE) || \
    defined(V9261F_BAUDRATE) || \
    defined(EZOPH_RX_PIN) || \
    defined(EZOPH_TX_PIN) \
)
#warning "Sensor serial port configuration should be using UART_... (see general.h) and UART[1-3]_... (see defaults.h)"
#endif
