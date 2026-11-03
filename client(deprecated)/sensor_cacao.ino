#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "OneWire.h"
#include "DallasTemperature.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

#define ONE_WIRE_BUS 2

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Set the LCD address to 0x27 or 0x3F depending on your module
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Define custom I2C pins
#define SDA_PIN 8
#define SCL_PIN 9

#define SENSOR_ID "baltasar"

typedef struct {
  const char* ssid;
  const char* password;
} WifiNetwork;

// Add more networks here as needed
WifiNetwork networks[] = {
  { "MOTMOT-ENG",     "PrimeroPaPachus" },
  { "MOTMOT-2.4GHZ",  "PrimeroPaPachus" },
  { "LaCarbonera",    "PrimeroPaPachus" },
};
const int networkCount = sizeof(networks) / sizeof(networks[0]);

// Circular buffer for temperature readings
#define MAX_READINGS     100
#define READ_INTERVAL    5000UL    // 5 seconds
#define SAVE_INTERVAL    300000UL  // 5 minutes

char readingBuffer[MAX_READINGS][80];
int  bufferHead  = 0;
int  bufferCount = 0;

unsigned long lastReadTime = 0;
unsigned long lastSaveTime = 0;

// UTC offset in seconds: adjust for your timezone
// e.g. UTC-6 = -6 * 3600 = -21600
const long  gmtOffset_sec     = -6 * 3600;
const int   daylightOffset_sec = 0;

void sendBuffer() {
  if (bufferCount == 0 || WiFi.status() != WL_CONNECTED) return;

  // Build plain text payload: one entry per line
  String payload = "";
  int start = (bufferHead - bufferCount + MAX_READINGS) % MAX_READINGS;
  for (int i = 0; i < bufferCount; i++) {
    int idx = (start + i) % MAX_READINGS;
    payload += readingBuffer[idx];
    payload += "\n";
  }

  HTTPClient http;
  http.begin("https://cacao.parkerlabs.dev/samples");
  http.addHeader("Content-Type", "text/plain");
  int code = http.POST(payload);
  Serial.print("POST status: "); Serial.println(code);
  http.end();
}

// Tries each network in order. Returns true if connected.
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);
  for (int i = 0; i < networkCount; i++) {
    Serial.print("Trying: "); Serial.println(networks[i].ssid);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Trying:");
    lcd.setCursor(0, 1); lcd.print(networks[i].ssid);

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
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Connected:");
      lcd.setCursor(0, 1); lcd.print(networks[i].ssid);
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
  Serial.println("WiFi lost, reconnecting...");
  connectWiFi(); // don't halt on failure, will retry next loop
}

void setup() {
  sensors.begin();
  Serial.begin(9600);

  // Initialize the I2C bus with custom SDA and SCL pins
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize the LCD
  lcd.init();
  lcd.backlight();

  // Scan and display available networks
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("Networks found:");
  for (int i = 0; i < n; i++) {
    Serial.print(i); Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" ("); Serial.print(WiFi.RSSI(i)); Serial.println(" dBm)");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(WiFi.SSID(i));
    lcd.setCursor(0, 1); lcd.print(WiFi.RSSI(i)); lcd.print(" dBm");
    delay(3000);
  }

  if (!connectWiFi()) {
    lcd.clear();
    lcd.print("No WiFi found");
    while (true) delay(1000);
  }

  // Sync time via NTP
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
  lcd.clear();
  lcd.print("Syncing time...");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
    Serial.println("Waiting for NTP...");
  }
  Serial.println("Time synced");
  lcd.clear();

  // Force first save on the first read
  lastSaveTime = millis() - SAVE_INTERVAL;
}

void loop() {
  reconnectWiFi();

  unsigned long now = millis();

  if (now - lastReadTime >= READ_INTERVAL) {
    lastReadTime = now;

    struct tm timeinfo;
    getLocalTime(&timeinfo);

    sensors.requestTemperatures();
    float tempC0 = sensors.getTempCByIndex(0);
    float tempC1 = sensors.getTempCByIndex(1);
    float tempC2 = sensors.getTempCByIndex(2);

    // Serial output
    // Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
    // Serial.print("Sensor 1: "); Serial.println(tempC0 != DEVICE_DISCONNECTED_C ? tempC0 : -999);
    // Serial.print("Sensor 2: "); Serial.println(tempC1 != DEVICE_DISCONNECTED_C ? tempC1 : -999);
    // Serial.print("Sensor 3: "); Serial.println(tempC2 != DEVICE_DISCONNECTED_C ? tempC2 : -999);

    // LCD line 1: "MM/DD HH:MM:SS"
    lcd.clear();
    lcd.setCursor(0, 0);
    char timeBuf[17];
    strftime(timeBuf, sizeof(timeBuf), "%m/%d %H:%M:%S", &timeinfo);
    lcd.print(timeBuf);

    // LCD line 2: "28.5 27.1 29.0"
    lcd.setCursor(0, 1);
    char buf[5];
    float temps[3] = {tempC0, tempC1, tempC2};
    for (int i = 0; i < 3; i++) {
      if (i > 0) lcd.print(" ");
      if (temps[i] != DEVICE_DISCONNECTED_C) {
        dtostrf(temps[i], 4, 1, buf);
        lcd.print(buf);
      } else {
        lcd.print(" ---");
      }
    }

    // Save to circular buffer every 5 minutes
    if (now - lastSaveTime >= SAVE_INTERVAL) {
      lastSaveTime = now;

      // Build datetime with timezone offset: 2026-02-11T16:43:03.000000-06:00
      char datetime[36];
      strftime(datetime, sizeof(datetime), "%Y-%m-%dT%H:%M:%S.000000", &timeinfo);
      char tz[7];
      int tzHours = abs(gmtOffset_sec / 3600);
      char tzSign = gmtOffset_sec >= 0 ? '+' : '-';
      sprintf(tz, "%c%02d:00", tzSign, tzHours);
      strcat(datetime, tz);

      // Format: SENSOR_ID,datetime,t0,t1,t2
      char t0[7], t1[7], t2[7];
      dtostrf(tempC0 != DEVICE_DISCONNECTED_C ? tempC0 : -999, 5, 2, t0);
      dtostrf(tempC1 != DEVICE_DISCONNECTED_C ? tempC1 : -999, 5, 2, t1);
      dtostrf(tempC2 != DEVICE_DISCONNECTED_C ? tempC2 : -999, 5, 2, t2);

      snprintf(readingBuffer[bufferHead], 80, "%s,%s,%s,%s,%s",
               SENSOR_ID, datetime,
               t0, t1, t2);

      bufferHead = (bufferHead + 1) % MAX_READINGS;
      if (bufferCount < MAX_READINGS) bufferCount++;

      Serial.print("Saved: "); Serial.println(readingBuffer[(bufferHead - 1 + MAX_READINGS) % MAX_READINGS]);

      sendBuffer();
    }
  }
}