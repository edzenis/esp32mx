#include <WiFi.h>                 // Include Wi‑Fi library
#include <HTTPClient.h>           // Include HTTP client library
#include <HTTPUpdate.h>           // Include OTA update library
#include <WiFiClientSecure.h>     // Include secure Wi‑Fi client for HTTPS
#include <Preferences.h>          // Include Preferences to store data in flash

// ===== USER CONFIG =====
const char* WIFI_SSID     = "LMT_0A29";   // Your Wi‑Fi SSID
const char* WIFI_PASSWORD = "nm3MHB9YbT2";// Your Wi‑Fi password

// Full‑length MaintainX API token
const char* MAINTX_TOKEN  =
  "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
  "eyJ1c2VySWQiOjc0MjA4Nywib3JnYW5pemF0aW9uSWQiOjMzMDU2NiwiaWF0IjoxNzUyMTM0ODExLCJzdWIiOiJSRVNUX0FQSV9BVVRIIiwianRpIjoiYmU0ZmE4MWEtZWFmMi00YmY5LTlmYzYtMzFmN2I0NzRiMzljIn0."
  "ZAU4qG6zy_WxHgX065oNeyxmF72sb95tmECCWHJ5T9s";  // Your full token
const char* MAINTX_URL    = "https://api.getmaintainx.com/v1/meterreadings"; // MaintainX API endpoint
const char* METER_ID      = "423679";          // Your meter ID

// OTA URLs for version and firmware
const char* VERSION_URL   = "https://raw.githubusercontent.com/edzenis/esp32mx/main/version.txt";  // Raw version file
const char* FIRMWARE_URL  = "https://raw.githubusercontent.com/edzenis/esp32mx/main/firmware.bin"; // Raw firmware binary

const int   SENSOR_PIN    = 4;                 // GPIO pin for your sensor

// ===== STATE =====
Preferences prefs;        // Preferences object for flash storage
unsigned long activeMs     = 0; // Total time sensor was HIGH in ms
unsigned long lastLoopMs   = 0; // Timestamp of last loop for delta calc
unsigned long lastReportMs = 0; // Timestamp of last MaintainX report
unsigned long lastOtaMs    = 0; // Timestamp of last OTA check

void setup() {
  Serial.begin(115200);        // Start Serial for debug output
  delay(100);                  // Wait a moment

  prefs.begin("app", false);   // Open Preferences namespace "app"
  activeMs = prefs.getULong("activeMs", 0); // Load saved activeMs
  float ver = prefs.getFloat("ver", 0.0);    // Load saved firmware version
  Serial.println("Boot version: " + String(ver)); // Print loaded version

  pinMode(SENSOR_PIN, INPUT_PULLDOWN); // Set sensor pin as input with pull-down

  // Connect to Wi‑Fi
  Serial.print("Wi‑Fi connecting to "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);      // Start Wi‑Fi connection
  while (WiFi.status() != WL_CONNECTED) {    // Wait until connected
    delay(500); Serial.print('.');           // Print dots while waiting
  }
  Serial.println();
  Serial.println("Wi‑Fi connected: " + WiFi.localIP().toString()); // Print IP

  // Initialize timing variables
  lastLoopMs   = millis();  // Set lastLoopMs to now
  lastReportMs = lastLoopMs; // Set lastReportMs to now
  lastOtaMs    = lastLoopMs; // Set lastOtaMs to now
}

void loop() {
  unsigned long now   = millis();            // Get current time
  unsigned long delta = now - lastLoopMs;    // Calculate time since last loop
  lastLoopMs = now;                          // Update lastLoopMs

  // Count sensor HIGH time
  if (digitalRead(SENSOR_PIN) == HIGH) {     // If sensor is HIGH
    activeMs += delta;                       // Add delta to activeMs
  }

  // REPORT to MaintainX every 60 seconds
  if (now - lastReportMs >= 60000UL) {       // If 60s passed
    Serial.println("Reporting to MaintainX..."); // Debug message
    HTTPClient http;                         // Create HTTPClient
    http.begin(MAINTX_URL);                  // Initialize with API URL
    http.addHeader("Authorization", MAINTX_TOKEN); // Add auth header
    http.addHeader("Content-Type", "application/json"); // Add content type

    unsigned long secs = activeMs / 1000;    // Convert ms to seconds
    String body = "[{\"meterId\":" + String(METER_ID)
                + ",\"value\":" + String(secs) + "}]"; // Build JSON

    int code = http.POST(body);              // Send POST request
    Serial.println("Report HTTP code: " + String(code)); // Print response code
    if (code >= 200 && code < 300) {         // If success
      prefs.putULong("activeMs", activeMs);  // Save activeMs to flash
      Serial.println("MaintainX data saved"); // Debug message
    } else {                                 // If failure
      Serial.println("MaintainX report failed"); // Debug message
    }
    http.end();                              // Close HTTP connection
    lastReportMs = now;                      // Update lastReportMs
  }

  // OTA check every 60 seconds
  if (now - lastOtaMs >= 60000UL) {          // If 60s passed
    Serial.println("\n=== OTA Check ===");   // Debug message

    float oldVer = prefs.getFloat("ver", 0.0); // Read saved version
    Serial.print("Saved version: "); Serial.println(oldVer); // Print it

    WiFiClientSecure client;                 // Create secure client
    client.setInsecure();                    // Skip SSL cert check

    HTTPClient httpV;                        // HTTP client for version
    Serial.println("Fetching version.txt..."); // Debug message
    httpV.begin(client, VERSION_URL);        // Initialize with version URL
    int vcode = httpV.GET();                 // Send GET request
    if (vcode == HTTP_CODE_OK) {             // If HTTP 200
      String newVerStr = httpV.getString();  // Read response
      newVerStr.trim();                      // Trim whitespace/newlines
      Serial.print("Raw repo version: "); Serial.println(newVerStr); // Print it

      // Strip leading 'v' if present
      if (newVerStr.startsWith("v") || newVerStr.startsWith("V")) {
        newVerStr = newVerStr.substring(1);  // Remove first char
        Serial.print("Stripped 'v', now: "); Serial.println(newVerStr); // Print change
      }

      float newVer = newVerStr.toFloat();    // Convert to float
      Serial.print("Parsed repo version: "); Serial.println(newVer); // Print

      // If a newer version is available, start OTA
      if (newVer > oldVer) {
        Serial.println("New version found! Starting OTA..."); // Debug

        HTTPUpdate httpUpdate;               // Create updater object
        httpUpdate.rebootOnUpdate(false);    // We will reboot manually
        Serial.println("Downloading + flashing firmware.bin..."); // Debug

        t_httpUpdate_return ret = httpUpdate.update(client, FIRMWARE_URL); // OTA pull & flash

        if (ret == HTTP_UPDATE_OK) {         // If OTA succeeded
          Serial.println("OTA successful!"); // Debug message
          prefs.putFloat("ver", newVer);     // Save new version
          Serial.println("Rebooting now..."); // Debug message
          ESP.restart();                     // Reboot into new firmware
        } else {                             // If OTA failed
          Serial.printf("HTTPUpdate failed (%d): %s\n",
            httpUpdate.getLastError(),
            httpUpdate.getLastErrorString().c_str()); // Print error
        }
      } else {                               // If no update needed
        Serial.println("No newer version");  // Debug message
      }
    } else {                                 // If version fetch failed
      Serial.printf("version.txt fetch failed, HTTP %d\n", vcode); // Print error
    }
    httpV.end();                             // Close version request
    lastOtaMs = now;                         // Update lastOtaMs
    Serial.println("=== OTA Done ===\n");    // Debug message
  }
}
