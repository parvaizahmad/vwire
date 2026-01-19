/*
 * Vwire IOT Arduino Library
 * 
 * Professional IoT platform library for Arduino, ESP32, ESP8266 and compatible boards.
 * Connect your microcontrollers to Vwire IOT cloud platform via MQTT.
 * 
 * Features:
 * - MQTT over TCP (port 1883)
 * - MQTT over TLS (port 8883) - RECOMMENDED for security
 * - Virtual pins for bidirectional communication
 * - Push notifications and email alerts
 * - OTA firmware updates (ESP32/ESP8266)
 * - Auto-reconnection with configurable intervals
 * 
 * Copyright (c) 2026 Vwire IOT
 * Website: https://vwireiot.com
 * MIT License
 */

#ifndef VWIRE_H
#define VWIRE_H

#include <Arduino.h>
#include "VwireConfig.h"

// =============================================================================
// PLATFORM-SPECIFIC INCLUDES
// =============================================================================
#if defined(VWIRE_BOARD_ESP32)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #if VWIRE_HAS_OTA
    #include <ArduinoOTA.h>
  #endif
  
#elif defined(VWIRE_BOARD_ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecure.h>
  #if VWIRE_HAS_OTA
    #include <ArduinoOTA.h>
  #endif
  
#elif defined(VWIRE_BOARD_RP2040)
  #include <WiFi.h>
  #include <WiFiClient.h>
  
#elif defined(VWIRE_BOARD_SAMD)
  #include <WiFiNINA.h>
  #include <WiFiClient.h>
  
#else
  #include <WiFi.h>
  #include <WiFiClient.h>
#endif

#include <PubSubClient.h>
#include <ArduinoJson.h>

// =============================================================================
// VIRTUAL PIN CLASS
// =============================================================================
class VirtualPin {
public:
  VirtualPin() : _value("") {}
  VirtualPin(const String& value) : _value(value) {}
  VirtualPin(const char* value) : _value(value) {}
  VirtualPin(int value) : _value(String(value)) {}
  VirtualPin(long value) : _value(String(value)) {}
  VirtualPin(unsigned int value) : _value(String(value)) {}
  VirtualPin(unsigned long value) : _value(String(value)) {}
  VirtualPin(float value) : _value(String(value, 2)) {}
  VirtualPin(double value) : _value(String(value, 4)) {}
  VirtualPin(bool value) : _value(value ? "1" : "0") {}
  
  // Setters
  void set(const String& value) { _value = value; }
  void set(const char* value) { _value = String(value); }
  void set(int value) { _value = String(value); }
  void set(long value) { _value = String(value); }
  void set(unsigned int value) { _value = String(value); }
  void set(unsigned long value) { _value = String(value); }
  void set(float value) { _value = String(value, 2); }
  void set(double value) { _value = String(value, 4); }
  void set(bool value) { _value = value ? "1" : "0"; }
  
  // Getters
  int asInt() const { return _value.toInt(); }
  float asFloat() const { return _value.toFloat(); }
  double asDouble() const { return _value.toDouble(); }
  bool asBool() const { return _value == "1" || _value.equalsIgnoreCase("true") || _value.equalsIgnoreCase("on"); }
  String asString() const { return _value; }
  const char* asCString() const { return _value.c_str(); }
  
  // Array support (comma-separated values)
  int getArraySize() const {
    if (_value.length() == 0) return 0;
    int count = 1;
    for (size_t i = 0; i < _value.length(); i++) {
      if (_value.charAt(i) == ',') count++;
    }
    return count;
  }
  
  int getArrayInt(int index) const { return getArrayElement(index).toInt(); }
  float getArrayFloat(int index) const { return getArrayElement(index).toFloat(); }
  String getArrayElement(int index) const {
    int start = 0;
    int count = 0;
    for (size_t i = 0; i <= _value.length(); i++) {
      if (i == _value.length() || _value.charAt(i) == ',') {
        if (count == index) {
          return _value.substring(start, i);
        }
        count++;
        start = i + 1;
      }
    }
    return "";
  }
  
  // Operators
  operator int() const { return asInt(); }
  operator float() const { return asFloat(); }
  operator bool() const { return asBool(); }
  operator String() const { return _value; }
  
private:
  String _value;
};

// =============================================================================
// SETTINGS STRUCTURE
// =============================================================================
struct VwireSettings {
  char authToken[VWIRE_MAX_TOKEN_LENGTH];
  char server[VWIRE_MAX_SERVER_LENGTH];
  uint16_t port;
  VwireTransport transport;
  bool autoReconnect;
  unsigned long reconnectInterval;
  unsigned long heartbeatInterval;
  unsigned long wifiTimeout;
  unsigned long mqttTimeout;
  uint8_t dataQoS;        // QoS for data writes (0=fastest, 1=reliable)
  bool dataRetain;        // Retain flag for data writes
  
  VwireSettings() {
    memset(authToken, 0, sizeof(authToken));
    strncpy(server, VWIRE_DEFAULT_SERVER, VWIRE_MAX_SERVER_LENGTH - 1);
    port = VWIRE_DEFAULT_PORT_TLS;  // Default to secure port
    transport = VWIRE_TRANSPORT_TCP_SSL;  // Default to TLS
    autoReconnect = true;
    dataQoS = 1;           // Default QoS 1 for reliability
    dataRetain = false;    // Don't retain by default (faster)
    reconnectInterval = VWIRE_DEFAULT_RECONNECT_INTERVAL;
    heartbeatInterval = VWIRE_DEFAULT_HEARTBEAT_INTERVAL;
    wifiTimeout = VWIRE_DEFAULT_WIFI_TIMEOUT;
    mqttTimeout = VWIRE_DEFAULT_MQTT_TIMEOUT;
  }
};

// =============================================================================
// CALLBACK TYPES
// =============================================================================
typedef void (*PinHandler)(VirtualPin&);
typedef void (*ConnectionHandler)();
typedef void (*RawMessageHandler)(const char* topic, const char* payload);

// =============================================================================
// AUTO-REGISTRATION SYSTEM (Blynk-style)
// =============================================================================

// Maximum auto-registered handlers
#define VWIRE_MAX_AUTO_HANDLERS 32

// Global handler table for auto-registration
struct VwireAutoHandler {
  uint8_t pin;
  PinHandler handler;
};

// External declarations for the auto-handler system
extern VwireAutoHandler _vwireAutoWriteHandlers[];
extern uint8_t _vwireAutoWriteCount;
extern ConnectionHandler _vwireAutoConnectHandler;
extern ConnectionHandler _vwireAutoDisconnectHandler;

// Registration functions (called by macros)
void _vwireRegisterWriteHandler(uint8_t pin, PinHandler handler);
void _vwireRegisterConnectHandler(ConnectionHandler handler);
void _vwireRegisterDisconnectHandler(ConnectionHandler handler);

// =============================================================================
// BLYNK-STYLE HANDLER MACROS
// =============================================================================
// Usage:
//   VWIRE_WRITE(V0) {
//     int value = param.asInt();
//     digitalWrite(LED_PIN, value);
//   }
//
//   VWIRE_CONNECTED() {
//     Serial.println("Connected!");
//   }
// =============================================================================

// Helper to create unique variable names
#define _VWIRE_CONCAT(a, b) a##b
#define _VWIRE_UNIQUE(prefix, line) _VWIRE_CONCAT(prefix, line)

// VWIRE_WRITE(Vpin) - Auto-registers a handler for virtual pin writes
// The 'param' variable is available inside the handler
#define VWIRE_WRITE(vpin) \
  void _vwire_write_handler_##vpin(VirtualPin& param); \
  struct _VWIRE_UNIQUE(_VwireAutoReg_, __LINE__) { \
    _VWIRE_UNIQUE(_VwireAutoReg_, __LINE__)() { \
      _vwireRegisterWriteHandler(vpin, _vwire_write_handler_##vpin); \
    } \
  } _VWIRE_UNIQUE(_vwireAutoRegInstance_, __LINE__); \
  void _vwire_write_handler_##vpin(VirtualPin& param)

// VWIRE_READ(Vpin) - Placeholder for read requests (server requests data from device)
#define VWIRE_READ(vpin) \
  void _vwire_read_handler_##vpin()

// VWIRE_CONNECTED() - Auto-registers connection handler
#define VWIRE_CONNECTED() \
  void _vwire_connected_handler(); \
  struct _VWIRE_UNIQUE(_VwireConnectReg_, __LINE__) { \
    _VWIRE_UNIQUE(_VwireConnectReg_, __LINE__)() { \
      _vwireRegisterConnectHandler(_vwire_connected_handler); \
    } \
  } _VWIRE_UNIQUE(_vwireConnectRegInstance_, __LINE__); \
  void _vwire_connected_handler()

// VWIRE_DISCONNECTED() - Auto-registers disconnection handler
#define VWIRE_DISCONNECTED() \
  void _vwire_disconnected_handler(); \
  struct _VWIRE_UNIQUE(_VwireDisconnectReg_, __LINE__) { \
    _VWIRE_UNIQUE(_VwireDisconnectReg_, __LINE__)() { \
      _vwireRegisterDisconnectHandler(_vwire_disconnected_handler); \
    } \
  } _VWIRE_UNIQUE(_vwireDisconnectRegInstance_, __LINE__); \
  void _vwire_disconnected_handler()

// Virtual pin number definitions (V0-V31)
#define V0  0
#define V1  1
#define V2  2
#define V3  3
#define V4  4
#define V5  5
#define V6  6
#define V7  7
#define V8  8
#define V9  9
#define V10 10
#define V11 11
#define V12 12
#define V13 13
#define V14 14
#define V15 15
#define V16 16
#define V17 17
#define V18 18
#define V19 19
#define V20 20
#define V21 21
#define V22 22
#define V23 23
#define V24 24
#define V25 25
#define V26 26
#define V27 27
#define V28 28
#define V29 29
#define V30 30
#define V31 31

// =============================================================================
// MAIN VWIRE CLASS
// =============================================================================
class VwireClass {
public:
  VwireClass();
  ~VwireClass();
  
  // Configuration
  void config(const char* authToken);
  void config(const char* authToken, const char* server, uint16_t port);
  void config(const VwireSettings& settings);
  void setTransport(VwireTransport transport);
  void setAutoReconnect(bool enable);
  void setReconnectInterval(unsigned long interval);
  void setHeartbeatInterval(unsigned long interval);
  void setDataQoS(uint8_t qos);      // 0=fastest (fire&forget), 1=reliable (default)
  void setDataRetain(bool retain);   // false=faster (default), true=retained
  
  // Connection
  bool begin(const char* ssid, const char* password);
  bool begin();  // Use pre-configured WiFi
  void run();
  bool connected();
  void disconnect();
  
  // State
  VwireState getState();
  VwireError getLastError();
  int getWiFiRSSI();
  
  // Virtual Pin Write Operations
  template<typename T>
  void virtualWrite(uint8_t pin, T value) {
    VirtualPin vp(value);
    _virtualWriteInternal(pin, vp.asString());
  }
  
  void virtualWriteArray(uint8_t pin, float* values, int count);
  void virtualWriteArray(uint8_t pin, int* values, int count);
  void virtualWritef(uint8_t pin, const char* format, ...);
  
  // Sync Operations - request stored values from server after reconnect/power cycle
  void syncVirtual(uint8_t pin);      // Sync single pin
  void syncAll();                      // Sync all pins
  
  // Variadic sync for multiple specific pins: Vwire.sync(V0, V1, V2);
  template<typename... Pins>
  void sync(Pins... pins) {
    uint8_t pinArray[] = {static_cast<uint8_t>(pins)...};
    for (size_t i = 0; i < sizeof...(pins); i++) {
      syncVirtual(pinArray[i]);
    }
  }
  
  // Event Handlers
  void onVirtualWrite(uint8_t pin, PinHandler handler);
  void onConnect(ConnectionHandler handler);
  void onDisconnect(ConnectionHandler handler);
  void onMessage(RawMessageHandler handler);
  
  // Notifications
  void notify(const char* message);
  void email(const char* subject, const char* body);
  void log(const char* message);
  
  // Device Info
  const char* getDeviceId();
  const char* getBoardName();
  const char* getVersion();
  uint32_t getFreeHeap();
  uint32_t getUptime();
  
  // OTA Updates (ESP32/ESP8266 only)
  #if VWIRE_HAS_OTA
  void enableOTA(const char* hostname = nullptr, const char* password = nullptr);
  void handleOTA();
  #endif
  
  // Debug
  void setDebug(bool enable);
  void setDebugStream(Stream& stream);
  void printDebugInfo();
  
private:
  // Settings and state
  VwireSettings _settings;
  VwireState _state;
  VwireError _lastError;
  char _deviceId[VWIRE_MAX_TOKEN_LENGTH];
  bool _debug;
  Stream* _debugStream;
  unsigned long _startTime;
  
  // Timing
  unsigned long _lastHeartbeat;
  unsigned long _lastReconnectAttempt;
  
  // Network clients - member variables (not pointers!) for stable TLS
  WiFiClient _wifiClient;
  #if VWIRE_HAS_SSL
  WiFiClientSecure _secureClient;
  #endif
  PubSubClient _mqttClient;
  
  // Handlers
  struct PinHandlerEntry {
    uint8_t pin;
    PinHandler handler;
    bool active;
  };
  PinHandlerEntry _pinHandlers[VWIRE_MAX_HANDLERS];
  int _pinHandlerCount;
  
  ConnectionHandler _connectHandler;
  ConnectionHandler _disconnectHandler;
  RawMessageHandler _messageHandler;
  
  // OTA
  #if VWIRE_HAS_OTA
  bool _otaEnabled;
  #endif
  
  // Internal methods
  bool _connectWiFi(const char* ssid, const char* password);
  bool _connectMQTT();
  void _setupClient();
  void _handleMessage(char* topic, byte* payload, unsigned int length);
  static void _mqttCallbackWrapper(char* topic, byte* payload, unsigned int length);
  void _virtualWriteInternal(uint8_t pin, const String& value);
  String _buildTopic(const char* type, int pin = -1);
  void _sendHeartbeat();
  void _setError(VwireError error);
  void _debugPrint(const char* message);
  void _debugPrintf(const char* format, ...);
};

// =============================================================================
// GLOBAL INSTANCE
// =============================================================================
extern VwireClass Vwire;

#endif // VWIRE_H
