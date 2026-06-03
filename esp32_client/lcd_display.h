#pragma once
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DallasTemperature.h"
#include "time.h"

#define SDA_PIN 8
#define SCL_PIN 9

LiquidCrystal_I2C lcd(0x27, 16, 2);

void lcdInit() {
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
}

void lcdClear() {
  lcd.clear();
}

void lcdShowNetwork(const char* ssid, int rssi) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(ssid);
  lcd.setCursor(0, 1); lcd.print(rssi); lcd.print(" dBm");
}

void lcdShowTrying(const char* ssid) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Trying:");
  lcd.setCursor(0, 1); lcd.print(ssid);
}

void lcdShowConnected(const char* ssid) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Connected:");
  lcd.setCursor(0, 1); lcd.print(ssid);
}

void lcdShowNoWifi() {
  lcd.clear();
  lcd.print("No WiFi found");
}

void lcdShowSyncingTime() {
  lcd.clear();
  lcd.print("Syncing time...");
}

void lcdShowUploadResult(bool success, int count) {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (success) {
    lcd.print("Upload OK");
  } else {
    lcd.print("Upload FAILED");
  }
  lcd.setCursor(0, 1);
  lcd.print(count);
  lcd.print(" entries");
}

void lcdShowReadings(const struct tm& timeinfo,
                     float tempC0, float tempC1, float tempC2, float tempC3,
                     float humidity, float dhtTemp, bool dhtOk) {
  lcd.clear();
  lcd.setCursor(0, 0);
  char timeBuf[17];
  strftime(timeBuf, sizeof(timeBuf), "%m/%d %H:%M:%S", &timeinfo);
  lcd.print(timeBuf);

  static bool showDHT = false;
  showDHT = !showDHT;
  lcd.setCursor(0, 1);
  if (showDHT) {
    if (dhtOk) {
      char hBuf[5], tBuf[5];
      dtostrf(humidity, 4, 1, hBuf);
      dtostrf(dhtTemp,  4, 1, tBuf);
      lcd.print("H:"); lcd.print(hBuf); lcd.print("% T:"); lcd.print(tBuf);
    } else {
      lcd.print("DHT: error");
    }
  } else {
    char buf[5];
    float temps[4] = {tempC0, tempC1, tempC2, tempC3};
    for (int i = 0; i < 4; i++) {
      if (i > 0) lcd.print(" ");
      if (temps[i] != DEVICE_DISCONNECTED_C) {
        dtostrf(temps[i], 3, 0, buf);
        lcd.print(buf);
      } else {
        lcd.print("---");
      }
    }
  }
}
