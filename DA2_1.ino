#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <ThingSpeak.h>
#include <ESP32QRCodeReader.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

/* ===== I2C ===== */
#define SDA_PIN 14
#define SCL_PIN 15

/* ===== BUTTON ===== */
#define BUTTON_PIN 13

/* ===== LCD ===== */
LiquidCrystal_I2C lcd(0x27, 16, 2);

/* ===== WIFI ===== */
const char *ssid = "m.wazg";
const char *password = "12346789";
WiFiClient client;

/* ===== MODULE ===== */
Adafruit_MLX90614 mlx;
ESP32QRCodeReader qrReader(CAMERA_MODEL_AI_THINKER);

/* ===== TIME CONTROL ===== */
bool allowScan = true;
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 20000;

/* ===== LCD HELPER ===== */
void lcdShow(String l1, String l2 = "")
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);
}

/* ===== READ TEMP SAFE ===== */
float readTempSafe()
{
  float t = mlx.readObjectTempC();
  if (isnan(t) || t < 0 || t > 100)
  {
    Wire.end();
    delay(50);
    Wire.begin(SDA_PIN, SCL_PIN);
    delay(50);
    t = mlx.readObjectTempC();
  }
  return t;
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  lcd.begin();
  lcd.backlight();
  lcdShow("ESP32-CAM", "Starting...");

  if (!mlx.begin())
  {
    lcdShow("MLX90614", "Not Found");
    while (1);
  }

  WiFi.begin(ssid, password);
  lcdShow("WiFi", "Connecting...");
  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  lcdShow("WiFi OK", WiFi.localIP().toString());
  ThingSpeak.begin(client);

  if (qrReader.setup() != SETUP_OK)
  {
    lcdShow("QR ERROR", "PSRAM Fail");
    while (1);
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s)
  {
    s->set_vflip(s, 1);
    s->set_contrast(s, 2);
    s->set_sharpness(s, 2);
  }

  qrReader.begin();
  lcdShow("Scan QR", "Waiting...");
}

void loop()
{
  struct QRCodeData qrData;

  if (!allowScan && millis() - lastScanTime >= SCAN_INTERVAL)
  {
    allowScan = true;
    lcdShow("Scan QR", "Waiting...");
  }

  if (allowScan && qrReader.receiveQrCode(&qrData, 100))
  {
    if (!qrData.valid) return;

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, (char *)qrData.payload))
    {
      lcdShow("QR Error", "Invalid JSON");
      return;
    }

    /* ===== PARSE QR ===== */
    const char *pID = doc["id"] | "N/A";
    const char *pName = doc["name"] | "N/A";
    unsigned long channelID = doc["channel"];
    const char *writeAPIKey = doc["key"];

    if (channelID == 0 || strlen(writeAPIKey) < 10)
    {
      lcdShow("QR Error", "No Channel");
      return;
    }

    lcdShow("QR OK", pName);
    delay(3000);

    lcdShow("Nhan nut", "Do nhiet");
    while (digitalRead(BUTTON_PIN) == HIGH)
      delay(10);
    delay(300);

    float temp = readTempSafe();
    if (isnan(temp))
    {
      lcdShow("Temp Error", "");
      return;
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(temp, 1);
    lcd.print(" C");
    lcd.setCursor(0, 1);
    lcd.print(pID);
    delay(8000);

    /* ===== SEND TO THINGSPEAK ===== */
    ThingSpeak.setField(1, temp);
    ThingSpeak.setField(2, pID);
    ThingSpeak.setField(3, pName);
    ThingSpeak.writeFields(channelID, writeAPIKey);

    qrReader.end();
    delay(300);
    qrReader.begin();

    allowScan = false;
    lastScanTime = millis();
    lcdShow("Uploaded", "Wait 20s");
  }
}
