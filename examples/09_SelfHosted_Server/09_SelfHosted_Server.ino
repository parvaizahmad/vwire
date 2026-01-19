/*
 * Vwire IOT - Self-Hosted Server Connection
 * Copyright (c) 2026 Vwire IOT
 * 
 * Board: Any WiFi-capable board
 * 
 * Connect to your own self-hosted Vwire IOT server:
 * - Custom server hostname/IP
 * - Multiple transport protocols (TCP, TLS)
 * - Configurable ports
 * 
 * Server Setup Requirements:
 * - Vwire IOT server running with MQTT broker (port 1883 internal)
 * - Nginx reverse proxy with Let's Encrypt SSL
 *   - Port 8883: TLS termination -> proxy to internal port 1883
 * 
 * RECOMMENDED Connection Options for ESP8266/ESP32:
 * 1. TCP (port 1883) - Plain MQTT, good for LAN/local networks
 * 2. TCP+TLS (port 8883) - Encrypted MQTT via nginx - BEST FOR INTERNET
 */

#include <Vwire.h>

// =============================================================================
// WIFI CONFIGURATION
// =============================================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// =============================================================================
// VWIRE IOT AUTHENTICATION
// =============================================================================
const char* AUTH_TOKEN    = "YOUR_AUTH_TOKEN";

// =============================================================================
// MQTT BROKER CONFIGURATION
// =============================================================================
// Update these with your self-hosted server details

const char* MQTT_BROKER   = "vwire.yourdomain.com";   // Your server hostname or IP address
const uint16_t MQTT_PORT  = 8883;                     // 8883 for TLS, 1883 for plain TCP

// Transport protocol - RECOMMENDED options:
// - VWIRE_TRANSPORT_TCP           (port 1883) - Plain MQTT, good for LAN
// - VWIRE_TRANSPORT_TCP_SSL       (port 8883) - TLS encrypted - RECOMMENDED
const VwireTransport MQTT_TRANSPORT = VWIRE_TRANSPORT_TCP_SSL;

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
bool ledState = false;
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 5000;

// Helper to get transport name for display
const char* getTransportName() {
  switch(MQTT_TRANSPORT) {
    case VWIRE_TRANSPORT_TCP: return "TCP";
    case VWIRE_TRANSPORT_TCP_SSL: return "TCP+SSL";
    default: return "Unknown";
  }
}

// =============================================================================
// HANDLERS (Auto-registered via macros)
// =============================================================================

VWIRE_WRITE(V0) {
  ledState = param.asBool();
  digitalWrite(LED_BUILTIN, ledState);
  Serial.printf("LED: %s\n", ledState ? "ON" : "OFF");
}

VWIRE_CONNECTED() {
  Serial.println("\nConnected to self-hosted Vwire IOT server!");
  Serial.println("=======================================");
  Serial.printf("  Server: %s\n", MQTT_BROKER);
  Serial.printf("  Port: %d\n", MQTT_PORT);
  Serial.printf("  Transport: %s\n", getTransportName());
  Serial.printf("  WiFi RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.println("=======================================\n");
  
  // Sync state
  Vwire.virtualWrite(V0, ledState);
  
  // Send connection info to dashboard
  char info[128];
  snprintf(info, sizeof(info), "%s:%d (%s)", MQTT_BROKER, MQTT_PORT, getTransportName());
  Vwire.virtualWrite(V2, info);
}

VWIRE_DISCONNECTED() {
  Serial.println("Disconnected from server!");
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println();
  Serial.println("========================================");
  Serial.println("  Vwire IOT - Self-Hosted Server Example");
  Serial.println("========================================");
  Serial.printf("  Broker: %s:%d\n", MQTT_BROKER, MQTT_PORT);
  Serial.printf("  Transport: %s\n", getTransportName());
  Serial.println("========================================\n");
  
  // Initialize LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  // Configure Vwire for self-hosted server
  Vwire.setDebug(true);
  
  // Create settings struct for full configuration
  VwireSettings settings;
  strncpy(settings.authToken, AUTH_TOKEN, VWIRE_MAX_TOKEN_LENGTH);
  strncpy(settings.server, MQTT_BROKER, VWIRE_MAX_SERVER_LENGTH);
  settings.port = MQTT_PORT;
  settings.transport = MQTT_TRANSPORT;
  settings.autoReconnect = true;
  settings.reconnectInterval = 5000;
  
  Vwire.config(settings);
  
  // Alternative simple configuration:
  // Vwire.config(AUTH_TOKEN, MQTT_BROKER, MQTT_PORT);
  // Vwire.setTransport(MQTT_TRANSPORT);
  
  // Note: Handlers are auto-registered via VWIRE_WRITE(), VWIRE_CONNECTED(),
  // and VWIRE_DISCONNECTED() macros defined above - no manual registration needed!
  
  // Connect to WiFi and server
  Serial.println("Connecting to WiFi...");
  if (Vwire.begin(WIFI_SSID, WIFI_PASSWORD)) {
    Serial.println("Connected successfully!");
  } else {
    Serial.println("Connection failed!");
    Serial.printf("Error: %d\n", Vwire.getLastError());
    Serial.println("\nTroubleshooting:");
    Serial.println("1. Check server hostname/IP");
    Serial.println("2. Verify port is accessible");
    Serial.println("3. Check auth token");
    Serial.println("4. Ensure MQTT is configured on server");
  }
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
  Vwire.run();
  
  // Send periodic updates
  if (Vwire.connected() && millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();
    
    float value = random(100, 400) / 10.0;
    Vwire.virtualWrite(V1, value);
    
    Serial.printf("Sent to %s: %.1f\n", MQTT_BROKER, value);
  }
  
  // Monitor connection state
  static VwireState lastState = VWIRE_STATE_IDLE;
  VwireState currentState = Vwire.getState();
  
  if (currentState != lastState) {
    lastState = currentState;
    
    const char* stateNames[] = {
      "IDLE", "WIFI_CONNECTING", "WIFI_CONNECTED", 
      "MQTT_CONNECTING", "CONNECTED", "DISCONNECTED", "ERROR"
    };
    
    Serial.printf("State changed: %s\n", stateNames[currentState]);
  }
}

// =============================================================================
// SERVER CONFIGURATION NOTES
// =============================================================================
/*
 * For your self-hosted Vwire IOT server to work with this example,
 * ensure your reverse proxy (Nginx/NPM) is configured properly:
 * 
 * 1. MQTTS on port 8883:
 *    stream {
 *        upstream mqtt_backend {
 *            server 127.0.0.1:1883;
 *        }
 *        
 *        server {
 *            listen 8883 ssl;
 *            proxy_pass mqtt_backend;
 *            ssl_certificate /path/to/cert.pem;
 *            ssl_certificate_key /path/to/key.pem;
 *        }
 *    }
 * 
 * 2. SSL certificate must be valid (or use setInsecure)
 * 
 * 3. Firewall must allow inbound on your chosen port
 */
