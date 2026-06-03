// Board: ESP32S3 Dev Module
// Flash Size: 16MB (128Mb)
// PSRAM: OPI PSRAM
// Flash Mode: QIO 80MHz
// USB CDC On Boot: Enabled

#include "lcd_display.h"
#include "OneWire.h"
#include "DallasTemperature.h"
#include <DHT.h>
#include "wifi_manager.h"
#include <HTTPClient.h>
#include "time_utils.h"
#include "file_manager.h"

#define ONE_WIRE_BUS 2
#define DHT_PIN      4

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DHT dht(DHT_PIN, DHT11);

#define SENSOR_ID    "goliat"
#define DATA_VERSION "01"

// Circular buffer for temperature readings
#define MAX_READINGS     100
#define READ_INTERVAL    5000UL    // 5 seconds
#define SAVE_INTERVAL    300000UL  // 5 minutes
#define BUFFER_FILE      "/buffer.csv"

char readingBuffer[MAX_READINGS][99];
int  bufferHead  = 0;
int  bufferCount = 0;

unsigned long lastReadTime = 0;
unsigned long lastSaveTime = 0;

void loadBufferFromFlash() {
  File f = LittleFS.open(BUFFER_FILE, "r");
  if (!f) 
  {
    Serial.print("Buffer file doesn't exist, skipping loading from flash ");
    return;
  }
  while (f.available() && bufferCount < MAX_READINGS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    strncpy(readingBuffer[bufferHead], line.c_str(), 95);
    readingBuffer[bufferHead][95] = '\0';
    Serial.println(readingBuffer[bufferHead]);
    bufferHead = (bufferHead + 1) % MAX_READINGS;
    bufferCount++;
  }
  f.close();
  Serial.print("Loaded "); Serial.print(bufferCount); Serial.println(" entries from flash");
}

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
  int sent = bufferCount;
  int code = http.POST(payload);
  Serial.print("POST status: "); Serial.println(code);
  http.end();

  bool success = (code >= 200 && code < 300);
  lcdShowUploadResult(success, sent);

  if (success) {
    bufferHead  = 0;
    bufferCount = 0;
    deleteFile(LittleFS, BUFFER_FILE);
  }
}

void setup() {
  sensors.begin();
  dht.begin();
  Serial.begin(115200);

  lcdInit();

  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Serial.println("LittleFS Mount Failed");
  }
  loadBufferFromFlash();

  wifiSetup();

  lcdShowSyncingTime();
  syncTime();
  lcdClear();

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
    float tempC3 = sensors.getTempCByIndex(3);

    float humidity = dht.readHumidity();
    float dhtTemp  = dht.readTemperature();
    bool  dhtOk    = !isnan(humidity) && !isnan(dhtTemp);

    lcdShowReadings(timeinfo, tempC0, tempC1, tempC2, tempC3, humidity, dhtTemp, dhtOk);

    // Save to circular buffer every 5 minutes
    if (now - lastSaveTime >= SAVE_INTERVAL) {
      lastSaveTime = now;

      char datetime[36];
      buildDatetimeString(datetime, sizeof(datetime), timeinfo);

      // Format: SENSOR_ID,datetime,t0,t1,t2,t3,humidity,dht_temp
      char t0[7], t1[7], t2[7], t3[7], hum[7], dhtT[7];
      dtostrf(tempC0 != DEVICE_DISCONNECTED_C ? tempC0 : 0, 5, 2, t0);
      dtostrf(tempC1 != DEVICE_DISCONNECTED_C ? tempC1 : 0, 5, 2, t1);
      dtostrf(tempC2 != DEVICE_DISCONNECTED_C ? tempC2 : 0, 5, 2, t2);
      dtostrf(tempC3 != DEVICE_DISCONNECTED_C ? tempC3 : 0, 5, 2, t3);
      dtostrf(dhtOk ? humidity : 0, 5, 2, hum);
      dtostrf(dhtOk ? dhtTemp  : 0, 5, 2, dhtT);

      snprintf(readingBuffer[bufferHead], 99, "%s,%s,%s,%s,%s,%s,%s,%s,%s",
               DATA_VERSION, SENSOR_ID, datetime,
               t0, t1, t2, t3, hum, dhtT);

      char line[100];
      snprintf(line, sizeof(line), "%s\n", readingBuffer[bufferHead]);
      appendFile(LittleFS, BUFFER_FILE, line);

      bufferHead = (bufferHead + 1) % MAX_READINGS;
      if (bufferCount < MAX_READINGS) bufferCount++;

      Serial.print("Saved: "); Serial.println(readingBuffer[(bufferHead - 1 + MAX_READINGS) % MAX_READINGS]);

      sendBuffer();
    }
  }
}
