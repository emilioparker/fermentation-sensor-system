// Board: ESP32S3 Dev Module
// Flash Size: 16MB (128Mb)
// PSRAM: OPI PSRAM
// Flash Mode: QIO 80MHz
// USB CDC On Boot: Enabled

<<<<<<< HEAD
#include "lcd_display.h"
#include "OneWire.h"
#include "DallasTemperature.h"
#include <Adafruit_SHT4x.h>
#include "wifi_manager.h"
#include <HTTPClient.h>
#include "time_utils.h"
#include "file_manager.h"

#define ONE_WIRE_BUS  2
#define SHT41_SDA_PIN 4
#define SHT41_SCL_PIN 5

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Adafruit_SHT4x sht4 = Adafruit_SHT4x();

#define SENSOR_ID    "thor"
#define DATA_VERSION "02"

// Circular buffer for temperature readings (allocated in PSRAM)
#define PSRAM_READINGS    8640      // 30 days at 5 min
#define FALLBACK_READINGS 100       // internal RAM if PSRAM is unavailable
#define READING_LEN       99
#define UPLOAD_BATCH      200       // entries per POST
#define READ_INTERVAL    5000UL    // 5 seconds
#define SAVE_INTERVAL    300000UL  // 5 minutes
#define BUFFER_FILE      "/buffer.csv"

char (*readingBuffer)[READING_LEN] = nullptr;
int  maxReadings = 0;
=======
// WiFi, DS18B20 temp sensors, LCD and data upload are disabled for now.
// Only SHT41 readings are collected, printed to Serial.
// #include "lcd_display.h"
// #include "OneWire.h"
// #include "DallasTemperature.h"
#include <Wire.h>
#include <Adafruit_SHT4x.h>
// #include "wifi_manager.h"
// #include <HTTPClient.h>
// #include "time_utils.h"
// #include "file_manager.h"

// #define ONE_WIRE_BUS 2
#define SDA_PIN 8
#define SCL_PIN 9

// OneWire oneWire(ONE_WIRE_BUS);
// DallasTemperature sensors(&oneWire);
Adafruit_SHT4x sht4 = Adafruit_SHT4x();

#define SENSOR_ID    "goliat"
#define DATA_VERSION "01"

// WiFi/NTP disabled: use a fixed placeholder datetime instead of buildDatetimeString().
#define DEFAULT_DATETIME "2026-01-01T00:00:00.000000-06:00"

// Circular buffer for temperature readings
#define MAX_READINGS     100
#define READ_INTERVAL    5000UL    // 5 seconds
#define SAVE_INTERVAL    300000UL  // 5 minutes
// #define BUFFER_FILE      "/buffer.csv"

char readingBuffer[MAX_READINGS][99];
>>>>>>> efa39e8 (esp32 client)
int  bufferHead  = 0;
int  bufferCount = 0;

unsigned long lastReadTime = 0;
unsigned long lastSaveTime = 0;

<<<<<<< HEAD
void allocateBuffer() {
  if (psramFound()) {
    readingBuffer = (char (*)[READING_LEN]) ps_malloc(PSRAM_READINGS * READING_LEN);
    maxReadings   = PSRAM_READINGS;
  }
  if (!readingBuffer) {
    Serial.println("PSRAM unavailable, using internal RAM");
    readingBuffer = (char (*)[READING_LEN]) malloc(FALLBACK_READINGS * READING_LEN);
    maxReadings   = FALLBACK_READINGS;
  }
  Serial.print("Buffer capacity: "); Serial.print(maxReadings); Serial.println(" readings");
}

void loadBufferFromFlash() {
  File f = LittleFS.open(BUFFER_FILE, "r");
  if (!f) 
  {
    Serial.print("Buffer file doesn't exist, skipping loading from flash ");
    return;
  }
  while (f.available() && bufferCount < maxReadings) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    strncpy(readingBuffer[bufferHead], line.c_str(), 95);
    readingBuffer[bufferHead][95] = '\0';
    bufferHead = (bufferHead + 1) % maxReadings;
    bufferCount++;
  }
  f.close();
  Serial.print("Loaded "); Serial.print(bufferCount); Serial.println(" entries from flash");
}

int oldestIndex() {
  return (bufferHead - bufferCount + maxReadings) % maxReadings;
}

// POST n entries starting at buffer index start, one entry per line
bool postBatch(int start, int n) {
  String payload = "";
  payload.reserve(n * 72);
  for (int i = 0; i < n; i++) {
    payload += readingBuffer[(start + i) % maxReadings];
    payload += "\n";
  }

  HTTPClient http;
  http.begin("https://cacao.parkerlabs.dev/samples");
  http.addHeader("Content-Type", "text/plain");
  int code = http.POST(payload);
  Serial.print("POST "); Serial.print(n); Serial.print(" entries, status: "); Serial.println(code);
  http.end();

  return code >= 200 && code < 300;
}

void rewriteBufferFile() {
  File f = LittleFS.open(BUFFER_FILE, FILE_WRITE);
  if (!f) {
    Serial.println("Failed to rewrite buffer file");
    return;
  }
  int start = oldestIndex();
  for (int i = 0; i < bufferCount; i++) {
    f.println(readingBuffer[(start + i) % maxReadings]);
  }
  f.close();
}

void sendBuffer() {
  if (bufferCount == 0 || WiFi.status() != WL_CONNECTED) return;

  // Send oldest entries first, in batches; stop at the first failure
  int sent = 0;
  bool success = true;
  while (bufferCount > 0) {
    int n = min(bufferCount, UPLOAD_BATCH);
    if (!postBatch(oldestIndex(), n)) {
      success = false;
      break;
    }
    bufferCount -= n;
    sent += n;
  }

  lcdShowUploadResult(success, sent);

  if (bufferCount == 0) {
    bufferHead = 0;
    deleteFile(LittleFS, BUFFER_FILE);
  } else if (sent > 0) {
    // Partial upload: drop the sent entries from flash so a reboot doesn't resend them
    rewriteBufferFile();
  }
}

void setup() {
  sensors.begin();
  Serial.begin(115200);

  lcdInit();

  Wire1.begin(SHT41_SDA_PIN, SHT41_SCL_PIN);
  if (!sht4.begin(&Wire1)) {
    Serial.println("Couldn't find SHT4x sensor");
  } else {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
  }

  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Serial.println("LittleFS Mount Failed");
  }
  allocateBuffer();
  loadBufferFromFlash();

  wifiSetup();

  lcdShowSyncingTime();
  syncTime();
  lcdClear();
=======
// void loadBufferFromFlash() {
//   File f = LittleFS.open(BUFFER_FILE, "r");
//   if (!f)
//   {
//     Serial.print("Buffer file doesn't exist, skipping loading from flash ");
//     return;
//   }
//   while (f.available() && bufferCount < MAX_READINGS) {
//     String line = f.readStringUntil('\n');
//     line.trim();
//     if (line.length() == 0) continue;
//     strncpy(readingBuffer[bufferHead], line.c_str(), 95);
//     readingBuffer[bufferHead][95] = '\0';
//     Serial.println(readingBuffer[bufferHead]);
//     bufferHead = (bufferHead + 1) % MAX_READINGS;
//     bufferCount++;
//   }
//   f.close();
//   Serial.print("Loaded "); Serial.print(bufferCount); Serial.println(" entries from flash");
// }

// void sendBuffer() {
//   if (bufferCount == 0 || WiFi.status() != WL_CONNECTED) return;
//
//   // Build plain text payload: one entry per line
//   String payload = "";
//   int start = (bufferHead - bufferCount + MAX_READINGS) % MAX_READINGS;
//   for (int i = 0; i < bufferCount; i++) {
//     int idx = (start + i) % MAX_READINGS;
//     payload += readingBuffer[idx];
//     payload += "\n";
//   }
//
//   HTTPClient http;
//   http.begin("https://cacao.parkerlabs.dev/samples");
//   http.addHeader("Content-Type", "text/plain");
//   int sent = bufferCount;
//   int code = http.POST(payload);
//   Serial.print("POST status: "); Serial.println(code);
//   http.end();
//
//   bool success = (code >= 200 && code < 300);
//   lcdShowUploadResult(success, sent);
//
//   if (success) {
//     bufferHead  = 0;
//     bufferCount = 0;
//     deleteFile(LittleFS, BUFFER_FILE);
//   }
// }

void setup() {
  // sensors.begin();
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!sht4.begin()) {
    Serial.println("SHT4x not found");
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);

  // lcdInit();

  // if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
  //   Serial.println("LittleFS Mount Failed");
  // }
  // loadBufferFromFlash();

  // wifiSetup();

  // lcdShowSyncingTime();
  // syncTime();
  // lcdClear();
>>>>>>> efa39e8 (esp32 client)

  // Force first save on the first read
  lastSaveTime = millis() - SAVE_INTERVAL;
}

void loop() {
<<<<<<< HEAD
  reconnectWiFi();
=======
  // reconnectWiFi();
>>>>>>> efa39e8 (esp32 client)

  unsigned long now = millis();

  if (now - lastReadTime >= READ_INTERVAL) {
    lastReadTime = now;

<<<<<<< HEAD
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    sensors.requestTemperatures();
    float tempC0 = sensors.getTempCByIndex(0);
    float tempC1 = sensors.getTempCByIndex(1);
    float tempC2 = sensors.getTempCByIndex(2);

    sensors_event_t shtHumidity, shtTemp;
    bool  ambientOk    = sht4.getEvent(&shtHumidity, &shtTemp);
    float humidity     = shtHumidity.relative_humidity;
    float ambientTemp  = shtTemp.temperature;

    lcdShowReadings(timeinfo, tempC0, tempC1, tempC2, humidity, ambientTemp, ambientOk);
=======
    // struct tm timeinfo;
    // getLocalTime(&timeinfo);

    // sensors.requestTemperatures();
    // float tempC0 = sensors.getTempCByIndex(0);
    // float tempC1 = sensors.getTempCByIndex(1);
    // float tempC2 = sensors.getTempCByIndex(2);
    // float tempC3 = sensors.getTempCByIndex(3);

    sensors_event_t humidityEvent, tempEvent;
    bool  dhtOk    = sht4.getEvent(&humidityEvent, &tempEvent);
    float humidity = humidityEvent.relative_humidity;
    float dhtTemp  = tempEvent.temperature;

    // lcdShowReadings(timeinfo, tempC0, tempC1, tempC2, tempC3, humidity, dhtTemp, dhtOk);
>>>>>>> efa39e8 (esp32 client)

    // Save to circular buffer every 5 minutes
    if (now - lastSaveTime >= SAVE_INTERVAL) {
      lastSaveTime = now;

<<<<<<< HEAD
      char datetime[36];
      buildDatetimeString(datetime, sizeof(datetime), timeinfo);

      // Format: SENSOR_ID,datetime,t0,t1,t2,humidity,ambient_temp
      char t0[7], t1[7], t2[7], hum[7], ambT[7];
      dtostrf(tempC0 != DEVICE_DISCONNECTED_C ? tempC0 : 0, 5, 2, t0);
      dtostrf(tempC1 != DEVICE_DISCONNECTED_C ? tempC1 : 0, 5, 2, t1);
      dtostrf(tempC2 != DEVICE_DISCONNECTED_C ? tempC2 : 0, 5, 2, t2);
      dtostrf(ambientOk ? humidity    : 0, 5, 2, hum);
      dtostrf(ambientOk ? ambientTemp : 0, 5, 2, ambT);

      snprintf(readingBuffer[bufferHead], READING_LEN, "%s,%s,%s,%s,%s,%s,%s,%s",
               DATA_VERSION, SENSOR_ID, datetime,
               t0, t1, t2, hum, ambT);

      char line[100];
      snprintf(line, sizeof(line), "%s\n", readingBuffer[bufferHead]);
      appendFile(LittleFS, BUFFER_FILE, line);

      bufferHead = (bufferHead + 1) % maxReadings;
      if (bufferCount < maxReadings) bufferCount++;

      Serial.print("Saved: "); Serial.println(readingBuffer[(bufferHead - 1 + maxReadings) % maxReadings]);

      sendBuffer();
=======
      // char datetime[36];
      // buildDatetimeString(datetime, sizeof(datetime), timeinfo);

      // Format: DATA_VERSION,SENSOR_ID,datetime,t0,t1,t2,t3,humidity,dht_temp
      // DS18B20 sensors (t0-t3) are disabled; dummy values keep the saved format unchanged.
      char t0[7], t1[7], t2[7], t3[7], hum[7], dhtT[7];
      dtostrf(0, 5, 2, t0);
      dtostrf(0, 5, 2, t1);
      dtostrf(0, 5, 2, t2);
      dtostrf(0, 5, 2, t3);
      dtostrf(dhtOk ? humidity : 0, 5, 2, hum);
      dtostrf(dhtOk ? dhtTemp  : 0, 5, 2, dhtT);

      snprintf(readingBuffer[bufferHead], 99, "%s,%s,%s,%s,%s,%s,%s,%s,%s",
               DATA_VERSION, SENSOR_ID, DEFAULT_DATETIME, t0, t1, t2, t3, hum, dhtT);

      // char line[100];
      // snprintf(line, sizeof(line), "%s\n", readingBuffer[bufferHead]);
      // appendFile(LittleFS, BUFFER_FILE, line);

      bufferHead = (bufferHead + 1) % MAX_READINGS;
      if (bufferCount < MAX_READINGS) bufferCount++;

      Serial.print("Saved: "); Serial.println(readingBuffer[(bufferHead - 1 + MAX_READINGS) % MAX_READINGS]);

      // sendBuffer();
>>>>>>> efa39e8 (esp32 client)
    }
  }
}
