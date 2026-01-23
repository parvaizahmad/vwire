/*
 * Vwire IOT Arduino Library - Configuration
 * 
 * Board detection, platform-specific settings, and default values.
 * 
 * Copyright (c) 2026 Vwire IOT
 * Website: https://vwireiot.com
 * MIT License
 */

#ifndef VWIRE_CONFIG_H
#define VWIRE_CONFIG_H

// =============================================================================
// VERSION
// =============================================================================
#define VWIRE_VERSION "3.0.0"

// =============================================================================
// BOARD DETECTION
// =============================================================================
#if defined(ESP32)
  #define VWIRE_BOARD_ESP32
  #define VWIRE_BOARD_NAME "ESP32"
  #define VWIRE_HAS_WIFI 1
  #define VWIRE_HAS_SSL 1
  #define VWIRE_HAS_OTA 1
  #define VWIRE_HAS_DEEP_SLEEP 1
  #define VWIRE_MAX_PAYLOAD_LENGTH 2048
  #define VWIRE_JSON_BUFFER_SIZE 1024

#elif defined(ESP8266)
  #define VWIRE_BOARD_ESP8266
  #define VWIRE_BOARD_NAME "ESP8266"
  #define VWIRE_HAS_WIFI 1
  #define VWIRE_HAS_SSL 1
  #define VWIRE_HAS_OTA 1
  #define VWIRE_HAS_DEEP_SLEEP 1
  #define VWIRE_MAX_PAYLOAD_LENGTH 1024
  #define VWIRE_JSON_BUFFER_SIZE 512

#elif defined(ARDUINO_ARCH_RP2040)
  #define VWIRE_BOARD_RP2040
  #define VWIRE_BOARD_NAME "RP2040"
  #define VWIRE_HAS_WIFI 1
  #define VWIRE_HAS_SSL 0  // Limited SSL support on RP2040
  #define VWIRE_HAS_OTA 0
  #define VWIRE_HAS_DEEP_SLEEP 1
  #define VWIRE_MAX_PAYLOAD_LENGTH 1024
  #define VWIRE_JSON_BUFFER_SIZE 512

#elif defined(ARDUINO_ARCH_SAMD)
  #define VWIRE_BOARD_SAMD
  #define VWIRE_BOARD_NAME "SAMD"
  #define VWIRE_HAS_WIFI 1
  #define VWIRE_HAS_SSL 0
  #define VWIRE_HAS_OTA 0
  #define VWIRE_HAS_DEEP_SLEEP 0
  #define VWIRE_MAX_PAYLOAD_LENGTH 512
  #define VWIRE_JSON_BUFFER_SIZE 256

#else
  #define VWIRE_BOARD_GENERIC
  #define VWIRE_BOARD_NAME "Generic"
  #define VWIRE_HAS_WIFI 1
  #define VWIRE_HAS_SSL 0
  #define VWIRE_HAS_OTA 0
  #define VWIRE_HAS_DEEP_SLEEP 0
  #define VWIRE_MAX_PAYLOAD_LENGTH 512
  #define VWIRE_JSON_BUFFER_SIZE 256
#endif

// =============================================================================
// DEFAULT SERVER CONFIGURATION
// =============================================================================
#define VWIRE_DEFAULT_SERVER "mqtt.vwire.io"
#define VWIRE_DEFAULT_PORT_TCP 1883
#define VWIRE_DEFAULT_PORT_TLS 8883

// =============================================================================
// TRANSPORT TYPES
// =============================================================================
typedef enum {
  VWIRE_TRANSPORT_TCP = 0,       // Plain MQTT over TCP (port 1883)
  VWIRE_TRANSPORT_TCP_SSL = 1    // MQTT over TLS (port 8883) - RECOMMENDED
} VwireTransport;

// =============================================================================
// VIRTUAL PIN LIMITS
// =============================================================================
#define VWIRE_MAX_VIRTUAL_PINS 128
#define VWIRE_MAX_HANDLERS 32
#define VWIRE_MAX_TOKEN_LENGTH 64
#define VWIRE_MAX_SERVER_LENGTH 64

// =============================================================================
// TIMING CONFIGURATION
// =============================================================================
#define VWIRE_DEFAULT_HEARTBEAT_INTERVAL 30000    // 30 seconds
#define VWIRE_DEFAULT_RECONNECT_INTERVAL 5000     // 5 seconds
#define VWIRE_DEFAULT_WIFI_TIMEOUT 30000          // 30 seconds
#define VWIRE_DEFAULT_MQTT_TIMEOUT 10000          // 10 seconds

// =============================================================================
// CONNECTION STATES
// =============================================================================
typedef enum {
  VWIRE_STATE_IDLE = 0,
  VWIRE_STATE_CONNECTING_WIFI,
  VWIRE_STATE_CONNECTING_MQTT,
  VWIRE_STATE_CONNECTED,
  VWIRE_STATE_DISCONNECTED,
  VWIRE_STATE_ERROR
} VwireState;

// =============================================================================
// ERROR CODES
// =============================================================================
typedef enum {
  VWIRE_ERR_NONE = 0,
  VWIRE_ERR_NO_TOKEN,
  VWIRE_ERR_WIFI_FAILED,
  VWIRE_ERR_MQTT_FAILED,
  VWIRE_ERR_NOT_CONNECTED,
  VWIRE_ERR_INVALID_PIN,
  VWIRE_ERR_BUFFER_FULL,
  VWIRE_ERR_HANDLER_FULL,
  VWIRE_ERR_TIMEOUT,
  VWIRE_ERR_SSL_FAILED
} VwireError;

#endif // VWIRE_CONFIG_H
