#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <HTTPClient.h>
#include "base64.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "MAX30105.h"
#include "heartRate.h"

// ================= BLYNK =================
#define BLYNK_TEMPLATE_ID "TMPL3aYpKaUEv"
#define BLYNK_TEMPLATE_NAME "ESP32 HealthX"
#define BLYNK_AUTH_TOKEN "qtmJUXBIpWzPUDu8aZrka_uY9IVutM4X"

#include <BlynkSimpleEsp32.h>
char auth[] = BLYNK_AUTH_TOKEN;

// ================= WIFI =================
const char* ssid = "wifi";
const char* password = "pass";

const long gmtOffset_sec = 19800;
const int daylightOffset_sec = 0;

// ================= TWILIO =================
const char* accountSID = "AC0051fde8a13b0abad26070e8401b2e08";
const char* authToken = "37116736c59ffa9a8f873ae046146cb4";
const char* fromNumber = "number";
const char* toNumber   = "number";

// ================= OLED =================
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ================= SENSORS =================
#define ONE_WIRE_BUS 15
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
MAX30105 particleSensor;

// ================= BUTTON =================
#define BUTTON_PIN 18
#define DEBOUNCE_TIME 40
int screenMode = 0;

bool lastReading = HIGH;
bool stableState = HIGH;
unsigned long lastDebounceTime = 0;

// ================= EMERGENCY =================
unsigned long pressStartTime = 0;
bool longPressTriggered = false;
bool emergencyActive = false;
bool flashState = false;
unsigned long lastFlashTime = 0;

// ================= HEART =================
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int beatAvg = 0;
float spo2 = 0;

// ================= GRAPH =================
#define GRAPH_POINTS 120
int bpmGraph[GRAPH_POINTS];
int graphIndex = 0;

// ================= BLYNK TIMER =================
unsigned long lastBlynkSend = 0;

// ======================================================
void sendEmergencySMS()
{
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "https://api.twilio.com/2010-04-01/Accounts/" + String(accountSID) + "/Messages.json";
  http.begin(url);

  String authStr = String(accountSID) + ":" + String(authToken);
  String encodedAuth = base64::encode(authStr);

  http.addHeader("Authorization", "Basic " + encodedAuth);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String messageBody = "From=" + String(fromNumber) +
                       "&To=" + String(toNumber) +
                       "&Body=EMERGENCY ALERT! Patient needs immediate attention!";

  http.POST(messageBody);
  http.end();
}

// ======================================================
void connectWiFi()
{
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected");
    Blynk.begin(auth, ssid, password);

    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 15) {
      delay(1000);
      retry++;
    }
  }
}

// ======================================================
void setup() {

  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(21,22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  sensors.begin();
  particleSensor.begin(Wire);
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeIR(0x1F);

  for(int i=0;i<GRAPH_POINTS;i++){
    bpmGraph[i] = 75;
  }

  connectWiFi();
}

// ======================================================
void handleButton()
{
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading)
    lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > DEBOUNCE_TIME) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW && !emergencyActive) {
        screenMode++;
        if (screenMode > 5) screenMode = 0;
      }
    }
  }

  lastReading = reading;

  if (stableState == LOW) {

    if (pressStartTime == 0) {
      pressStartTime = millis();
    }

    if (!longPressTriggered && (millis() - pressStartTime > 3000)) {

      longPressTriggered = true;

      if (!emergencyActive) {
        emergencyActive = true;
        sendEmergencySMS();
      } 
      else {
        emergencyActive = false;
      }
    }
  }
  else {
    pressStartTime = 0;
    longPressTriggered = false;
  }
}

// ======================================================
void drawHeader(String title)
{
  display.fillRect(0, 0, 128, 16, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor((128 - title.length()*6)/2, 4);
  display.print(title);
  display.setTextColor(WHITE);
}

void drawEmergency()
{
  if (millis() - lastFlashTime > 500) {
    flashState = !flashState;
    lastFlashTime = millis();
  }

  if (flashState) {
    display.fillScreen(WHITE);
    display.setTextColor(BLACK);
  } else {
    display.fillScreen(BLACK);
    display.setTextColor(WHITE);
  }

  display.setTextSize(2);
  display.setCursor(15, 20);
  display.print("EMERGENCY");

  display.setTextSize(1);
  display.setCursor(20, 45);
  display.print("HELP REQUIRED!");
}

// ======================================================
// 🔥 UPDATED TEMPERATURE SCREEN ONLY
void drawTemp()
{
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  float tempF = sensors.toFahrenheit(tempC);

  drawHeader("BODY TEMP");

  display.setTextSize(1);   // Smaller font
  display.setCursor(20, 30);
  display.print("C: ");
  display.print(tempC,1);
  display.print(" C");

  display.setCursor(20, 45);
  display.print("F: ");
  display.print(tempF,1);
  display.print(" F");
}
// ======================================================

// ===== REST OF YOUR CODE EXACTLY SAME =====

void drawClock()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    drawHeader("TIME");
    display.setCursor(20,30);
    display.print("No NTP Sync");
    return;
  }

  char timeStr[10];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

  drawHeader("INDIAN TIME");
  display.setTextSize(2);
  display.setCursor(15, 30);
  display.print(timeStr);
}

void drawBPM()
{
  long irValue = particleSensor.getIR();

  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    float bpm = 60 / (delta / 1000.0);

    if (bpm > 20 && bpm < 200) {
      rates[rateSpot++] = (byte)bpm;
      rateSpot %= RATE_SIZE;

      beatAvg = 0;
      for (byte i = 0; i < RATE_SIZE; i++)
        beatAvg += rates[i];
      beatAvg /= RATE_SIZE;

      bpmGraph[graphIndex] = beatAvg;
      graphIndex++;
      if (graphIndex >= GRAPH_POINTS) graphIndex = 0;
    }
  }

  drawHeader("HEART RATE");
  display.setTextSize(2);
  display.setCursor(25, 30);
  display.print(String(beatAvg) + " BPM");
}

void drawSpO2()
{
  long ir = particleSensor.getIR();
  long red = particleSensor.getRed();

  if (ir > 10000 && red > 10000) {
    float ratio = (float)red / ir;
    spo2 = 110 - 25 * ratio;
    if (spo2 > 100) spo2 = 100;
    if (spo2 < 0) spo2 = 0;
  }

  drawHeader("SpO2 LEVEL");
  display.setTextSize(2);
  display.setCursor(35, 30);
  display.print(String(spo2,1) + " %");
}

void drawGraph()
{
  drawHeader("BPM GRAPH");

  int graphTop = 18;
  int graphBottom = 63;

  for (int i = 1; i < GRAPH_POINTS; i++) {
    if (bpmGraph[i-1] > 40 && bpmGraph[i] > 40) {

      int x1 = i - 1;
      int x2 = i;

      int y1 = graphBottom - map(bpmGraph[i-1], 50, 120, 0, 40);
      int y2 = graphBottom - map(bpmGraph[i], 50, 120, 0, 40);

      y1 = constrain(y1, graphTop, graphBottom);
      y2 = constrain(y2, graphTop, graphBottom);

      display.drawLine(x1, y1, x2, y2, WHITE);
    }
  }
}

void drawStatus()
{
  drawHeader("HEALTH STATUS");
  display.setTextSize(2);
  display.setCursor(10, 30);

  if (beatAvg < 50) display.print("LOW BPM");
  else if (beatAvg > 120) display.print("HIGH BPM");
  else if (spo2 < 90) display.print("LOW SpO2");
  else display.print("STABLE");
}

void loop()
{
  Blynk.run();
  handleButton();
  display.clearDisplay();

  if (emergencyActive) {
    drawEmergency();
  }
  else {
    switch(screenMode) {
      case 0: drawClock(); break;
      case 1: drawBPM(); break;
      case 2: drawTemp(); break;
      case 3: drawSpO2(); break;
      case 4: drawGraph(); break;
      case 5: drawStatus(); break;
    }
  }

  display.display();

  if (millis() - lastBlynkSend > 2000) {

    sensors.requestTemperatures();
    float tempF = sensors.toFahrenheit(sensors.getTempCByIndex(0));

    Blynk.virtualWrite(V0, beatAvg);
    Blynk.virtualWrite(V1, tempF);
    Blynk.virtualWrite(V2, spo2);

    lastBlynkSend = millis();
  }
}