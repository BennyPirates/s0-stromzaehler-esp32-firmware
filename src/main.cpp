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
  page += R"HTML(</td></tr></table><p>JSON: <a href="/api/status">/api/status</a></p></body></html>)HTML";
  return page;
}

void startNetworkServices() {
  if (!serverStarted) {
    server.on("/", HTTP_GET, []() { server.send(200, "text/html", statusPage()); });
    server.on("/api/status", HTTP_GET,
              []() { server.send(200, "application/json", statusJson()); });
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

  Serial.printf("[WIFI] Connecting to '%s'\n", WIFI_SSID);
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
