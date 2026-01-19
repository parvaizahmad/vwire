/*
 * Vwire IOT Arduino Library - Implementation
 * 
 * Professional IoT platform library for Arduino, ESP32, ESP8266 and compatible boards.
 * 
 * Copyright (c) 2026 Vwire IOT
 * Website: https://vwireiot.com
 * MIT License
 */

#include "Vwire.h"
#include <stdarg.h>

// =============================================================================
// GLOBAL INSTANCE
// =============================================================================
VwireClass Vwire;
static VwireClass* _vwireInstance = nullptr;

// =============================================================================
// AUTO-REGISTRATION SYSTEM (Blynk-style)
// =============================================================================
VwireAutoHandler _vwireAutoWriteHandlers[VWIRE_MAX_AUTO_HANDLERS];
uint8_t _vwireAutoWriteCount = 0;
ConnectionHandler _vwireAutoConnectHandler = nullptr;
ConnectionHandler _vwireAutoDisconnectHandler = nullptr;

void _vwireRegisterWriteHandler(uint8_t pin, PinHandler handler) {
  if (_vwireAutoWriteCount < VWIRE_MAX_AUTO_HANDLERS) {
    _vwireAutoWriteHandlers[_vwireAutoWriteCount].pin = pin;
    _vwireAutoWriteHandlers[_vwireAutoWriteCount].handler = handler;
    _vwireAutoWriteCount++;
  }
}

void _vwireRegisterConnectHandler(ConnectionHandler handler) {
  _vwireAutoConnectHandler = handler;
}

void _vwireRegisterDisconnectHandler(ConnectionHandler handler) {
  _vwireAutoDisconnectHandler = handler;
}

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================
VwireClass::VwireClass() 
  : _state(VWIRE_STATE_IDLE)
  , _lastError(VWIRE_ERR_NONE)
  , _debug(false)
  , _debugStream(&Serial)
  , _startTime(0)
  , _lastHeartbeat(0)
  , _lastReconnectAttempt(0)
  , _mqttClient(_wifiClient)  // Initialize with WiFiClient - CRITICAL!
  , _pinHandlerCount(0)
  , _connectHandler(nullptr)
  , _disconnectHandler(nullptr)
  , _messageHandler(nullptr)
  #if VWIRE_HAS_OTA
  , _otaEnabled(false)
  #endif
{
  memset(_deviceId, 0, sizeof(_deviceId));
  memset(_pinHandlers, 0, sizeof(_pinHandlers));
  _vwireInstance = this;
}

VwireClass::~VwireClass() {
  disconnect();
}

// =============================================================================
// CONFIGURATION
// =============================================================================
void VwireClass::config(const char* authToken) {
  config(authToken, VWIRE_DEFAULT_SERVER, VWIRE_DEFAULT_PORT_TLS);
}

void VwireClass::config(const char* authToken, const char* server, uint16_t port) {
  strncpy(_settings.authToken, authToken, VWIRE_MAX_TOKEN_LENGTH - 1);
  strncpy(_settings.server, server, VWIRE_MAX_SERVER_LENGTH - 1);
  _settings.port = port;
  
  // Auto-detect transport based on port
  if (port == 8883 || port == 443) {
    _settings.transport = VWIRE_TRANSPORT_TCP_SSL;
  } else {
    _settings.transport = VWIRE_TRANSPORT_TCP;
  }
  
  // Use FULL auth token as device ID for topic authorization
  strncpy(_deviceId, authToken, VWIRE_MAX_TOKEN_LENGTH - 1);
  _deviceId[VWIRE_MAX_TOKEN_LENGTH - 1] = '\0';
  
  _debugPrintf("[Vwire] Config: server=%s, port=%d, transport=%s", 
               _settings.server, _settings.port,
               _settings.transport == VWIRE_TRANSPORT_TCP_SSL ? "TLS" : "TCP");
}

void VwireClass::config(const VwireSettings& settings) {
  _settings = settings;
  
  // Use FULL auth token as device ID for topic authorization
  strncpy(_deviceId, settings.authToken, VWIRE_MAX_TOKEN_LENGTH - 1);
  _deviceId[VWIRE_MAX_TOKEN_LENGTH - 1] = '\0';
}

void VwireClass::setTransport(VwireTransport transport) {
  _settings.transport = transport;
  _debugPrintf("[Vwire] Transport set to: %s", 
               transport == VWIRE_TRANSPORT_TCP_SSL ? "TLS" : "TCP");
}

void VwireClass::setAutoReconnect(bool enable) {
  _settings.autoReconnect = enable;
}

void VwireClass::setReconnectInterval(unsigned long interval) {
  _settings.reconnectInterval = interval;
}

void VwireClass::setHeartbeatInterval(unsigned long interval) {
  _settings.heartbeatInterval = interval;
}

void VwireClass::setDataQoS(uint8_t qos) {
  _settings.dataQoS = (qos > 1) ? 1 : qos;  // Clamp to 0 or 1
}

void VwireClass::setDataRetain(bool retain) {
  _settings.dataRetain = retain;
}

// =============================================================================
// CONNECTION
// =============================================================================
void VwireClass::_setupClient() {
  // Configure client based on transport type
  #if VWIRE_HAS_SSL
  if (_settings.transport == VWIRE_TRANSPORT_TCP_SSL) {
    #if defined(VWIRE_BOARD_ESP32)
    _secureClient.setInsecure();  // Allow self-signed certs
    _secureClient.setTimeout(10);  // 10 second timeout (reduced for faster response)
    #elif defined(VWIRE_BOARD_ESP8266)
    _secureClient.setInsecure();
    // ESP8266 BearSSL needs larger buffers for TLS
    // RX=2048 for incoming, TX=1024 for outgoing (TLS overhead needs ~500+ bytes)
    _secureClient.setBufferSizes(2048, 1024);
    _secureClient.setTimeout(10000);  // 10 second timeout (ms for ESP8266)
    #endif
    
    _mqttClient.setClient(_secureClient);
    _debugPrint("[Vwire] Using TLS/SSL client");
  } else
  #endif
  {
    _mqttClient.setClient(_wifiClient);
    _debugPrint("[Vwire] Using plain TCP client");
  }
  
  _mqttClient.setServer(_settings.server, _settings.port);
  _mqttClient.setCallback(_mqttCallbackWrapper);
  _mqttClient.setBufferSize(VWIRE_MAX_PAYLOAD_LENGTH);
  _mqttClient.setKeepAlive(30);       // 30 second keepalive (faster disconnect detection)
  _mqttClient.setSocketTimeout(5);    // 5 second socket timeout (faster error detection)
}

bool VwireClass::_connectWiFi(const char* ssid, const char* password) {
  _state = VWIRE_STATE_CONNECTING_WIFI;
  _debugPrintf("[Vwire] Connecting to WiFi: %s", ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  unsigned long startAttempt = millis();
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    yield();
    _debugPrint(".");
    
    if (millis() - startAttempt >= _settings.wifiTimeout) {
      _debugPrint("\n[Vwire] WiFi connection timeout!");
      _setError(VWIRE_ERR_WIFI_FAILED);
      _state = VWIRE_STATE_ERROR;
      return false;
    }
  }
  
  _debugPrintf("\n[Vwire] WiFi connected! IP: %s", WiFi.localIP().toString().c_str());
  return true;
}

bool VwireClass::_connectMQTT() {
  if (strlen(_settings.authToken) == 0) {
    _setError(VWIRE_ERR_NO_TOKEN);
    _debugPrint("[Vwire] Error: No auth token configured!");
    return false;
  }
  
  _state = VWIRE_STATE_CONNECTING_MQTT;
  _debugPrintf("[Vwire] Connecting to MQTT: %s:%d", _settings.server, _settings.port);
  
  // Generate client ID from device ID
  String clientId = "vwire-";
  clientId += _deviceId;
  
  // Last will message
  String willTopic = _buildTopic("status");
  const char* willMessage = "{\"status\":\"offline\"}";
  
  _debugPrintf("[Vwire] MQTT connecting as: %s", clientId.c_str());
  
  // Connect with token as both username and password (server validates password)
  bool connected = _mqttClient.connect(clientId.c_str(), _settings.authToken, _settings.authToken, 
                          willTopic.c_str(), 1, true, willMessage);
  
  if (connected) {
    _state = VWIRE_STATE_CONNECTED;
    _debugPrint("[Vwire] MQTT connected!");
    
    // Publish online status
    _mqttClient.publish(willTopic.c_str(), "{\"status\":\"online\"}", true);
    
    // Subscribe to command topics
    String cmdTopic = _buildTopic("cmd") + "/#";
    _mqttClient.subscribe(cmdTopic.c_str());
    _debugPrintf("[Vwire] Subscribed to: %s", cmdTopic.c_str());
    
    _startTime = millis();
    
    // Call connect handler (manual registration first, then auto-registered)
    if (_connectHandler) _connectHandler();
    if (_vwireAutoConnectHandler) _vwireAutoConnectHandler();
    
    return true;
  } else {
    int mqttState = _mqttClient.state();
    _debugPrintf("[Vwire] MQTT failed, state=%d", mqttState);
    _setError(VWIRE_ERR_MQTT_FAILED);
    _state = VWIRE_STATE_ERROR;
    return false;
  }
}

bool VwireClass::begin(const char* ssid, const char* password) {
  _debugPrint("\n[Vwire] ========================================");
  _debugPrintf("[Vwire] Vwire IOT Library v%s", VWIRE_VERSION);
  _debugPrintf("[Vwire] Board: %s", VWIRE_BOARD_NAME);
  _debugPrint("[Vwire] ========================================\n");
  
  // Setup network client first
  _setupClient();
  
  // Connect to WiFi
  if (!_connectWiFi(ssid, password)) {
    return false;
  }
  
  // Connect to MQTT
  return _connectMQTT();
}

bool VwireClass::begin() {
  // Assume WiFi is already connected
  if (WiFi.status() != WL_CONNECTED) {
    _debugPrint("[Vwire] Error: WiFi not connected!");
    _setError(VWIRE_ERR_WIFI_FAILED);
    return false;
  }
  
  _setupClient();
  return _connectMQTT();
}

void VwireClass::run() {
  // Process MQTT messages FIRST - critical for low latency command reception
  if (_mqttClient.connected()) {
    _mqttClient.loop();
    
    // Send heartbeat (only when connected)
    unsigned long now = millis();
    if (now - _lastHeartbeat >= _settings.heartbeatInterval) {
      _lastHeartbeat = now;
      _sendHeartbeat();
    }
    return;  // Fast path - everything is good
  }
  
  // Below here only runs when disconnected
  
  // Allow ESP8266/ESP32 network stack to process
  yield();
  
  // Handle OTA if enabled
  #if VWIRE_HAS_OTA
  if (_otaEnabled) {
    ArduinoOTA.handle();
  }
  #endif
  
  // Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
    if (_state == VWIRE_STATE_CONNECTED) {
      _state = VWIRE_STATE_DISCONNECTED;
      _debugPrint("[Vwire] WiFi disconnected!");
      if (_disconnectHandler) _disconnectHandler();
      if (_vwireAutoDisconnectHandler) _vwireAutoDisconnectHandler();
    }
    return;
  }
  
  // MQTT disconnected but WiFi is up
  if (_state == VWIRE_STATE_CONNECTED) {
    _state = VWIRE_STATE_DISCONNECTED;
    _debugPrint("[Vwire] MQTT disconnected!");
    if (_disconnectHandler) _disconnectHandler();
    if (_vwireAutoDisconnectHandler) _vwireAutoDisconnectHandler();
  }
  
  // Attempt reconnect
  if (_settings.autoReconnect) {
    unsigned long now = millis();
    if (now - _lastReconnectAttempt >= _settings.reconnectInterval) {
      _lastReconnectAttempt = now;
      _connectMQTT();
    }
  }
}

bool VwireClass::connected() {
  return _state == VWIRE_STATE_CONNECTED && _mqttClient.connected();
}

void VwireClass::disconnect() {
  if (_mqttClient.connected()) {
    // Publish offline status using stack buffer
    char topic[96];
    snprintf(topic, sizeof(topic), "vwire/%s/status", _deviceId);
    _mqttClient.publish(topic, "{\"status\":\"offline\"}", true);
    _mqttClient.disconnect();
  }
  _state = VWIRE_STATE_DISCONNECTED;
}

VwireState VwireClass::getState() { return _state; }
VwireError VwireClass::getLastError() { return _lastError; }
int VwireClass::getWiFiRSSI() { return WiFi.RSSI(); }

// =============================================================================
// MQTT CALLBACK
// =============================================================================
void VwireClass::_mqttCallbackWrapper(char* topic, byte* payload, unsigned int length) {
  if (_vwireInstance) {
    _vwireInstance->_handleMessage(topic, payload, length);
  }
}

void VwireClass::_handleMessage(char* topic, byte* payload, unsigned int length) {
  // Copy payload to null-terminated string
  char payloadStr[VWIRE_MAX_PAYLOAD_LENGTH];
  int copyLen = min((unsigned int)(VWIRE_MAX_PAYLOAD_LENGTH - 1), length);
  memcpy(payloadStr, payload, copyLen);
  payloadStr[copyLen] = '\0';
  
  _debugPrintf("[Vwire] Received: %s = %s", topic, payloadStr);
  
  // Call raw message handler if set
  if (_messageHandler) {
    _messageHandler(topic, payloadStr);
  }
  
  // Fast parse: find "/cmd/" in topic using strstr (no heap allocation)
  char* cmdPos = strstr(topic, "/cmd/");
  if (!cmdPos) return;  // Not a command topic
  
  // Get pin string after "/cmd/"
  char* pinStr = cmdPos + 5;  // Skip "/cmd/"
  if (!pinStr || *pinStr == '\0') return;
  
  // Parse pin number (handle V prefix)
  int pin = -1;
  if (*pinStr == 'V' || *pinStr == 'v') {
    pin = atoi(pinStr + 1);
  } else {
    pin = atoi(pinStr);
  }
  
  // Handle the pin if valid
  if (pin >= 0 && pin < VWIRE_MAX_VIRTUAL_PINS) {
    VirtualPin vpin;
    vpin.set(payloadStr);
    
    // First, check manually registered handlers (onVirtualWrite)
    for (int i = 0; i < _pinHandlerCount; i++) {
      if (_pinHandlers[i].active && _pinHandlers[i].pin == pin) {
        if (_pinHandlers[i].handler) {
          _pinHandlers[i].handler(vpin);
        }
        return;  // Found handler, exit immediately
      }
    }
    
    // Then, check auto-registered handlers (VWIRE_WRITE macros)
    for (uint8_t i = 0; i < _vwireAutoWriteCount; i++) {
      if (_vwireAutoWriteHandlers[i].pin == pin) {
        if (_vwireAutoWriteHandlers[i].handler) {
          _vwireAutoWriteHandlers[i].handler(vpin);
        }
        return;  // Found handler, exit immediately
      }
    }
  }
}

// =============================================================================
// VIRTUAL PIN OPERATIONS
// =============================================================================
void VwireClass::_virtualWriteInternal(uint8_t pin, const String& value) {
  if (!connected()) {
    _setError(VWIRE_ERR_NOT_CONNECTED);
    return;
  }
  
  // Use stack-allocated buffer for topic (avoid heap allocation)
  char topic[96];
  snprintf(topic, sizeof(topic), "vwire/%s/pin/V%d", _deviceId, pin);
  
  // Publish with configurable QoS and retain (default: QoS 1, no retain for speed)
  _mqttClient.publish(topic, value.c_str(), _settings.dataRetain);
  _debugPrintf("[Vwire] Write V%d = %s", pin, value.c_str());
}

void VwireClass::virtualWriteArray(uint8_t pin, float* values, int count) {
  String str = "";
  for (int i = 0; i < count; i++) {
    if (i > 0) str += ",";
    str += String(values[i], 2);
  }
  _virtualWriteInternal(pin, str);
}

void VwireClass::virtualWriteArray(uint8_t pin, int* values, int count) {
  String str = "";
  for (int i = 0; i < count; i++) {
    if (i > 0) str += ",";
    str += String(values[i]);
  }
  _virtualWriteInternal(pin, str);
}

void VwireClass::virtualWritef(uint8_t pin, const char* format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  _virtualWriteInternal(pin, String(buffer));
}

void VwireClass::syncVirtual(uint8_t pin) {
  if (!connected()) return;
  // Use stack buffer for topic
  char topic[96];
  snprintf(topic, sizeof(topic), "vwire/%s/sync/V%d", _deviceId, pin);
  _mqttClient.publish(topic, "");
}

void VwireClass::syncAll() {
  if (!connected()) return;
  char topic[96];
  snprintf(topic, sizeof(topic), "vwire/%s/sync", _deviceId);
  _mqttClient.publish(topic, "all");
}

// =============================================================================
// EVENT HANDLERS
// =============================================================================
void VwireClass::onVirtualWrite(uint8_t pin, PinHandler handler) {
  if (_pinHandlerCount >= VWIRE_MAX_HANDLERS) {
    _setError(VWIRE_ERR_HANDLER_FULL);
    _debugPrint("[Vwire] Error: Max handlers reached!");
    return;
  }
  
  _pinHandlers[_pinHandlerCount].pin = pin;
  _pinHandlers[_pinHandlerCount].handler = handler;
  _pinHandlers[_pinHandlerCount].active = true;
  _pinHandlerCount++;
  
  _debugPrintf("[Vwire] Handler registered for V%d", pin);
}

void VwireClass::onConnect(ConnectionHandler handler) { _connectHandler = handler; }
void VwireClass::onDisconnect(ConnectionHandler handler) { _disconnectHandler = handler; }
void VwireClass::onMessage(RawMessageHandler handler) { _messageHandler = handler; }

// =============================================================================
// NOTIFICATIONS
// =============================================================================
void VwireClass::notify(const char* message) {
  if (!connected()) return;
  char topic[96];
  snprintf(topic, sizeof(topic), "vwire/%s/notify", _deviceId);
  _mqttClient.publish(topic, message);
  _debugPrintf("[Vwire] Notify: %s", message);
}

void VwireClass::email(const char* subject, const char* body) {
  if (!connected()) return;
  
  char topic[96];
  char buffer[VWIRE_JSON_BUFFER_SIZE];
  
  snprintf(topic, sizeof(topic), "vwire/%s/email", _deviceId);
  snprintf(buffer, sizeof(buffer), "{\"subject\":\"%s\",\"body\":\"%s\"}", subject, body);
  
  _mqttClient.publish(topic, buffer);
  _debugPrintf("[Vwire] Email: %s", subject);
}

void VwireClass::log(const char* message) {
  if (!connected()) return;
  char topic[96];
  snprintf(topic, sizeof(topic), "vwire/%s/log", _deviceId);
  _mqttClient.publish(topic, message);
}

// =============================================================================
// DEVICE INFO
// =============================================================================
const char* VwireClass::getDeviceId() { return _deviceId; }
const char* VwireClass::getBoardName() { return VWIRE_BOARD_NAME; }
const char* VwireClass::getVersion() { return VWIRE_VERSION; }

uint32_t VwireClass::getFreeHeap() {
  #if defined(VWIRE_BOARD_ESP32) || defined(VWIRE_BOARD_ESP8266)
  return ESP.getFreeHeap();
  #else
  return 0;
  #endif
}

uint32_t VwireClass::getUptime() {
  return (millis() - _startTime) / 1000;
}

// =============================================================================
// OTA
// =============================================================================
#if VWIRE_HAS_OTA
void VwireClass::enableOTA(const char* hostname, const char* password) {
  if (hostname) {
    ArduinoOTA.setHostname(hostname);
  } else {
    String defaultHostname = "vwire-" + String(_deviceId).substring(0, 8);
    ArduinoOTA.setHostname(defaultHostname.c_str());
  }
  
  if (password) {
    ArduinoOTA.setPassword(password);
  }
  
  ArduinoOTA.onStart([this]() {
    _debugPrint("[Vwire] OTA Update starting...");
  });
  
  ArduinoOTA.onEnd([this]() {
    _debugPrint("[Vwire] OTA Update complete!");
  });
  
  ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
    _debugPrintf("[Vwire] OTA Progress: %u%%", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([this](ota_error_t error) {
    _debugPrintf("[Vwire] OTA Error[%u]", error);
  });
  
  ArduinoOTA.begin();
  _otaEnabled = true;
  _debugPrint("[Vwire] OTA enabled");
}

void VwireClass::handleOTA() {
  if (_otaEnabled) ArduinoOTA.handle();
}
#endif

// =============================================================================
// HELPERS
// =============================================================================
String VwireClass::_buildTopic(const char* type, int pin) {
  String topic = "vwire/";
  topic += _deviceId;
  topic += "/";
  topic += type;
  
  if (pin >= 0) {
    topic += "/";
    topic += String(pin);
  }
  
  return topic;
}

void VwireClass::_sendHeartbeat() {
  if (!connected()) return;
  
  // Use stack buffers to avoid heap allocation
  char topic[96];
  char buffer[96];
  
  snprintf(topic, sizeof(topic), "vwire/%s/heartbeat", _deviceId);
  snprintf(buffer, sizeof(buffer), "{\"uptime\":%lu,\"heap\":%lu,\"rssi\":%d}",
           getUptime(), getFreeHeap(), getWiFiRSSI());
  
  _mqttClient.publish(topic, buffer);
}

void VwireClass::_setError(VwireError error) {
  _lastError = error;
}

// =============================================================================
// DEBUG
// =============================================================================
void VwireClass::setDebug(bool enable) { _debug = enable; }
void VwireClass::setDebugStream(Stream& stream) { _debugStream = &stream; }

void VwireClass::_debugPrint(const char* message) {
  if (_debug && _debugStream) {
    _debugStream->println(message);
  }
}

void VwireClass::_debugPrintf(const char* format, ...) {
  if (_debug && _debugStream) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    _debugStream->println(buffer);
  }
}

void VwireClass::printDebugInfo() {
  if (!_debugStream) return;
  
  _debugStream->println(F("\n=== Vwire IOT Debug Info ==="));
  _debugStream->print(F("Version: ")); _debugStream->println(VWIRE_VERSION);
  _debugStream->print(F("Board: ")); _debugStream->println(VWIRE_BOARD_NAME);
  _debugStream->print(F("Device ID: ")); _debugStream->println(_deviceId);
  _debugStream->print(F("Server: ")); _debugStream->print(_settings.server);
  _debugStream->print(F(":")); _debugStream->println(_settings.port);
  _debugStream->print(F("Transport: ")); 
  _debugStream->println(_settings.transport == VWIRE_TRANSPORT_TCP_SSL ? "TLS" : "TCP");
  _debugStream->print(F("State: ")); _debugStream->println(_state);
  _debugStream->print(F("WiFi RSSI: ")); _debugStream->print(getWiFiRSSI()); _debugStream->println(F(" dBm"));
  _debugStream->print(F("Free Heap: ")); _debugStream->print(getFreeHeap()); _debugStream->println(F(" bytes"));
  _debugStream->print(F("Uptime: ")); _debugStream->print(getUptime()); _debugStream->println(F(" sec"));
  _debugStream->print(F("Handlers: ")); _debugStream->println(_pinHandlerCount);
  _debugStream->println(F("============================\n"));
}
