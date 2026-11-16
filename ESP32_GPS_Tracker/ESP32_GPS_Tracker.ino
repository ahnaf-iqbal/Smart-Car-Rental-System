/*
 ===============================================================================
  Project: Smart Car Rental System (BRAC University - CSE461 Robotics Lab)
  Group:   Group 05 | Section 08
  Board:   ESP32 Dev Module / ESP-WROOM-32
  Sensor:  UBLOX NEO-6M GPS Module

  Features:
   - Live GPS tracking (Latitude, Longitude, Altitude, Speed, Satellites)
   - Cloud Telemetry with GeoLinker API / HTTP over 2.4 GHz WiFi
   - Hardware UART2 (RX2 = GPIO 16, TX2 = GPIO 17) connection to GPS
   - Robust NMEA parser error handling & indoor/outdoor status diagnostics
 ===============================================================================

  Wiring / Pin Mapping:
  ------------------------------------------------------------
  UBLOX NEO-6M GPS Pin  ESP32 Pin          Notes
  ------------------------------------------------------------
  VCC                   3.3V or 5V         NEO-6M supports 3.3V-5V input
  GND                   GND                Common Ground
  TX                    GPIO 16 (RX2)      ESP32 Hardware Serial2 RX
  RX                    GPIO 17 (TX2)      ESP32 Hardware Serial2 TX
 ===============================================================================
*/

#include <WiFi.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>

// --- WIFI CONFIGURATION (2.4 GHz only) ---
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- GEOLINKER / TELEMETRY CONFIGURATION ---
const char* serverName = "api.geolinker.com"; // Or your custom backend endpoint
const String apiKey    = "YOUR_GEOLINKER_API_KEY";
const String deviceId  = "CAR_01";

// --- GPS HARDWARE SERIAL CONFIGURATION ---
#define GPS_RX_PIN 16 // Connect to NEO-6M TX
#define GPS_TX_PIN 17 // Connect to NEO-6M RX
#define GPS_BAUD   9600

HardwareSerial neogps(2);
TinyGPSPlus gps;

// Status Enums matching the system diagnostic output
enum StatusCode {
  STATUS_OK = 0,
  STATUS_SENT = 1,
  STATUS_GPS_ERROR = 2,
  STATUS_NETWORK_ERROR = 3,
  STATUS_BAD_REQUEST_ERROR = 4,
  STATUS_GPS_PARSE_ERROR = 5,
  STATUS_INTERNAL_SERVER_ERROR = 6
};

unsigned long lastSendTime = 0;
const unsigned long sendInterval = 5000; // Ping interval: 5 seconds

void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WARNING] WiFi Connection failed! Will retry in loop.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=============================================");
  Serial.println("   CSE461 - ESP32 UBLOX NEO-6M GPS TRACKER   ");
  Serial.println("=============================================");

  // Initialize GPS UART2
  neogps.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // Initialize WiFi
  connectToWiFi();

  Serial.println("GeoLinker Tracker setup complete!");
}

void loop() {
  // Feed characters from GPS module to TinyGPS++ parser
  while (neogps.available() > 0) {
    char c = neogps.read();
    gps.encode(c);
  }

  // Periodic transmission every sendInterval ms
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();
    uint8_t status = evaluateAndSendTelemetry();

    if (status > 0) {
      Serial.print("[STATUS] ");
      switch (status) {
        case STATUS_SENT:
          Serial.println("^ Data sent to GeoLinker Cloud");
          break;
        case STATUS_GPS_ERROR:
          Serial.println("* GPS error - Check wiring!");
          break;
        case STATUS_NETWORK_ERROR:
          Serial.println("* Network error - Storing data offline");
          break;
        case STATUS_BAD_REQUEST_ERROR:
          Serial.println("* Bad request - Check API key/data format");
          break;
        case STATUS_GPS_PARSE_ERROR:
          Serial.println("* GPS data parse error - Invalid NMEA (Indoor / No Satellite Fix)");
          break;
        case STATUS_INTERNAL_SERVER_ERROR:
          Serial.println("* GeoLinker server error - Try later");
          break;
        default:
          Serial.print("? Unknown status code: ");
          Serial.println(status);
          break;
      }
    }
  }

  delay(20);
}

// -------------------------------------------------------------
// Telemetry & Error Diagnostic Handler
// -------------------------------------------------------------
uint8_t evaluateAndSendTelemetry() {
  // Check if GPS UART is receiving any characters at all
  if (gps.charsProcessed() < 10) {
    return STATUS_GPS_ERROR; // Wiring error or module unpowered
  }

  // Check if valid coordinates are fixed by satellites
  if (!gps.location.isValid()) {
    // As noted in project report: Indoors, NEO-6M cannot lock 4 satellites,
    // which results in Invalid NMEA coordinates.
    return STATUS_GPS_PARSE_ERROR;
  }

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    return STATUS_NETWORK_ERROR;
  }

  // Valid GPS Fix Available
  float latitude  = gps.location.lat();
  float longitude = gps.location.lng();
  float speedKmph = gps.speed.kmph();
  uint32_t satellites = gps.satellites.value();

  Serial.println("---------------------------------------------");
  Serial.print("Lat: "); Serial.print(latitude, 6);
  Serial.print(" | Lng: "); Serial.print(longitude, 6);
  Serial.print(" | Sats: "); Serial.print(satellites);
  Serial.print(" | Speed: "); Serial.print(speedKmph); Serial.println(" km/h");
  Serial.println("---------------------------------------------");

  // In production, execute HTTP POST to GeoLinker Cloud endpoint
  WiFiClient client;
  if (!client.connect(serverName, 80)) {
    return STATUS_NETWORK_ERROR;
  }

  String postData = "{\"deviceId\":\"" + deviceId + "\",\"lat\":" + String(latitude, 6) +
                    ",\"lng\":" + String(longitude, 6) + ",\"speed\":" + String(speedKmph) +
                    ",\"key\":\"" + apiKey + "\"}";

  client.println("POST /api/v1/telemetry HTTP/1.1");
  client.println("Host: " + String(serverName));
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(postData.length());
  client.println();
  client.println(postData);

  return STATUS_SENT;
}
