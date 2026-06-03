#pragma once
#include <WiFi.h>
#include <LittleFS.h>
#include "lcd_display.h"

#define MAX_NETWORKS  10
#define MAX_SSID_LEN  32
#define MAX_PASS_LEN  64

typedef struct {
  const char* ssid;
  const char* password;
} WifiNetwork;

char      _ssidStore[MAX_NETWORKS][MAX_SSID_LEN + 1];
char      _passStore[MAX_NETWORKS][MAX_PASS_LEN + 1];
WifiNetwork networks[MAX_NETWORKS];
int       networkCount = 0;

void loadNetworks() {
  File f = LittleFS.open("/wifi.csv", "r");
  if (!f) {
    Serial.println("wifi.csv not found");
    return;
  }
  while (f.available() && networkCount < MAX_NETWORKS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int comma = line.indexOf(',');
    if (comma < 0) continue;
    line.substring(0, comma).toCharArray(_ssidStore[networkCount], MAX_SSID_LEN + 1);
    line.substring(comma + 1).toCharArray(_passStore[networkCount], MAX_PASS_LEN + 1);
    networks[networkCount] = { _ssidStore[networkCount], _passStore[networkCount] };
    networkCount++;
  }
  f.close();
  Serial.print("Loaded "); Serial.print(networkCount); Serial.println(" WiFi networks");
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);
  for (int i = 0; i < networkCount; i++) {
    Serial.print("Trying: "); Serial.println(networks[i].ssid);
    lcdShowTrying(networks[i].ssid);

    WiFi.begin(networks[i].ssid, networks[i].password);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    delay(1000);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
      Serial.print("Attempt "); Serial.print(attempts);
      Serial.print(" - Status: "); Serial.println(WiFi.status());
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to: " + String(networks[i].ssid));
      lcdShowConnected(networks[i].ssid);
      delay(2000);
      return true;
    }
    WiFi.disconnect();
    delay(500);
  }
  Serial.println("All networks failed.");
  return false;
}

void reconnectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  static unsigned long lastAttempt = 0;
  unsigned long now = millis();
  if (now - lastAttempt < 60000UL) return;
  lastAttempt = now;
  Serial.println("WiFi lost, reconnecting...");
  connectWiFi();
}

// Scans available networks, then connects. Halts on failure.
void wifiSetup() {
  loadNetworks();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("Networks found:");
  for (int i = 0; i < n; i++) {
    Serial.print(i); Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" ("); Serial.print(WiFi.RSSI(i)); Serial.println(" dBm)");
    lcdShowNetwork(WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    delay(3000);
  }

  if (!connectWiFi()) {
    lcdShowNoWifi();
    while (true) delay(1000);
  }
}
