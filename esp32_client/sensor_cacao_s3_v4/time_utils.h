#pragma once
#include "time.h"

const long gmtOffset_sec      = -6 * 3600;
const int  daylightOffset_sec = 0;

// Configures NTP and blocks until time is synced.
void syncTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
    Serial.println("Waiting for NTP...");
  }
  Serial.println("Time synced");
}

// Writes an ISO-8601 datetime string with fixed timezone offset into `out`.
// Requires `out` to be at least 36 bytes.
void buildDatetimeString(char* out, size_t size, const struct tm& timeinfo) {
  strftime(out, size, "%Y-%m-%dT%H:%M:%S.000000", &timeinfo);
  char tz[7];
  char tzSign = gmtOffset_sec >= 0 ? '+' : '-';
  int  tzHours = abs(gmtOffset_sec / 3600);
  sprintf(tz, "%c%02d:00", tzSign, tzHours);
  strncat(out, tz, size - strlen(out) - 1);
}
