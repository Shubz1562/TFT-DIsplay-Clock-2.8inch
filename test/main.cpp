#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>


[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

monitor_speed = 115200

lib_deps =
    bodmer/TFT_eSPI
    paulstoffregen/XPT2046_Touchscreen

build_flags =
    -D USER_SETUP_LOADED
    -D ILI9341_DRIVER

    -D TFT_MISO=19
    -D TFT_MOSI=23
    -D TFT_SCLK=18
    -D TFT_CS=15
    -D TFT_DC=2
    -D TFT_RST=4

    -D TOUCH_CS=5

    -D LOAD_GLCD
    -D LOAD_FONT2
    -D LOAD_FONT4
    -D LOAD_FONT6
    -D LOAD_FONT7
    -D LOAD_FONT8

    -D SPI_FREQUENCY=20000000
    -D SPI_READ_FREQUENCY=10000000
    -D SPI_TOUCH_FREQUENCY=2500000

// ---------------- WiFi ----------------
const char* WIFI_SSID = "Innovex";
const char* WIFI_PASS = "Innovex@12345";

// India time UTC + 5:30
const long GMT_OFFSET_SEC = 19800;
const int DAYLIGHT_OFFSET_SEC = 0;
const char* NTP_SERVER = "pool.ntp.org";

// ---------------- Pins ----------------
#ifndef TOUCH_CS
#define TOUCH_CS 5
#endif

#define TOUCH_IRQ 27
#define BUZZER_PIN 12
#define LED_PIN 13

// ---------------- Display / Touch ----------------
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// Your correct display angle
#define DISPLAY_ROTATION 2
#define TOUCH_ROTATION   1

// Landscape size for rotation 1
#define SCREEN_W 320
#define SCREEN_H 240

// ---------------- Touch calibration ----------------
#define TOUCH_MIN_X 250
#define TOUCH_MAX_X 3900
#define TOUCH_MIN_Y 250
#define TOUCH_MAX_Y 3900

// Change only if touch buttons are wrong
#define TOUCH_SWAP_XY  false
#define TOUCH_INVERT_X false
#define TOUCH_INVERT_Y false

// ---------------- Alarm data ----------------
int alarmHour = 6;
int alarmMinute = 30;

bool alarmEnabled = true;
bool alarmRinging = false;

int lastAlarmDay = -1;

bool buzzerState = false;
unsigned long lastBuzzerToggle = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastTouchTime = 0;

const int BUZZER_INTERVAL = 300;
const int TOUCH_DEBOUNCE = 250;
const int SNOOZE_MINUTES = 5;

// ---------------- Button structure ----------------
struct Button {
  int x;
  int y;
  int w;
  int h;
  const char* text;
};

Button btnHMinus = {10, 145, 70, 35, "H-"};
Button btnHPlus  = {90, 145, 70, 35, "H+"};
Button btnMMinus = {170, 145, 70, 35, "M-"};
Button btnMPlus  = {250, 145, 60, 35, "M+"};

Button btnOnOff  = {10, 195, 95, 35, "ON/OFF"};
Button btnStop   = {115, 195, 90, 35, "STOP"};
Button btnSnooze = {215, 195, 95, 35, "SNOOZE"};

// ---------------- Time ----------------
bool getTimeNow(struct tm &timeinfo) {
  return getLocalTime(&timeinfo, 50);
}

// ---------------- Draw text helpers ----------------
void drawText(int x, int y, const String &text, int size, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(size);
  tft.setCursor(x, y);
  tft.print(text);
}

void drawButton(Button b, uint16_t color) {
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, color);

  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(2);

  int textX = b.x + 10;
  int textY = b.y + 10;

  if (String(b.text).length() <= 2) {
    textX = b.x + 22;
  }

  tft.setCursor(textX, textY);
  tft.print(b.text);
}

// ---------------- Main screen ----------------
void drawScreen() {
  tft.fillScreen(TFT_BLACK);

  // Header like your correct test code
  drawText(80, 15, "SMART ALARM", 2, TFT_YELLOW);

  struct tm timeinfo;

  if (getTimeNow(timeinfo)) {
    int hour24 = timeinfo.tm_hour;
    int hour12 = hour24 % 12;

    if (hour12 == 0) {
      hour12 = 12;
    }

    bool pm = hour24 >= 12;

    char timeText[20];
    sprintf(
      timeText,
      "%02d:%02d:%02d %s",
      hour12,
      timeinfo.tm_min,
      timeinfo.tm_sec,
      pm ? "PM" : "AM"
    );

    drawText(45, 50, timeText, 3, TFT_CYAN);

    char dateText[25];
    strftime(dateText, sizeof(dateText), "%d %b %Y", &timeinfo);

    drawText(95, 90, dateText, 2, TFT_WHITE);
  } else {
    drawText(80, 55, "NO TIME", 3, TFT_CYAN);
    drawText(95, 95, "Check WiFi", 2, TFT_WHITE);
  }

  int alarmHour12 = alarmHour % 12;

  if (alarmHour12 == 0) {
    alarmHour12 = 12;
  }

  bool alarmPM = alarmHour >= 12;

  char alarmText[35];
  sprintf(
    alarmText,
    "Alarm %02d:%02d %s %s",
    alarmHour12,
    alarmMinute,
    alarmPM ? "PM" : "AM",
    alarmEnabled ? "ON" : "OFF"
  );

  drawText(45, 120, alarmText, 2, alarmEnabled ? TFT_GREEN : TFT_DARKGREY);

  if (alarmRinging) {
    drawText(85, 130, "ALARM RINGING", 2, TFT_RED);
  }

  drawButton(btnHMinus, TFT_YELLOW);
  drawButton(btnHPlus, TFT_YELLOW);
  drawButton(btnMMinus, TFT_YELLOW);
  drawButton(btnMPlus, TFT_YELLOW);

  drawButton(btnOnOff, TFT_CYAN);
  drawButton(btnStop, TFT_RED);
  drawButton(btnSnooze, TFT_GREEN);
}

// ---------------- Touch ----------------
bool getTouchXY(int &x, int &y) {
  if (!touch.touched()) {
    return false;
  }

  TS_Point p = touch.getPoint();

  int tx = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_W);
  int ty = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_H);

  if (TOUCH_SWAP_XY) {
    int temp = tx;
    tx = ty;
    ty = temp;
  }

  if (TOUCH_INVERT_X) {
    tx = SCREEN_W - tx;
  }

  if (TOUCH_INVERT_Y) {
    ty = SCREEN_H - ty;
  }

  x = constrain(tx, 0, SCREEN_W - 1);
  y = constrain(ty, 0, SCREEN_H - 1);

  Serial.print("Touch X: ");
  Serial.print(x);
  Serial.print(" Y: ");
  Serial.println(y);

  return true;
}

bool buttonPressed(int x, int y, Button b) {
  return x >= b.x && x <= b.x + b.w &&
         y >= b.y && y <= b.y + b.h;
}

// ---------------- Alarm functions ----------------
void stopAlarm() {
  alarmRinging = false;
  buzzerState = false;

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
}

void startAlarm() {
  alarmRinging = true;
  buzzerState = false;
  lastBuzzerToggle = 0;

  drawScreen();
}

void snoozeAlarm() {
  struct tm timeinfo;

  if (getTimeNow(timeinfo)) {
    int totalMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min + SNOOZE_MINUTES;
    totalMinutes %= 24 * 60;

    alarmHour = totalMinutes / 60;
    alarmMinute = totalMinutes % 60;

    alarmEnabled = true;
    lastAlarmDay = -1;
  }

  stopAlarm();
}

void handleTouch() {
  int x, y;

  if (!getTouchXY(x, y)) {
    return;
  }

  if (millis() - lastTouchTime < TOUCH_DEBOUNCE) {
    return;
  }

  lastTouchTime = millis();

  if (buttonPressed(x, y, btnHMinus)) {
    alarmHour--;

    if (alarmHour < 0) {
      alarmHour = 23;
    }

    alarmEnabled = true;
  }

  else if (buttonPressed(x, y, btnHPlus)) {
    alarmHour++;

    if (alarmHour > 23) {
      alarmHour = 0;
    }

    alarmEnabled = true;
  }

  else if (buttonPressed(x, y, btnMMinus)) {
    alarmMinute -= 5;

    if (alarmMinute < 0) {
      alarmMinute = 55;
      alarmHour--;

      if (alarmHour < 0) {
        alarmHour = 23;
      }
    }

    alarmEnabled = true;
  }

  else if (buttonPressed(x, y, btnMPlus)) {
    alarmMinute += 5;

    if (alarmMinute >= 60) {
      alarmMinute = 0;
      alarmHour++;

      if (alarmHour > 23) {
        alarmHour = 0;
      }
    }

    alarmEnabled = true;
  }

  else if (buttonPressed(x, y, btnOnOff)) {
    alarmEnabled = !alarmEnabled;

    if (!alarmEnabled) {
      stopAlarm();
    }
  }

  else if (buttonPressed(x, y, btnStop)) {
    stopAlarm();
  }

  else if (buttonPressed(x, y, btnSnooze)) {
    if (alarmRinging) {
      snoozeAlarm();
    }
  }

  drawScreen();
}

void checkAlarm() {
  if (!alarmEnabled || alarmRinging) {
    return;
  }

  struct tm timeinfo;

  if (!getTimeNow(timeinfo)) {
    return;
  }

  int today = timeinfo.tm_yday;

  if (
    timeinfo.tm_hour == alarmHour &&
    timeinfo.tm_min == alarmMinute &&
    today != lastAlarmDay
  ) {
    lastAlarmDay = today;
    startAlarm();
  }
}

void updateBuzzer() {
  if (!alarmRinging) {
    return;
  }

  if (millis() - lastBuzzerToggle >= BUZZER_INTERVAL) {
    lastBuzzerToggle = millis();

    buzzerState = !buzzerState;

    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    digitalWrite(LED_PIN, buzzerState ? HIGH : LOW);
  }
}

// ---------------- WiFi / NTP ----------------
void connectWiFiAndTime() {
  tft.fillScreen(TFT_BLACK);

  drawText(55, 60, "ESP32 ALARM CLOCK", 2, TFT_CYAN);
  drawText(70, 105, "Connecting WiFi", 2, TFT_WHITE);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long wifiStart = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  tft.fillScreen(TFT_BLACK);

  if (WiFi.status() == WL_CONNECTED) {
    drawText(75, 80, "WiFi Connected", 2, TFT_GREEN);

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    drawText(80, 120, "Syncing Time", 2, TFT_WHITE);

    struct tm timeinfo;
    int tries = 0;

    while (!getTimeNow(timeinfo) && tries < 20) {
      delay(500);
      tries++;
    }

    if (tries < 20) {
      drawText(90, 160, "Time Synced", 2, TFT_GREEN);
    } else {
      drawText(90, 160, "Time Failed", 2, TFT_RED);
    }
  } else {
    drawText(95, 95, "WiFi Failed", 2, TFT_RED);
    drawText(95, 135, "Check WiFi", 2, TFT_YELLOW);
  }

  delay(1500);
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  tft.init();

  // This is your confirmed correct screen angle
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);

  touch.begin();

  // Touch same angle
  touch.setRotation(TOUCH_ROTATION);

  connectWiFiAndTime();

  drawScreen();
}

// ---------------- Loop ----------------
void loop() {
  handleTouch();
  checkAlarm();
  updateBuzzer();

  if (millis() - lastScreenUpdate >= 1000) {
    lastScreenUpdate = millis();
    drawScreen();
  }
}