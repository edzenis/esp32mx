#include <WiFi.h>                 // Let ESP32 talk to Wi-Fi
#include <HTTPClient.h>           // Let ESP32 make HTTP requests
#include <HTTPUpdate.h>           // Let ESP32 update its own firmware
#include <WiFiClientSecure.h>     // Let ESP32 do HTTPS requests
#include <Preferences.h>          // Let ESP32 save data in its flash

/*
 * CHANGE LOG
 * ----------
 * 2025-07-17: Added per-second Serial countdown for both the next MaintainX report
 *             and the next OTA check.
 * 2025-07-17b: Debug update – ensure countdown prints immediately after setup,
 *             and prints reliably even if long blocking HTTP calls occur.
 *             (We print BEFORE running report/OTA blocks each loop.)
 * test for OTA server
 */

// ===== USER CONFIG =====
const char* WIFI_SSID     = "LMT_0A29";    // Your Wi-Fi SSID
const char* WIFI_PASSWORD = "nm3MHB9YbT2"; // Your Wi-Fi password

// **Full-length** MaintainX API token—never omit or truncate this!
const char* MAINTX_TOKEN  =
  "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
  "eyJ1c2VySWQiOjc0MjA4Nywib3JnYW5pemF0aW9uSWQiOjMzMDU2NiwiaWF0IjoxNzUyMTM0ODExLCJzdWIiOiJSRVNUX0FQSV9BVVRIIiwianRpIjoiYmU0ZmE4MWEtZWFmMi00YmY5LTlmYzYtMzFmN2I0NzRiMzljIn0."
  "ZAU4qG6zy_WxHgX065oNeyxmF72sb95tmECCWHJ5T9s";  // Your exact token
const char* MAINTX_URL    = "https://api.getmaintainx.com/v1/meterreadings"; // API endpoint
const char* METER_ID      = "423679";          // Your meter ID

// OTA update URLs
const char* VERSION_URL   = "https://raw.githubusercontent.com/edzenis/esp32mx/main/version.txt";  // Raw version file
const char* FIRMWARE_URL  = "https://raw.githubusercontent.com/edzenis/esp32mx/main/firmware.bin"; // Raw firmware binary

const int   SENSOR_PIN    = 4;                 // GPIO pin for your sensor

// ==== TIMING CONSTANTS ====
const unsigned long REPORT_INTERVAL    = 60000UL; // 60,000 ms = 60 s between reports
const unsigned long OTA_INTERVAL       = 60000UL; // 60 s between OTA checks
const unsigned long COUNTDOWN_INTERVAL = 1000UL;  // 1 s between countdown prints

// ===== STATE =====
Preferences prefs;            // Flash storage for version & meter data
unsigned long activeMs        = 0; // How many ms sensor was HIGH
unsigned long lastLoopMs      = 0; // Timestamp of last loop()
unsigned long lastReportMs    = 0; // Timestamp of last MaintainX report
unsigned long lastOtaMs       = 0; // Timestamp of last OTA check
unsigned long lastCountdownMs = 0; // Timestamp of last countdown print

// -----------------------------------------------------------------------------
// Helper: print combined countdown line (report + OTA)
// Call with current millis().
// -----------------------------------------------------------------------------
static inline void printCountdown(unsigned long now) {
  unsigned long sinceReport = now - lastReportMs;
  unsigned long untilReport = (sinceReport < REPORT_INTERVAL) ? (REPORT_INTERVAL - sinceReport) : 0;
  unsigned long secsReportLeft = (untilReport + 999UL) / 1000UL;  // round up to s

  unsigned long sinceOta = now - lastOtaMs;
  unsigned long untilOta = (sinceOta < OTA_INTERVAL) ? (OTA_INTERVAL - sinceOta) : 0;
  unsigned long secsOtaLeft = (untilOta + 999UL) / 1000UL;        // round up to s

  Serial.print("Next report in "); Serial.print(secsReportLeft); Serial.print(" s");
  Serial.print(" | Next OTA check in "); Serial.print(secsOtaLeft); Serial.println(" s");
}

void setup() {
  Serial.begin(115200);         // Open Serial for debug
  delay(100);                   // Wait a bit for Serial
  Serial.println();
  Serial.println("=== Booting ESP32MX (Countdown build) ===");

  prefs.begin("app", false);    // Open flash storage named "app"
  activeMs = prefs.getULong("activeMs", 0);  // Load saved sensor time
  float ver = prefs.getFloat("ver", 0.0);    // Load saved firmware version
  Serial.println("Boot version: " + String(ver)); // Print it

  pinMode(SENSOR_PIN, INPUT_PULLDOWN); // Set sensor pin as input with pull-down

  // Connect to Wi-Fi
  Serial.print("Wi-Fi connecting to "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Start Wi-Fi
  while (WiFi.status() != WL_CONNECTED) { // Wait until connected
    delay(500);
    Serial.print('.');                // Print a dot every half-second
  }
  Serial.println();
  Serial.println("Wi-Fi connected: " + WiFi.localIP().toString());

  // Initialize our timers
  lastLoopMs      = millis();
  lastReportMs    = lastLoopMs;
  lastOtaMs       = lastLoopMs;
  lastCountdownMs = lastLoopMs;  // force immediate 1s countdown schedule

  // Give immediate visual feedback so you know countdown code is active.
  Serial.println("Countdown timers armed. First countdown line in ~1s...");
}

void loop() {
  unsigned long now   = millis();             // Current time in ms
  unsigned long delta = now - lastLoopMs;     // Time since last loop()
  lastLoopMs = now;                           // Save for next iteration

  // --- Count sensor active time ---
  if (digitalRead(SENSOR_PIN) == HIGH) {
    activeMs += delta;                        // Accumulate ms HIGH
  }

  // --- Per-second countdown prints (MaintainX report + OTA check) ---
  if (now - lastCountdownMs >= COUNTDOWN_INTERVAL) {  // once per second
    lastCountdownMs = now;                            // reset countdown timer
    printCountdown(now);                              // do the print
  }

  // --- REPORT to MaintainX every REPORT_INTERVAL ---
  unsigned long sinceReport = now - lastReportMs;
  if (sinceReport >= REPORT_INTERVAL) {
    Serial.println("Reporting to MaintainX...");
    HTTPClient http;
    http.begin(MAINTX_URL);                  // Set API endpoint
    http.addHeader("Authorization", MAINTX_TOKEN);  // Add full token
    http.addHeader("Content-Type", "application/json");

    unsigned long secs = activeMs / 1000;     // Convert ms → seconds
    String body = "[{\"meterId\":" + String(METER_ID)
                + ",\"value\":" + String(secs) + "}]"; // JSON payload

    int code = http.POST(body);               // Send data
    Serial.println("Report HTTP code: " + String(code));
    if (code >= 200 && code < 300) {          // On success
      prefs.putULong("activeMs", activeMs);   // Save cumulative time
      Serial.println("MaintainX data saved");
    } else {
      Serial.println("MaintainX report failed");
    }
    http.end();                               // Close HTTP connection
    lastReportMs = now;                       // Reset timer

    // Immediately show fresh countdown after a report (avoids waiting up to 1s)
    printCountdown(now);
  }

  // --- OTA update every OTA_INTERVAL ---
  if (now - lastOtaMs >= OTA_INTERVAL) {
    Serial.println();
    Serial.println("=== OTA Check ===");

    float oldVer = prefs.getFloat("ver", 0.0); // Read saved version
    Serial.print("Saved version: "); Serial.println(oldVer);

    // Fetch version.txt
    WiFiClientSecure client;
    client.setInsecure();                     // Skip SSL cert check (NOT for production)
    HTTPClient httpV;
    Serial.println("Fetching version.txt...");
    httpV.begin(client, VERSION_URL);
    int vcode = httpV.GET();
    if (vcode == HTTP_CODE_OK) {
      String newVerStr = httpV.getString();
      newVerStr.trim();
      Serial.print("Raw repo version: "); Serial.println(newVerStr);

      // Remove leading 'v' if present
      if (newVerStr.startsWith("v") || newVerStr.startsWith("V")) {
        newVerStr = newVerStr.substring(1);
        Serial.print("Stripped 'v', now: "); Serial.println(newVerStr);
      }

      float newVer = newVerStr.toFloat();    // Parse to float
      Serial.print("Parsed repo version: "); Serial.println(newVer);

      // If a newer version is available, start OTA
      if (newVer > oldVer) {
        Serial.println("New version found! Starting OTA...");

        HTTPUpdate httpUpdate;               // OTA updater
        httpUpdate.rebootOnUpdate(false);    // We'll reboot manually
        Serial.println("Downloading + flashing firmware.bin...");
        t_httpUpdate_return ret = httpUpdate.update(client, FIRMWARE_URL);

        if (ret == HTTP_UPDATE_OK) {         // If OTA succeeded
          Serial.println("OTA successful!");
          prefs.putFloat("ver", newVer);     // Save the new version
          Serial.println("Rebooting now...");
          ESP.restart();                      // Reboot into new firmware
        } else {                              // If OTA failed
          Serial.print("HTTPUpdate failed (error ");
          Serial.print(httpUpdate.getLastError());
          Serial.print("): ");
          Serial.println(httpUpdate.getLastErrorString());
        }
      } else {
        Serial.println("No newer version");  // Already up-to-date
      }
    } else {
      Serial.print("version.txt fetch failed, HTTP ");
      Serial.println(vcode);
    }
    httpV.end();                              // Clean up
    lastOtaMs = now;                          // Reset OTA timer
    Serial.println("=== OTA Done ===");
    Serial.println();

    // Immediately show fresh countdown after OTA (avoids waiting up to 1s)
    printCountdown(now);
  }
}
