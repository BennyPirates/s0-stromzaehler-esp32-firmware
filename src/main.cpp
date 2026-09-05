#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include "app_config.h"
#include "optocoupler_inputs.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

namespace {

WebServer server(80);
OptocouplerInputs optocouplerInputs;

constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MAX_MS = 60000;

uint32_t nextWifiAttempt = 0;
uint32_t wifiRetryInterval = WIFI_RETRY_INTERVAL_MS;
bool wifiWasConnected = false;
bool serverStarted = false;
bool mdnsStarted = false;
bool otaStarted = false;

bool hasWifiCredentials() {
  return WIFI_SSID[0] != '\0';
}

String jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (const char character : value) {
    if (character == '"' || character == '\\') {
      escaped += '\\';
    }
    escaped += character;
  }
  return escaped;
}

String uint64ToString(uint64_t value) {
  char buffer[21] = {};
  snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
  return String(buffer);
}

bool parseEnergyWh(const String& value, uint64_t& energyWh) {
  if (value.isEmpty()) {
    return false;
  }

  uint64_t parsed = 0;
  for (const char character : value) {
    if (character < '0' || character > '9') {
      return false;
    }
    const uint8_t digit = static_cast<uint8_t>(character - '0');
    if (parsed > (UINT64_MAX - digit) / 10ULL) {
      return false;
    }
    parsed = parsed * 10ULL + digit;
  }
  energyWh = parsed;
  return true;
}

bool requireMeterWriteAuthentication() {
  if (OTA_PASSWORD[0] == '\0') {
    server.send(503, "application/json", "{\"error\":\"meter_write_auth_not_configured\"}");
    return false;
  }
  if (!server.authenticate("admin", OTA_PASSWORD)) {
    server.requestAuthentication(BASIC_AUTH, "S0 meter write access");
    return false;
  }
  return true;
}

void handleMeterReadingWrite(uint8_t channel) {
  if (!requireMeterWriteAuthentication()) {
    return;
  }

  uint64_t energyWh = 0;
  if (!server.hasArg("energy_wh") || !parseEnergyWh(server.arg("energy_wh"), energyWh)) {
    server.send(400, "application/json", "{\"error\":\"energy_wh_must_be_an_unsigned_integer\"}");
    return;
  }
  if (!optocouplerInputs.setMeterEnergyWh(channel, energyWh)) {
    server.send(503, "application/json", "{\"error\":\"meter_storage_unavailable\"}");
    return;
  }

  const S0InputSnapshot input = optocouplerInputs.snapshot(channel);
  String body = "{\"channel\":" + String(channel + 1) + ",\"name\":\"" +
                jsonEscape(input.name) + "\",\"energy_wh\":" +
                uint64ToString(input.meterEnergyWh) + ",\"energy_kwh\":" +
                String(static_cast<double>(input.meterEnergyWh) / 1000.0, 3) + "}";
  server.send(200, "application/json", body);
}

String ipAddress() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "0.0.0.0";
}

String wifiState() {
  if (!hasWifiCredentials()) {
    return "credentials_missing";
  }
  if (WiFi.status() == WL_CONNECTED) {
    return "connected";
  }
  if (wifiWasConnected) {
    return "reconnecting";
  }
  return "connecting";
}

String statusJson() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  String body = "{";
  body += "\"hostname\":\"" + jsonEscape(DEVICE_HOSTNAME) + "\",";
  body += "\"firmware_version\":\"" + jsonEscape(FIRMWARE_VERSION) + "\",";
  body += "\"uptime_seconds\":" + String(millis() / 1000UL) + ",";
  body += "\"wifi_state\":\"" + wifiState() + "\",";
  body += "\"ip_address\":\"" + ipAddress() + "\",";
  body += "\"rssi\":";
  body += connected ? String(WiFi.RSSI()) : "null";
  body += ",\"s0_inputs\":{\"active_low\":";
  body += OptocouplerInputs::kActiveLow ? "true" : "false";
  body += ",\"debounce_ms\":" + String(OptocouplerInputs::kDebounceUs / 1000UL);
  body += ",\"channels\":[";
  for (uint8_t channel = 0; channel < OptocouplerInputs::kChannelCount; ++channel) {
    const S0InputSnapshot input = optocouplerInputs.snapshot(channel);
    if (channel > 0) {
      body += ",";
    }
    body += "{\"channel\":" + String(channel + 1);
    body += ",\"name\":\"" + jsonEscape(input.name) + "\"";
    body += ",\"gpio\":" + String(input.gpio);
    body += ",\"raw_level\":\"" + String(input.rawLevelHigh ? "high" : "low") + "\"";
    body += ",\"active\":" + String(input.active ? "true" : "false");
    body += ",\"rising_edges\":" + String(input.risingEdges);
    body += ",\"falling_edges\":" + String(input.fallingEdges);
    body += ",\"pulses\":" + String(input.pulses);
    body += ",\"energy_wh\":" + uint64ToString(input.meterEnergyWh);
    body += ",\"energy_kwh\":" + String(static_cast<double>(input.meterEnergyWh) / 1000.0, 3);
    body += ",\"power_w\":" + String(input.estimatedPowerW);
    body += ",\"last_pulse_interval_ms\":";
    body += input.hasPowerEstimate ? String(input.lastPulseIntervalMs) : "null";
    body += ",\"last_pulse_age_ms\":";
    body += input.hasPulse ? String(input.lastPulseAgeMs) : "null";
    body += "}";
  }
  body += "]}";
  body += "}";
  return body;
}

String statusPage() {
  String page = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>S0 Stromzähler ESP32</title>
<style>body{font:16px system-ui,sans-serif;max-width:40rem;margin:2rem auto;padding:0 1rem;color:#17202a}table{border-collapse:collapse;width:100%}td{border-bottom:1px solid #ddd;padding:.55rem}td:first-child{font-weight:600;width:45%}</style>
</head><body><h1>S0 Stromzähler ESP32</h1><table>
)HTML";
  page += String("<tr><td>Hostname</td><td>") + DEVICE_HOSTNAME + "</td></tr>";
  page += String("<tr><td>Firmware version</td><td>") + FIRMWARE_VERSION + "</td></tr>";
  page += "<tr><td>Uptime</td><td>" + String(millis() / 1000UL) + " s</td></tr>";
  page += "<tr><td>Wi-Fi state</td><td>" + wifiState() + "</td></tr>";
  page += "<tr><td>IP address</td><td>" + ipAddress() + "</td></tr>";
  page += "<tr><td>RSSI</td><td>";
  page += WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "n/a";
  page += "</td></tr></table><h2>S0 input diagnostics</h2><p>GPIO INPUT_PULLUP; active-low; debounce " +
          String(OptocouplerInputs::kDebounceUs / 1000UL) + " ms.</p>";
  page += "<table><tr><td>Meter</td><td>GPIO</td><td>State</td><td>Power</td><td>Energy</td><td>Edges (r/f)</td></tr>";
  for (uint8_t channel = 0; channel < OptocouplerInputs::kChannelCount; ++channel) {
    const S0InputSnapshot input = optocouplerInputs.snapshot(channel);
    page += "<tr><td>" + String(input.name) + "</td><td>" + String(input.gpio) + "</td><td>" +
            (input.active ? "active (low)" : "inactive (high)") + "</td><td>" +
            String(input.estimatedPowerW) + " W</td><td>" +
            String(static_cast<double>(input.meterEnergyWh) / 1000.0, 3) + " kWh (" +
            String(input.pulses) + " pulses)</td><td>" + String(input.risingEdges) + " / " +
            String(input.fallingEdges) + "</td></tr>";
  }
  page += R"HTML(</table><p>JSON: <a href="/api/status">/api/status</a></p></body></html>)HTML";
  return page;
}

void startNetworkServices() {
  if (!serverStarted) {
    server.on("/", HTTP_GET, []() { server.send(200, "text/html", statusPage()); });
    server.on("/api/status", HTTP_GET,
              []() { server.send(200, "application/json", statusJson()); });
    server.on("/api/meters/1/reading", HTTP_POST, []() { handleMeterReadingWrite(0); });
    server.on("/api/meters/2/reading", HTTP_POST, []() { handleMeterReadingWrite(1); });
    server.on("/api/meters/3/reading", HTTP_POST, []() { handleMeterReadingWrite(2); });
    server.onNotFound([]() { server.send(404, "text/plain", "Not found\n"); });
    server.begin();
    serverStarted = true;
    Serial.println("[HTTP] Status server listening on port 80");
  }

  if (!mdnsStarted) {
    if (MDNS.begin(DEVICE_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.printf("[MDNS] http://%s.local/\n", DEVICE_HOSTNAME);
    } else {
      Serial.println("[MDNS] ERROR: mDNS start failed");
    }
  }

  if (!otaStarted) {
    ArduinoOTA.setHostname(DEVICE_HOSTNAME);
    if (OTA_PASSWORD[0] != '\0') {
      ArduinoOTA.setPassword(OTA_PASSWORD);
    }
    ArduinoOTA
        .onStart([]() { Serial.println("[OTA] Update started"); })
        .onEnd([]() { Serial.println("[OTA] Update finished; rebooting"); })
        .onProgress([](unsigned int progress, unsigned int total) {
          static unsigned int lastPercent = 101;
          const unsigned int percent = (progress * 100U) / total;
          if (percent != lastPercent && (percent % 10U == 0 || percent == 100U)) {
            Serial.printf("[OTA] Progress: %u%%\n", percent);
            lastPercent = percent;
          }
        })
        .onError([](ota_error_t error) {
          Serial.printf("[OTA] ERROR code=%u\n", static_cast<unsigned int>(error));
        });
    ArduinoOTA.begin();
    otaStarted = true;
    Serial.println("[OTA] Ready for wireless firmware updates");
  }
}

void beginWifiAttempt() {
  if (!hasWifiCredentials()) {
    Serial.println("[WIFI] No local credentials configured; Wi-Fi disabled");
    nextWifiAttempt = millis() + WIFI_RETRY_INTERVAL_MAX_MS;
    return;
  }

  Serial.println("[WIFI] Connecting with local credentials");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  nextWifiAttempt = millis() + wifiRetryInterval;
  wifiRetryInterval = min(wifiRetryInterval * 2U, WIFI_RETRY_INTERVAL_MAX_MS);
}

void maintainWifi() {
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected && !wifiWasConnected) {
    wifiWasConnected = true;
    wifiRetryInterval = WIFI_RETRY_INTERVAL_MS;
    Serial.printf("[WIFI] Connected; IP=%s RSSI=%d dBm\n", ipAddress().c_str(), WiFi.RSSI());
    startNetworkServices();
  } else if (!connected && wifiWasConnected) {
    wifiWasConnected = false;
    otaStarted = false;
    Serial.println("[WIFI] Connection lost; continuing offline and retrying");
  }

  if (!connected && hasWifiCredentials() && static_cast<int32_t>(now - nextWifiAttempt) >= 0) {
    beginWifiAttempt();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\n[BOOT] S0 Stromzähler ESP32 firmware %s\n", FIRMWARE_VERSION);
  Serial.printf("[BOOT] Hostname: %s; reset reason: %d\n", DEVICE_HOSTNAME,
                static_cast<int>(esp_reset_reason()));

  optocouplerInputs.begin();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  beginWifiAttempt();
}

void loop() {
  maintainWifi();
  if (serverStarted) {
    server.handleClient();
  }
  if (otaStarted) {
    ArduinoOTA.handle();
  }
  optocouplerInputs.update();
  delay(2);
}
