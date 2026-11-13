// Board: ESP32S3 Dev Module
// Flash Size: 16MB (128Mb)
// PSRAM: OPI PSRAM
// Flash Mode: QIO 80MHz
// USB CDC On Boot: Enabled

#include "lcd_display.h"
#include "OneWire.h"
#include "DallasTemperature.h"
#include "DHT.h"
#include "wifi_manager.h"
#include <HTTPClient.h>
#include "time_utils.h"
#include "file_manager.h"
#include "secrets.h"

#define ONE_WIRE_BUS  2

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// DHT11 inside the enclosure, for device health (internal temp/humidity)
#define DHT_PIN  4
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

#define SENSOR_ID    "sentry"
#define DATA_VERSION "05"
#define SAMPLES_URL  "https://cacao.parkerlabs.dev/api/monitor/samples"

// Circular buffer for temperature readings (allocated in PSRAM)
#define PSRAM_READINGS    8640      // 30 days at 5 min
#define FALLBACK_READINGS 100       // internal RAM if PSRAM is unavailable
#define READING_LEN       99
#define UPLOAD_BATCH      200       // entries per POST (new endpoint allows max 500)
#define READ_INTERVAL    5000UL    // 5 seconds
#define SAVE_INTERVAL    300000UL  // 5 minutes
#define BUFFER_FILE      "/buffer.csv"
#define HEALTH_INTERVAL  300000UL  // 5 minutes
#define HEALTH_VERSION   "01"

#ifndef HEALTH_URL
#define HEALTH_URL "https://cacao.parkerlabs.dev/api/monitor/health"
#endif

char (*readingBuffer)[READING_LEN] = nullptr;
int  maxReadings = 0;
int  bufferHead  = 0;
int  bufferCount = 0;

unsigned long lastReadTime = 0;
unsigned long lastSaveTime = 0;
unsigned long lastHealthTime = 0;

// Last valid DHT11 reading (only the latest is kept)
float internalTemp     = NAN;
float internalHumidity = NAN;

// DS18B20 returns -127 when unreachable and 85 (power-on reset value) before
// its first conversion completes; neither is a real reading.
bool isValidTemp(float t) {
  return t != DEVICE_DISCONNECTED_C && t != 85.0f;
}

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

// Append one buffer line (VERSION,SENSOR_ID,datetime,t0,t1,t2[,...]) to `json`
// as a reading object, preceded by a comma if `comma`. Returns false (and
// appends nothing) if the line is malformed.
bool appendReadingJson(String& json, const char* line, bool comma) {
  String f[6];
  int field = 0, from = 0;
  String s(line);
  while (field < 6) {
    int comma = s.indexOf(',', from);
    f[field++] = (comma < 0) ? s.substring(from) : s.substring(from, comma);
    if (comma < 0) break;
    from = comma + 1;
  }
  if (field < 6) return false;
  for (int i = 0; i < 6; i++) f[i].trim();

  char obj[160];
  snprintf(obj, sizeof(obj),
           "{\"time\":\"%s\",\"lower\":%.2f,\"middle\":%.2f,\"upper\":%.2f,\"format_version\":\"%s\"}",
           f[2].c_str(), f[3].toFloat(), f[4].toFloat(), f[5].toFloat(), f[0].c_str());
  if (comma) json += ",";
  json += obj;
  return true;
}

// POST n entries to the monitor readings API as JSON
bool postBatchMonitor(int start, int n) {
  String payload = "{\"monitor_id\":\"" SENSOR_ID "\",\"readings\":[";
  payload.reserve(n * 110 + 40);
  int added = 0;
  for (int i = 0; i < n; i++) {
    if (appendReadingJson(payload, readingBuffer[(start + i) % maxReadings], added > 0)) added++;
  }
  payload += "]}";
  if (added == 0) return true;  // nothing valid to send; don't block the queue

  HTTPClient http;
  http.begin(MONITOR_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " MONITOR_API_KEY);
  int code = http.POST(payload);
  Serial.print("POST monitor "); Serial.print(added); Serial.print(" readings, status: "); Serial.println(code);
  http.end();

  return code >= 200 && code < 300;
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
  http.begin(SAMPLES_URL);
  http.addHeader("Content-Type", "text/plain");
  int code = http.POST(payload);
  Serial.print("POST "); Serial.print(n); Serial.print(" entries, status: "); Serial.println(code);
  http.end();

  return code >= 200 && code < 300;
}

// Read the DHT11 and keep the value if it's valid; on a failed read the
// previous reading is retained.
void readInternalSensor() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    Serial.println("DHT11 read failed");
    return;
  }
  internalTemp     = t;
  internalHumidity = h;
}

// POST device health as "version,sensor_id,temp,humidity,uptime_s".
// Uptime (not a wall-clock boot time) so it is correct even if NTP sync failed;
// the server derives the power-on time from it.
bool sendHealth() {
  if (isnan(internalTemp) || isnan(internalHumidity)) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  // esp_timer is 64-bit microseconds, so unlike millis() it doesn't wrap after 49 days
  unsigned long long uptimeS = esp_timer_get_time() / 1000000ULL;
  char payload[80];
  snprintf(payload, sizeof(payload), "%s,%s,%.1f,%.1f,%llu",
           HEALTH_VERSION, SENSOR_ID, internalTemp, internalHumidity, uptimeS);

  HTTPClient http;
  http.begin(HEALTH_URL);
  http.addHeader("Content-Type", "text/plain");
  http.addHeader("Authorization", "Bearer " MONITOR_API_KEY);
  int code = http.POST((uint8_t*)payload, strlen(payload));
  Serial.print("POST health "); Serial.print(payload); Serial.print(", status: "); Serial.println(code);
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
    // New API first: re-sending to it is harmless, so if the old endpoint
    // fails afterwards the retry won't create duplicates there.
    if (!postBatchMonitor(oldestIndex(), n) || !postBatch(oldestIndex(), n)) {
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
  Serial.begin(115200);
  delay(500);  // let the 1-Wire sensors power up before enumerating
  sensors.begin();
  sensors.requestTemperatures();  // warm-up conversion; result discarded
  dht.begin();

  lcdInit();

  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Serial.println("LittleFS Mount Failed");
  }
  allocateBuffer();
  loadBufferFromFlash();

  wifiSetup();

  lcdShowSyncingTime();
  syncTime();
  lcdClear();

  // Force first save on the first read
  lastSaveTime = millis() - SAVE_INTERVAL;
  lastHealthTime = millis() - HEALTH_INTERVAL;
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

    lcdShowReadings(timeinfo, tempC0, tempC1, tempC2);

    readInternalSensor();
    // Only the latest reading matters, so a failed send just waits for the next interval
    if (now - lastHealthTime >= HEALTH_INTERVAL && !isnan(internalTemp)) {
      lastHealthTime = now;
      sendHealth();
    }

    // Save to circular buffer every 5 minutes
    // Skip (and retry on the next read) if no sensor gave a real value, e.g. right after boot
    bool anyValid = isValidTemp(tempC0) || isValidTemp(tempC1) || isValidTemp(tempC2);
    if (!anyValid) Serial.println("No valid temperature reading, skipping save");

    if (anyValid && now - lastSaveTime >= SAVE_INTERVAL) {
      lastSaveTime = now;

      char datetime[36];
      buildDatetimeString(datetime, sizeof(datetime), timeinfo);

      // Format (v04): DATA_VERSION,SENSOR_ID,datetime,t0,t1,t2
      char t0[7], t1[7], t2[7];
      dtostrf(isValidTemp(tempC0) ? tempC0 : 0, 5, 2, t0);
      dtostrf(isValidTemp(tempC1) ? tempC1 : 0, 5, 2, t1);
      dtostrf(isValidTemp(tempC2) ? tempC2 : 0, 5, 2, t2);

      snprintf(readingBuffer[bufferHead], READING_LEN, "%s,%s,%s,%s,%s,%s",
               DATA_VERSION, SENSOR_ID, datetime,
               t0, t1, t2);

      char line[100];
      snprintf(line, sizeof(line), "%s\n", readingBuffer[bufferHead]);
      appendFile(LittleFS, BUFFER_FILE, line);

      bufferHead = (bufferHead + 1) % maxReadings;
      if (bufferCount < maxReadings) bufferCount++;

      Serial.print("Saved: "); Serial.println(readingBuffer[(bufferHead - 1 + maxReadings) % maxReadings]);

      sendBuffer();
    }
  }
}
