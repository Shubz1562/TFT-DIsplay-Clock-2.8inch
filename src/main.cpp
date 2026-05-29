#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

// ─── PIN CONFIG ───────────────────────────────────────────────
#define BUZZER_PIN     26
#define ALARM_LED_PIN  13
#define TOUCH_IRQ      27
#define SCREEN_ROTATION 0

// ─── TOUCH CALIBRATION ────────────────────────────────────────
#define RUN_TOUCH_CALIBRATION false
uint16_t touchCalData[5] = { 300, 3600, 300, 3600, 7 };

// ─── WiFi CONFIG ──────────────────────────────────────────────
#define APP_WIFI_MODE_AP    0
#define APP_WIFI_MODE_STA   1

const char* AP_SSID     = "Inovex";
const char* AP_PASSWORD = "Innovex@12345";

int  wifiMode           = APP_WIFI_MODE_AP;
int  activeWifiMode     = APP_WIFI_MODE_AP;
char staSsid[64]        = "";
char staPass[64]        = "";
bool wifiConnected      = false;
bool ntpSynced          = false;

// ─── NTP CONFIG ───────────────────────────────────────────────
const char* ntpServer = "pool.ntp.org";
long        gmtOffset = 19800;   // IST = UTC+5:30 = 19800s  (editable via web)
int         dstOffset = 0;

// ─── CLOCK / DATE STATE ───────────────────────────────────────
int clockHour   = 12;
int clockMinute = 0;
int clockSecond = 0;

int clockDay    = 1;
int clockMonth  = 1;
int clockYear   = 2026;
int clockDow    = 4;   // 0=Sun, 1=Mon, ... 6=Sat

// ─── ALARMS ───────────────────────────────────────────────────
#define MAX_ALARMS 5

struct Alarm {
  int  hour;
  int  minute;
  bool enabled;
  bool onMon, onTue, onWed, onThu, onFri, onSat, onSun;
  char label[24];
};

Alarm alarms[MAX_ALARMS] = {
  {7, 0, false, true, true, true, true, true, false, false, "Wake Up"},
  {9, 0, false, true, true, true, true, true, false, false, "Work Start"},
  {13,0, false, true, true, true, true, true, false, false, "Lunch"},
  {18,0, false, true, true, true, true, true, true,  true,  "Evening"},
  {22,0, false, true, true, true, true, true, true,  true,  "Sleep"},
};

int  ringAlarmIndex     = -1;
bool alarmRinging       = false;

// ─── UI STATE ─────────────────────────────────────────────────
bool use24HourMode  = false;
bool showStopwatch  = false;
bool showTimer      = false;

// Stopwatch
unsigned long swStartMillis = 0;
unsigned long swElapsed     = 0;
bool          swRunning     = false;

// Countdown timer
int  timerSeconds   = 0;    // configured total seconds
int  timerRemaining = 0;
bool timerRunning   = false;
unsigned long timerLastTick = 0;

// Theme
#define THEME_COUNT 8

uint16_t accentColor  = TFT_CYAN;
uint8_t  currentTheme = 0; // 0=Cyber 1=Amber 2=Neon 3=Pink 4=Royal 5=Purple 6=Sunset 7=Ice

uint16_t THEMES[THEME_COUNT][2] = {
  {0x07FF, 0xF800}, // 0 Cyber Cyan   / Red
  {0xFD20, 0xF800}, // 1 Amber Pulse  / Red
  {0x07E0, 0xF800}, // 2 Neon Green   / Red
  {0xF81F, 0xF800}, // 3 Pink Glow    / Red
  {0x001F, 0xFFE0}, // 4 Royal Blue   / Yellow
  {0x780F, 0xFFFF}, // 5 Purple Mist  / White
  {0xFC00, 0x07FF}, // 6 Sunset Orange/ Cyan
  {0xAFE5, 0xF800}  // 7 Ice Mint     / Red
};

unsigned long lastClockMillis  = 0;
unsigned long lastTouchMillis  = 0;
int           lastAlarmMinute  = -1;
int           lastAlarmDateStamp = -1;

enum ScreenMode { SCREEN_MAIN, SCREEN_ALARM_SET, SCREEN_STOPWATCH, SCREEN_WIFI, SCREEN_SETTINGS };
ScreenMode screenMode = SCREEN_MAIN;

// ─── BUTTON STRUCT ────────────────────────────────────────────
struct Button { int x, y, w, h; const char* label; };

// Main screen buttons
Button btnSet      = {230,  44, 82, 34, "ALARMS"};
Button btnSW       = {230,  88, 82, 34, "STOPWCH"};
Button btnSettings = {230, 132, 82, 34, "SETTNGS"};
Button btnWifi     = {230, 176, 82, 34, "Wi-Fi"};
Button btnStop     = {80,  202,160, 30, "STOP ALARM"};

// Alarm set
Button btnBack      = {220, 200, 95, 30, "< BACK"};
Button btnPrevAlm   = {10,  200, 55, 30, "< PRV"};
Button btnNextAlm   = {70,  200, 55, 30, "NXT >"};
Button btnAlmToggle = {145, 48,  165,30, "TOGGLE"};
Button btnAlmH_up   = {10,  100, 55, 35, "H+"};
Button btnAlmH_dn   = {10,  145, 55, 35, "H-"};
Button btnAlmM_up   = {80,  100, 55, 35, "M+"};
Button btnAlmM_dn   = {80,  145, 55, 35, "M-"};

// Stopwatch
Button btnSwStart  = {10,  150, 90, 35, "START"};
Button btnSwStop   = {10,  150, 90, 35, "STOP"};
Button btnSwReset  = {115, 150, 90, 35, "RESET"};
Button btnSwBack   = {220, 200, 95, 30, "< BACK"};

// Settings
Button btnTheme    = {10,  80,  145, 34, "THEME"};
Button btnMode24   = {10,  125, 145, 34, "12/24H"};
Button btnNTP      = {10,  170, 145, 34, "SYNC NTP"};
Button btnSetBack  = {220, 200, 95,  30, "< BACK"};

// WiFi screen
Button btnWifiBack = {220, 200, 95, 30, "< BACK"};

int currentAlarmIndex = 0;

// ─── HELPERS ──────────────────────────────────────────────────
String twoDigit(int v) { return v < 10 ? "0" + String(v) : String(v); }

const char* DOW_NAMES[7]    = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* MONTH_NAMES[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

int monthFromName(const char* mon) {
  for (int i = 0; i < 12; i++) {
    if (strncmp(mon, MONTH_NAMES[i], 3) == 0) return i + 1;
  }
  return 1;
}

bool isLeapYear(int y) {
  return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
}

int daysInMonth(int m, int y) {
  if (m == 2) return isLeapYear(y) ? 29 : 28;
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  return 31;
}

void updateDowFromDate() {
  struct tm ti = {};
  ti.tm_year = clockYear - 1900;
  ti.tm_mon  = clockMonth - 1;
  ti.tm_mday = clockDay;
  mktime(&ti);
  clockDow = ti.tm_wday; // 0=Sun
}

void incrementDate() {
  clockDay++;
  if (clockDay > daysInMonth(clockMonth, clockYear)) {
    clockDay = 1;
    clockMonth++;
    if (clockMonth > 12) {
      clockMonth = 1;
      clockYear++;
    }
  }
  updateDowFromDate();
}

int dateStamp() {
  return clockYear * 10000 + clockMonth * 100 + clockDay;
}

String getDateText() {
  int dow = constrain(clockDow, 0, 6);
  int mon = constrain(clockMonth, 1, 12);
  return String(DOW_NAMES[dow]) + ", " + twoDigit(clockDay) + " " + String(MONTH_NAMES[mon - 1]) + " " + String(clockYear);
}

String getIsoDateText() {
  return String(clockYear) + "-" + twoDigit(clockMonth) + "-" + twoDigit(clockDay);
}

String getTimeText() {
  int dh = clockHour;
  String suf = "";
  if (!use24HourMode) {
    suf = clockHour >= 12 ? " PM" : " AM";
    dh  = clockHour % 12;
    if (dh == 0) dh = 12;
  }
  return twoDigit(dh) + ":" + twoDigit(clockMinute) + ":" + twoDigit(clockSecond) + suf;
}

String getAlarmText(int idx) {
  int dh = alarms[idx].hour;
  String suf = "";
  if (!use24HourMode) {
    suf = alarms[idx].hour >= 12 ? "PM" : "AM";
    dh  = alarms[idx].hour % 12;
    if (dh == 0) dh = 12;
  }
  return twoDigit(dh) + ":" + twoDigit(alarms[idx].minute) + suf;
}

String getSwText() {
  unsigned long ms = swRunning ? (swElapsed + millis() - swStartMillis) : swElapsed;
  unsigned long s  = ms / 1000;
  unsigned long m  = s / 60; s %= 60;
  unsigned long h  = m / 60; m %= 60;
  return twoDigit(h) + ":" + twoDigit(m) + ":" + twoDigit(s) + "." + twoDigit((ms % 1000) / 10);
}

bool isInside(Button b, uint16_t x, uint16_t y) {
  return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

uint16_t accentCol() { return THEMES[currentTheme][0]; }

String currentWifiIp() {
  if (activeWifiMode == APP_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  if (activeWifiMode == APP_WIFI_MODE_AP) {
    return WiFi.softAPIP().toString();
  }
  return "0.0.0.0";
}

String activeWifiModeText() {
  if (activeWifiMode == APP_WIFI_MODE_STA) return "Station";
  if (wifiMode == APP_WIFI_MODE_STA) return "Access Point (STA failed)";
  return "Access Point";
}

String activeWifiShortText() {
  return activeWifiMode == APP_WIFI_MODE_STA ? "STA" : "AP";
}

void drawButton(Button b, uint16_t border, uint16_t txt, uint16_t fill = TFT_BLACK) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 5, border);
  tft.setTextColor(txt, fill);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(b.label, b.x + b.w/2, b.y + b.h/2, 2);
  tft.setTextDatum(TL_DATUM);
}

// ─── SAVE / LOAD PREFS ────────────────────────────────────────
void savePrefs() {
  prefs.begin("clk", false);
  prefs.putInt("theme", currentTheme);
  prefs.putBool("24h", use24HourMode);
  prefs.putLong("gmt", gmtOffset);
  prefs.putString("staSsid", staSsid);
  prefs.putString("staPass", staPass);
  prefs.putInt("wifiMode", wifiMode);
  for (int i = 0; i < MAX_ALARMS; i++) {
    String k = "a" + String(i);
    prefs.putInt((k+"h").c_str(),   alarms[i].hour);
    prefs.putInt((k+"m").c_str(),   alarms[i].minute);
    prefs.putBool((k+"e").c_str(),  alarms[i].enabled);
    prefs.putString((k+"l").c_str(),alarms[i].label);
    uint8_t days = (alarms[i].onMon<<0)|(alarms[i].onTue<<1)|(alarms[i].onWed<<2)|
                   (alarms[i].onThu<<3)|(alarms[i].onFri<<4)|(alarms[i].onSat<<5)|(alarms[i].onSun<<6);
    prefs.putUChar((k+"d").c_str(), days);
  }
  prefs.end();
}

void loadPrefs() {
  prefs.begin("clk", true);
  currentTheme = constrain(prefs.getInt("theme", 0), 0, THEME_COUNT - 1);
  use24HourMode= prefs.getBool("24h", false);
  gmtOffset    = prefs.getLong("gmt", 19800);
  wifiMode     = prefs.getInt("wifiMode", APP_WIFI_MODE_AP);
  String ss    = prefs.getString("staSsid","");
  String sp    = prefs.getString("staPass","");
  ss.toCharArray(staSsid,64);
  sp.toCharArray(staPass,64);
  for (int i = 0; i < MAX_ALARMS; i++) {
    String k = "a" + String(i);
    alarms[i].hour    = prefs.getInt((k+"h").c_str(), alarms[i].hour);
    alarms[i].minute  = prefs.getInt((k+"m").c_str(), alarms[i].minute);
    alarms[i].enabled = prefs.getBool((k+"e").c_str(), false);
    String lbl        = prefs.getString((k+"l").c_str(), alarms[i].label);
    lbl.toCharArray(alarms[i].label, 24);
    uint8_t days = prefs.getUChar((k+"d").c_str(), 0x1F);
    alarms[i].onMon=(days>>0)&1; alarms[i].onTue=(days>>1)&1;
    alarms[i].onWed=(days>>2)&1; alarms[i].onThu=(days>>3)&1;
    alarms[i].onFri=(days>>4)&1; alarms[i].onSat=(days>>5)&1;
    alarms[i].onSun=(days>>6)&1;
  }
  prefs.end();
}

// ─── NTP SYNC ─────────────────────────────────────────────────
void syncNTP() {
  if (!wifiConnected) return;
  configTime(gmtOffset, dstOffset, ntpServer);
  struct tm ti;
  if (getLocalTime(&ti, 5000)) {
    clockHour   = ti.tm_hour;
    clockMinute = ti.tm_min;
    clockSecond = ti.tm_sec;
    clockDay    = ti.tm_mday;
    clockMonth  = ti.tm_mon + 1;
    clockYear   = ti.tm_year + 1900;
    clockDow    = ti.tm_wday;
    ntpSynced   = true;
    lastClockMillis = millis();
  }
}

// ─── WIFI INIT ────────────────────────────────────────────────
void initWifi() {
  wifiConnected = false;
  activeWifiMode = APP_WIFI_MODE_AP;

  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  if (wifiMode == APP_WIFI_MODE_STA && strlen(staSsid) > 0) {
    WiFi.mode(WIFI_STA);
    delay(100);
    if (strlen(staPass) > 0) WiFi.begin(staSsid, staPass);
    else WiFi.begin(staSsid);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      activeWifiMode = APP_WIFI_MODE_STA;
      syncNTP();
    } else {
      // fallback to AP
      WiFi.mode(WIFI_AP);
      delay(100);
      wifiConnected = WiFi.softAP(AP_SSID, AP_PASSWORD);
      activeWifiMode = APP_WIFI_MODE_AP;
    }
  } else {
    WiFi.mode(WIFI_AP);
    delay(100);
    wifiConnected = WiFi.softAP(AP_SSID, AP_PASSWORD);
    activeWifiMode = APP_WIFI_MODE_AP;
  }
}

// ─── WEB DASHBOARD HTML ───────────────────────────────────────
// Stored in PROGMEM to save RAM
const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0">
<title>Clock</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Space+Mono:wght@400;700&family=Syne:wght@400;700;800&display=swap');
*{margin:0;padding:0;box-sizing:border-box}
:root{
  --bg:#0a0a12;--surface:#111122;--surface2:#181830;
  --accent:#00f5d4;--accent2:#7b2ff7;--red:#ff3860;
  --text:#e8e8ff;--sub:#888899;--border:rgba(255,255,255,0.08);
  --radius:16px;--glow:0 0 24px rgba(0,245,212,0.25);
}
html,body{min-height:100%;background:var(--bg);color:var(--text);font-family:'Syne',sans-serif;overflow-x:hidden}
body{padding:16px;background-image:radial-gradient(ellipse 60% 40% at 50% -10%,rgba(123,47,247,0.3),transparent),
  radial-gradient(ellipse 40% 30% at 80% 110%,rgba(0,245,212,0.15),transparent)}
h1{font-size:1.5rem;font-weight:800;letter-spacing:-0.5px;background:linear-gradient(135deg,var(--accent),var(--accent2));
   -webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.logo{display:flex;align-items:center;gap:10px;margin-bottom:24px}
.logo .dot{width:10px;height:10px;background:var(--accent);border-radius:50%;box-shadow:var(--glow)}
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:20px;margin-bottom:16px;
      backdrop-filter:blur(12px)}
.card h2{font-size:0.7rem;font-weight:700;letter-spacing:2px;text-transform:uppercase;color:var(--sub);margin-bottom:16px}
.clock-display{font-family:'Space Mono',monospace;font-size:3rem;font-weight:700;text-align:center;
  background:linear-gradient(135deg,var(--accent),#fff);-webkit-background-clip:text;
  -webkit-text-fill-color:transparent;background-clip:text;line-height:1;margin:8px 0;
  text-shadow:none;filter:drop-shadow(0 0 12px rgba(0,245,212,0.5))}
.date-display{text-align:center;color:var(--sub);font-size:0.85rem;margin-bottom:4px}
.status-row{display:flex;justify-content:center;gap:12px;margin-top:8px}
.badge{display:inline-flex;align-items:center;gap:5px;padding:4px 10px;border-radius:99px;font-size:0.7rem;
  font-weight:700;letter-spacing:1px;border:1px solid}
.badge.on{color:var(--accent);border-color:rgba(0,245,212,0.4);background:rgba(0,245,212,0.1)}
.badge.off{color:var(--sub);border-color:var(--border)}
.badge.wifi{color:#7b2ff7;border-color:rgba(123,47,247,0.4);background:rgba(123,47,247,0.1)}
.dot-blink{width:6px;height:6px;border-radius:50%;background:currentColor;animation:blink 1s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0.2}}
label{display:block;font-size:0.7rem;letter-spacing:1.5px;text-transform:uppercase;color:var(--sub);margin-bottom:6px}
input[type=text],input[type=number],input[type=password],input[type=time],select{
  width:100%;padding:12px 14px;border-radius:10px;border:1px solid var(--border);
  background:var(--surface2);color:var(--text);font-family:'Space Mono',monospace;font-size:0.95rem;outline:none;
  transition:border 0.2s}
input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(0,245,212,0.1)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.field{margin-bottom:14px}
.btn{width:100%;padding:14px;border-radius:10px;border:none;font-family:'Syne',sans-serif;font-size:0.9rem;
  font-weight:700;cursor:pointer;letter-spacing:0.5px;transition:all 0.15s;position:relative;overflow:hidden}
.btn:active{transform:scale(0.97)}
.btn-primary{background:linear-gradient(135deg,var(--accent),rgba(0,245,212,0.7));color:#0a0a12}
.btn-primary:hover{box-shadow:0 0 20px rgba(0,245,212,0.4)}
.btn-secondary{background:var(--surface2);color:var(--text);border:1px solid var(--border)}
.btn-danger{background:linear-gradient(135deg,var(--red),rgba(255,56,96,0.7));color:#fff}
.btn-purple{background:linear-gradient(135deg,var(--accent2),rgba(123,47,247,0.7));color:#fff}
.btn-sm{padding:8px 12px;font-size:0.78rem;border-radius:8px;width:auto}
.tabs{display:flex;gap:4px;margin-bottom:16px;background:var(--surface2);padding:4px;border-radius:12px}
.tab{flex:1;padding:9px;text-align:center;border-radius:9px;font-size:0.75rem;font-weight:700;
  letter-spacing:1px;cursor:pointer;transition:all 0.2s;color:var(--sub)}
.tab.active{background:var(--accent);color:#0a0a12}
.tab-content{display:none}.tab-content.active{display:block}
.alarm-card{border:1px solid var(--border);border-radius:12px;padding:14px;margin-bottom:10px;
  background:var(--surface2);transition:border 0.2s}
.alarm-card.enabled{border-color:rgba(0,245,212,0.3)}
.alarm-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}
.alarm-time{font-family:'Space Mono',monospace;font-size:1.5rem;font-weight:700;color:var(--accent)}
.alarm-label{font-size:0.8rem;color:var(--sub);margin-top:2px}
.toggle{position:relative;width:46px;height:26px;flex-shrink:0}
.toggle input{opacity:0;width:0;height:0}
.slider{position:absolute;inset:0;background:#333;border-radius:13px;cursor:pointer;transition:0.3s}
.slider:before{content:'';position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:0.3s}
input:checked+.slider{background:var(--accent)}
input:checked+.slider:before{transform:translateX(20px)}
.days{display:flex;gap:4px;flex-wrap:wrap;margin-top:8px}
.day{padding:4px 7px;border-radius:6px;font-size:0.65rem;font-weight:700;letter-spacing:0.5px;cursor:pointer;
  border:1px solid var(--border);color:var(--sub);transition:all 0.2s;user-select:none}
.day.active{background:var(--accent);color:#0a0a12;border-color:var(--accent)}
.day.weekend{color:#ff9f43}
.switch-row{display:flex;align-items:center;justify-content:space-between;padding:10px 0;
  border-bottom:1px solid var(--border)}
.switch-row:last-child{border:none}
.switch-label{font-size:0.9rem}
.switch-sub{font-size:0.75rem;color:var(--sub);margin-top:2px}
.wifi-status{padding:14px;border-radius:12px;background:var(--surface2);margin-bottom:14px;border:1px solid var(--border)}
.wifi-ip{font-family:'Space Mono',monospace;font-size:1.1rem;color:var(--accent);margin-top:4px}
.notify{padding:12px 16px;border-radius:10px;margin-top:12px;font-size:0.85rem;display:none;
  background:rgba(0,245,212,0.1);border:1px solid rgba(0,245,212,0.3);color:var(--accent)}
.notify.err{background:rgba(255,56,96,0.1);border-color:rgba(255,56,96,0.3);color:var(--red)}
.section-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.theme-picker{display:flex;gap:8px;flex-wrap:wrap}
.theme-dot{width:32px;height:32px;border-radius:50%;cursor:pointer;border:3px solid transparent;transition:all 0.2s}
.theme-dot.selected{border-color:#fff;transform:scale(1.1)}
</style>
</head>
<body>
<div class="logo"><div class="dot"></div><h1>Clock</h1></div>

<!-- CLOCK CARD -->
<div class="card" id="clockCard">
  <h2>&#9654; Live Time</h2>
  <div class="date-display" id="dateDisp">Loading...</div>
  <div class="clock-display" id="clockDisp">--:--:--</div>
  <div class="status-row">
    <span class="badge wifi"><span class="dot-blink"></span><span id="wifiLabel">Wi-Fi</span></span>
    <span class="badge" id="ntpBadge">NTP</span>
    <span class="badge" id="alarmBadge">ALARM</span>
  </div>
</div>

<!-- TABS -->
<div class="tabs">
  <div class="tab active" onclick="switchTab('alarms')">Alarms</div>
  <div class="tab" onclick="switchTab('time')">Time</div>
  <div class="tab" onclick="switchTab('wifi')">Wi-Fi</div>
  <div class="tab" onclick="switchTab('settings')">Settings</div>
</div>

<!-- ALARMS TAB -->
<div class="tab-content active" id="tab-alarms">
  <div id="alarmList"></div>
  <button class="btn btn-primary" onclick="saveAlarms()">&#10003; Save All Alarms</button>
  <div class="notify" id="notifyAlarm"></div>
</div>

<!-- TIME TAB -->
<div class="tab-content" id="tab-time">
  <div class="card">
    <h2>&#9654; Set Clock Date & Time</h2>
    <div class="field"><label>DATE</label>
      <input type="date" id="setDate">
    </div>
    <div class="field"><label>TIME (HH:MM:SS)</label>
      <div class="row3">
        <input type="number" id="setH" min="0" max="23" placeholder="HH">
        <input type="number" id="setM" min="0" max="59" placeholder="MM">
        <input type="number" id="setS" min="0" max="59" placeholder="SS">
      </div>
    </div>
    <button class="btn btn-primary" style="margin-bottom:10px" onclick="setTime()">Set Date & Time</button>
    <button class="btn btn-purple" onclick="ntpSync()">&#8635; Sync NTP Now</button>
    <div class="field" style="margin-top:14px"><label>GMT Offset (seconds)</label>
      <input type="number" id="gmtOffset" placeholder="19800 = IST (UTC+5:30)">
    </div>
    <button class="btn btn-secondary" onclick="saveGmt()">Save Timezone</button>
    <div class="notify" id="notifyTime"></div>
  </div>
</div>

<!-- WIFI TAB -->
<div class="tab-content" id="tab-wifi">
  <div class="card">
    <h2>&#9654; Connection</h2>
    <div class="wifi-status">
      <div style="font-size:0.7rem;color:var(--sub);letter-spacing:1px;text-transform:uppercase">IP Address</div>
      <div class="wifi-ip" id="ipAddr">--</div>
      <div style="font-size:0.75rem;color:var(--sub);margin-top:6px" id="wifiMode">Mode: --</div>
    </div>
    <div class="field"><label>Mode</label>
      <select id="wMode">
        <option value="0">Access Point (AP) — Direct connect</option>
        <option value="1">Station (STA) — Join your router</option>
      </select>
    </div>
    <div id="staFields">
      <div class="field"><label>Network SSID</label><input type="text" id="staSSID" placeholder="Your WiFi name"></div>
      <div class="field"><label>Password</label><input type="password" id="staPass" placeholder="WiFi password"></div>
    </div>
    <button class="btn btn-primary" onclick="saveWifi()">&#128267; Save & Reconnect</button>
    <div class="notify" id="notifyWifi"></div>
  </div>
</div>

<!-- SETTINGS TAB -->
<div class="tab-content" id="tab-settings">
  <div class="card">
    <h2>&#9654; Display</h2>
    <div class="switch-row">
      <div><div class="switch-label">24-Hour Mode</div><div class="switch-sub">Toggle 12h / 24h display</div></div>
      <label class="toggle"><input type="checkbox" id="h24mode" onchange="saveSettings()"><span class="slider"></span></label>
    </div>
    <div class="switch-row" style="border:none;padding-bottom:0">
      <div><div class="switch-label">Color Theme</div><div class="switch-sub">Accent color on device</div></div>
    </div>
    <div class="theme-picker" style="margin-top:10px">
      <div class="theme-dot" title="Cyber Cyan" style="background:#00f5d4" onclick="setTheme(0)" id="th0"></div>
      <div class="theme-dot" title="Amber Pulse" style="background:#fda718" onclick="setTheme(1)" id="th1"></div>
      <div class="theme-dot" title="Neon Green" style="background:#06d6a0" onclick="setTheme(2)" id="th2"></div>
      <div class="theme-dot" title="Pink Glow" style="background:#f72585" onclick="setTheme(3)" id="th3"></div>
      <div class="theme-dot" title="Royal Blue" style="background:#1e40ff" onclick="setTheme(4)" id="th4"></div>
      <div class="theme-dot" title="Purple Mist" style="background:#7b2ff7" onclick="setTheme(5)" id="th5"></div>
      <div class="theme-dot" title="Sunset Orange" style="background:#ff7a00" onclick="setTheme(6)" id="th6"></div>
      <div class="theme-dot" title="Ice Mint" style="background:#a7ffe4" onclick="setTheme(7)" id="th7"></div>
    </div>
    <div class="notify" id="notifySettings"></div>
  </div>
</div>

<script>
// ── State ──────────────────────────────────────────────────────────
let alarmData = [];
let state = {};
let activeTab = 'alarms';
let alarmDirty = false;
let lastAlarmJson = '';

function isFormEditing() {
  const el = document.activeElement;
  return el && ['INPUT','SELECT','TEXTAREA'].includes(el.tagName);
}

function clampInt(v, min, max) {
  v = parseInt(v, 10);
  if (isNaN(v)) v = min;
  return Math.max(min, Math.min(max, v));
}

// ── Fetch state ────────────────────────────────────────────────────
async function fetchState(force=false) {
  try {
    const r = await fetch('/api/state', {cache:'no-store'});
    state = await r.json();
    updateUI(force);
  } catch(e) {}
}

function updateUI(force=false) {
  const editing = isFormEditing();
  // Clock
  const t = state.time || {};
  document.getElementById('clockDisp').textContent = 
    pad(t.h)+':'+pad(t.m)+':'+pad(t.s);

  // Pre-fill manual date/time fields only when the user is not editing.
  if (!editing || force) {
    [['setH', t.h], ['setM', t.m], ['setS', t.s]].forEach(([id, value]) => {
      const el = document.getElementById(id);
      if (el && (el.value === '' || force) && value !== undefined) el.value = value;
    });
  }

  const d = state.date || {};
  document.getElementById('dateDisp').textContent = formatDate(d);
  const dateInput = document.getElementById('setDate');
  if (dateInput && d.year && (!editing || force) && (!dateInput.value || force)) {
    dateInput.value = d.year + '-' + pad(d.month) + '-' + pad(d.day);
  }

  // Badges
  const ntpB = document.getElementById('ntpBadge');
  ntpB.className = 'badge ' + (state.ntpSynced ? 'on' : 'off');
  ntpB.innerHTML = (state.ntpSynced ? '<span class="dot-blink"></span>' : '') + 'NTP';

  const almB = document.getElementById('alarmBadge');
  const anyAlm = state.alarms && state.alarms.some(a => a.enabled);
  almB.className = 'badge ' + (anyAlm ? 'on' : 'off');
  almB.textContent = anyAlm ? 'ALARM ON' : 'ALARM OFF';

  document.getElementById('wifiLabel').textContent = state.activeWifiLabel || (state.activeWifiMode == 1 ? 'STA' : 'AP');
  document.getElementById('ipAddr').textContent = state.ip || '--';
  document.getElementById('wifiMode').textContent = 'Mode: ' + (state.activeWifiModeText || (state.activeWifiMode == 1 ? 'Station' : 'Access Point'));

  if (!editing || force) {
    document.getElementById('h24mode').checked = state.use24h;
    document.getElementById('gmtOffset').value = state.gmtOffset;

    // Wi-Fi form
    document.getElementById('wMode').value = state.wifiMode;
    toggleStaFields();
  }

  // Theme
  for(let i=0;i<8;i++) document.getElementById('th'+i).classList.toggle('selected', state.theme==i);

  // Alarms: do not rebuild while the user has unsaved edits or is typing.
  if (state.alarms) {
    const incomingAlarmJson = JSON.stringify(state.alarms);
    if (force || alarmData.length === 0 || (!alarmDirty && !editing && incomingAlarmJson !== lastAlarmJson)) {
      alarmData = JSON.parse(incomingAlarmJson);
      lastAlarmJson = incomingAlarmJson;
      renderAlarms();
    }
  }
}

// ── Clock ticks ────────────────────────────────────────────────────
function tickClock() {
  const el = document.getElementById('clockDisp');
  const parts = el.textContent.split(':');
  if (parts.length<3) return;
  let h=parseInt(parts[0]),m=parseInt(parts[1]),s=parseInt(parts[2]);
  s++; if(s>=60){s=0;m++;} if(m>=60){m=0;h++;} if(h>=24) h=0;
  el.textContent = pad(h)+':'+pad(m)+':'+pad(s);
}
function pad(n){return String(n).padStart(2,'0');}
function formatDate(d) {
  if (!d || !d.year) return '--';
  const days = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
  const months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
  return days[d.dow || 0] + ', ' + pad(d.day) + ' ' + months[(d.month || 1)-1] + ' ' + d.year;
}
setInterval(tickClock, 1000);
setInterval(fetchState, 5000);
fetchState(true);

// ── Tabs ───────────────────────────────────────────────────────────
function switchTab(name) {
  activeTab = name;
  document.querySelectorAll('.tab').forEach((t,i)=>t.classList.toggle('active',['alarms','time','wifi','settings'][i]===name));
  document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));
  document.getElementById('tab-'+name).classList.add('active');
}

// ── Alarm Rendering ────────────────────────────────────────────────
const DAYS = ['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];
const DAY_KEYS = ['onMon','onTue','onWed','onThu','onFri','onSat','onSun'];

function renderAlarms() {
  const html = alarmData.map((a,i) => `
    <div class="alarm-card ${a.enabled?'enabled':''}" id="ac${i}">
      <div class="alarm-top">
        <div>
          <div class="alarm-time">${fmtAlarm(a)}</div>
          <div class="alarm-label">${a.label}</div>
        </div>
        <label class="toggle">
          <input type="checkbox" ${a.enabled?'checked':''} onchange="toggleAlarm(${i},this.checked)">
          <span class="slider"></span>
        </label>
      </div>
      <div class="row" style="margin-bottom:8px">
        <div>
          <label>Hour</label>
          <input type="number" min="0" max="23" value="${a.hour}" oninput="alarmData[${i}].hour=+this.value;alarmDirty=true" onchange="alarmData[${i}].hour=clampInt(this.value,0,23);this.value=alarmData[${i}].hour;redrawAlarmCard(${i})">
        </div>
        <div>
          <label>Minute</label>
          <input type="number" min="0" max="59" value="${a.minute}" oninput="alarmData[${i}].minute=+this.value;alarmDirty=true" onchange="alarmData[${i}].minute=clampInt(this.value,0,59);this.value=alarmData[${i}].minute;redrawAlarmCard(${i})">
        </div>
      </div>
      <div><label>Label</label>
        <input type="text" maxlength="23" value="${a.label}" oninput="alarmData[${i}].label=this.value;alarmDirty=true">
      </div>
      <div class="days" style="margin-top:10px">
        ${DAYS.map((d,di)=>`<span class="day${DAY_KEYS[di]?a[DAY_KEYS[di]]?' active':'':''} ${di>=5?'weekend':''}" onclick="toggleDay(${i},${di})">${d}</span>`).join('')}
      </div>
    </div>`).join('');
  document.getElementById('alarmList').innerHTML = html;
}

function fmtAlarm(a) {
  let h=a.hour,s='';
  if(!state.use24h){s=h>=12?' PM':' AM';h=h%12||12;}
  return pad(h)+':'+pad(a.minute)+s;
}

function toggleAlarm(i, v) {
  alarmData[i].enabled = v;
  alarmDirty = true;
  document.getElementById('ac'+i).classList.toggle('enabled',v);
}

function toggleDay(i, di) {
  const key = DAY_KEYS[di];
  alarmData[i][key] = !alarmData[i][key];
  alarmDirty = true;
  renderAlarms();
}

function redrawAlarmCard(i) { /* minor — full re-render on save */ }

async function saveAlarms() {
  try {
    await fetch('/api/alarms', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(alarmData)});
    alarmDirty = false;
    lastAlarmJson = JSON.stringify(alarmData);
    notify('notifyAlarm', '✓ Alarms saved!');
    fetchState(true);
  } catch(e){ notify('notifyAlarm','Error saving', true); }
}

// ── Time ───────────────────────────────────────────────────────────
async function setTime() {
  const current = state.time || {};
  const readNum = (id, fallback) => {
    const v = document.getElementById(id).value;
    return v === '' ? fallback : Number(v);
  };
  const h = readNum('setH', current.h || 0),
        m = readNum('setM', current.m || 0),
        s = readNum('setS', current.s || 0);
  const payload = {h,m,s};
  const dateValue = document.getElementById('setDate').value;
  if (dateValue) {
    const [y,mo,d] = dateValue.split('-').map(Number);
    payload.y = y; payload.mo = mo; payload.d = d;
  }
  await fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
  notify('notifyTime','✓ Date & time updated!');
  fetchState(true);
}

async function ntpSync() {
  await fetch('/api/ntp',{method:'POST'});
  notify('notifyTime','✓ NTP sync requested');
  fetchState(true);
}

async function saveGmt() {
  const gmt=+document.getElementById('gmtOffset').value;
  await fetch('/api/gmt',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({gmt})});
  notify('notifyTime','✓ Timezone saved');
  fetchState(true);
}

// ── Wi-Fi ──────────────────────────────────────────────────────────
function toggleStaFields() {
  document.getElementById('staFields').style.display = document.getElementById('wMode').value=='1'?'block':'none';
}
document.getElementById('wMode').onchange = toggleStaFields;

async function saveWifi() {
  const mode=+document.getElementById('wMode').value,
        ssid=document.getElementById('staSSID').value,
        pass=document.getElementById('staPass').value;
  await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode,ssid,pass})});
  notify('notifyWifi','✓ Saved — reconnecting…');
}

// ── Settings ───────────────────────────────────────────────────────
async function saveSettings() {
  const h24=document.getElementById('h24mode').checked;
  await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({use24h:h24})});
  notify('notifySettings','✓ Settings saved');
  state.use24h = h24;
  renderAlarms();
}

async function setTheme(t) {
  await fetch('/api/theme',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({theme:t})});
  for(let i=0;i<8;i++) document.getElementById('th'+i).classList.toggle('selected',i==t);
  state.theme=t;
}

// ── Notify ─────────────────────────────────────────────────────────
function notify(id, msg, err=false) {
  const el=document.getElementById(id);
  el.textContent=msg; el.style.display='block';
  el.classList.toggle('err',err);
  setTimeout(()=>el.style.display='none',3000);
}
</script>
</body>
</html>
)rawhtml";

// ─── WEB SERVER ROUTES ────────────────────────────────────────
void sendJson(String json) {
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.send(200,"application/json", json);
}

String alarmsJson() {
  String s = "[";
  for (int i = 0; i < MAX_ALARMS; i++) {
    if (i) s += ",";
    s += "{\"hour\":" + String(alarms[i].hour);
    s += ",\"minute\":" + String(alarms[i].minute);
    s += ",\"enabled\":" + String(alarms[i].enabled ? "true":"false");
    s += ",\"label\":\"" + String(alarms[i].label) + "\"";
    s += ",\"onMon\":"  + String(alarms[i].onMon  ? "true":"false");
    s += ",\"onTue\":"  + String(alarms[i].onTue  ? "true":"false");
    s += ",\"onWed\":"  + String(alarms[i].onWed  ? "true":"false");
    s += ",\"onThu\":"  + String(alarms[i].onThu  ? "true":"false");
    s += ",\"onFri\":"  + String(alarms[i].onFri  ? "true":"false");
    s += ",\"onSat\":"  + String(alarms[i].onSat  ? "true":"false");
    s += ",\"onSun\":"  + String(alarms[i].onSun  ? "true":"false");
    s += "}";
  }
  return s + "]";
}

void setupRoutes() {
  // Dashboard
  server.on("/", HTTP_GET, [](){
    server.send_P(200, "text/html", DASHBOARD_HTML);
  });

  // State JSON
  server.on("/api/state", HTTP_GET, [](){
    String ip = currentWifiIp();
    bool anyAlm = false;
    for (auto& a : alarms) if (a.enabled) { anyAlm=true; break; }
    String j = "{";
    j += "\"time\":{\"h\":" + String(clockHour) + ",\"m\":" + String(clockMinute) + ",\"s\":" + String(clockSecond) + "}";
    j += ",\"date\":{\"day\":" + String(clockDay) + ",\"month\":" + String(clockMonth) + ",\"year\":" + String(clockYear) + ",\"dow\":" + String(clockDow) + "}";
    j += ",\"dateText\":\"" + getDateText() + "\"";
    j += ",\"isoDate\":\"" + getIsoDateText() + "\"";
    j += ",\"ntpSynced\":" + String(ntpSynced?"true":"false");
    j += ",\"use24h\":"    + String(use24HourMode?"true":"false");
    j += ",\"theme\":"     + String(currentTheme);
    j += ",\"gmtOffset\":" + String(gmtOffset);
    j += ",\"wifiMode\":"  + String(wifiMode);
    j += ",\"activeWifiMode\":" + String(activeWifiMode);
    j += ",\"activeWifiLabel\":\"" + activeWifiShortText() + "\"";
    j += ",\"activeWifiModeText\":\"" + activeWifiModeText() + "\"";
    j += ",\"wifiConnected\":" + String(wifiConnected ? "true" : "false");
    j += ",\"ip\":\""      + ip + "\"";
    j += ",\"alarms\":"    + alarmsJson();
    j += "}";
    sendJson(j);
  });

  // Set time
  server.on("/api/time", HTTP_POST, [](){
    if (server.hasArg("plain")) {
      String b = server.arg("plain");
      // parse {"h":X,"m":X,"s":X,"d":X,"mo":X,"y":X}
      auto hasKey = [&](const char* key) -> bool {
        return b.indexOf(String(key)+":") >= 0;
      };
      auto getVal = [&](const char* key, int fallback) -> int {
        int idx = b.indexOf(String(key)+":");
        if (idx < 0) return fallback;
        idx += strlen(key) + 1;
        return b.substring(idx).toInt();
      };
      clockHour   = constrain(getVal("\"h\"", clockHour),   0, 23);
      clockMinute = constrain(getVal("\"m\"", clockMinute), 0, 59);
      clockSecond = constrain(getVal("\"s\"", clockSecond), 0, 59);

      if (hasKey("\"d\"") && hasKey("\"mo\"") && hasKey("\"y\"")) {
        int y  = constrain(getVal("\"y\"",  clockYear),  2000, 2099);
        int mo = constrain(getVal("\"mo\"", clockMonth), 1, 12);
        int d  = constrain(getVal("\"d\"",  clockDay),   1, daysInMonth(mo, y));
        clockYear = y;
        clockMonth = mo;
        clockDay = d;
        updateDowFromDate();
      }

      ntpSynced = false; // manual date/time overrides last NTP value
      lastClockMillis = millis();
    }
    sendJson("{\"ok\":true}");
  });

  // NTP sync
  server.on("/api/ntp", HTTP_POST, [](){
    syncNTP();
    sendJson("{\"ok\":true}");
  });

  // GMT offset
  server.on("/api/gmt", HTTP_POST, [](){
    if (server.hasArg("plain")) {
      String b = server.arg("plain");
      int idx = b.indexOf("\"gmt\":") + 6;
      gmtOffset = b.substring(idx).toInt();
      savePrefs();
    }
    sendJson("{\"ok\":true}");
  });

  // Alarms
  server.on("/api/alarms", HTTP_POST, [](){
    if (server.hasArg("plain")) {
      String b = server.arg("plain");
      // Simple positional parser (no full JSON lib needed)
      int pos = 0;
      for (int i = 0; i < MAX_ALARMS; i++) {
        auto find = [&](const char* key) -> int {
          int p = b.indexOf(String(key), pos);
          if (p < 0) return -1;
          p += strlen(key);
          while (p < (int)b.length() && (b[p]==':'||b[p]==' ')) p++;
          return p;
        };
        auto getInt = [&](const char* key) -> int {
          int p = find(key); if (p<0) return 0;
          return b.substring(p).toInt();
        };
        auto getBool = [&](const char* key) -> bool {
          int p = find(key); if (p<0) return false;
          return b.substring(p,p+4) == "true";
        };
        auto getStr = [&](const char* key) -> String {
          int p = find(key); if (p<0) return "";
          p++; int e = b.indexOf('"',p); return b.substring(p,e);
        };
        // advance pos to next alarm block
        if (i > 0) pos = b.indexOf('{', pos+1);
        alarms[i].hour    = getInt("\"hour\"");
        alarms[i].minute  = getInt("\"minute\"");
        alarms[i].enabled = getBool("\"enabled\"");
        String lbl = getStr("\"label\"");
        lbl.toCharArray(alarms[i].label, 24);
        alarms[i].onMon = getBool("\"onMon\"");
        alarms[i].onTue = getBool("\"onTue\"");
        alarms[i].onWed = getBool("\"onWed\"");
        alarms[i].onThu = getBool("\"onThu\"");
        alarms[i].onFri = getBool("\"onFri\"");
        alarms[i].onSat = getBool("\"onSat\"");
        alarms[i].onSun = getBool("\"onSun\"");
        pos = b.indexOf('}', pos) + 1;
      }
      savePrefs();
    }
    sendJson("{\"ok\":true}");
  });

  // Settings
  server.on("/api/settings", HTTP_POST, [](){
    if (server.hasArg("plain")) {
      String b = server.arg("plain");
      use24HourMode = b.indexOf("\"use24h\":true") >= 0;
      savePrefs();
    }
    sendJson("{\"ok\":true}");
  });

  // Theme
  server.on("/api/theme", HTTP_POST, [](){
    if (server.hasArg("plain")) {
      String b = server.arg("plain");
      int idx = b.indexOf("\"theme\":") + 8;
      currentTheme = constrain(b.substring(idx).toInt(), 0, THEME_COUNT - 1);
      savePrefs();
    }
    sendJson("{\"ok\":true}");
  });

  // WiFi
  server.on("/api/wifi", HTTP_POST, [](){
    if (server.hasArg("plain")) {
      String b = server.arg("plain");
      auto getStr = [&](const char* key) -> String {
        int p = b.indexOf(String(key));
        if (p<0) return "";
        p = b.indexOf('"', p+strlen(key)+2)+1;
        int e = b.indexOf('"',p);
        return b.substring(p,e);
      };
      int modePos = b.indexOf("\"mode\":") + 7;
      wifiMode = constrain(b.substring(modePos).toInt(), APP_WIFI_MODE_AP, APP_WIFI_MODE_STA);
      String ssid = getStr("\"ssid\"");
      String pass = getStr("\"pass\"");
      ssid.toCharArray(staSsid,64);
      pass.toCharArray(staPass,64);
      savePrefs();
      server.send(200,"application/json","{\"ok\":true}");
      delay(500);
      ESP.restart();
    }
  });

  server.onNotFound([](){
    server.sendHeader("Location","/");
    server.send(301);
  });
}

// ─── DRAW FUNCTIONS ───────────────────────────────────────────
void drawClockHand(float angleDeg, int length, uint16_t color, int thick=1) {
  float rad = angleDeg * DEG_TO_RAD;
  int cx=110, cy=125;
  int x = cx + cos(rad)*length;
  int y = cy + sin(rad)*length;
  tft.drawLine(cx, cy, x, y, color);
  if (thick>1) {
    tft.drawLine(cx+1, cy, x+1, y, color);
    tft.drawLine(cx, cy+1, x, y+1, color);
  }
}

void drawAnalogClock() {
  int cx=110, cy=125, r=58;
  tft.fillCircle(cx, cy, r+6, TFT_BLACK);
  tft.drawCircle(cx, cy, r,   accentCol());
  tft.drawCircle(cx, cy, r+1, accentCol());

  for (int i=0; i<60; i++) {
    float ang = (i*6-90)*DEG_TO_RAD;
    int ox=cx+cos(ang)*r, oy=cy+sin(ang)*r;
    int il=(i%5==0)?r-9:r-4;
    int ix=cx+cos(ang)*il, iy=cy+sin(ang)*il;
    tft.drawLine(ix,iy,ox,oy, (i%5==0)?accentCol():TFT_DARKGREY);
  }

  float sA = clockSecond*6-90;
  float mA = clockMinute*6 + clockSecond*0.1 - 90;
  float hA = (clockHour%12)*30 + clockMinute*0.5 - 90;

  drawClockHand(hA, 30, TFT_WHITE, 2);
  drawClockHand(mA, 43, accentCol(), 2);
  drawClockHand(sA, 50, THEMES[currentTheme][1]);

  tft.fillCircle(cx, cy, 4, TFT_YELLOW);
}

void drawDigitalTime() {
  tft.fillRect(0, 2, 225, 52, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(accentCol(), TFT_BLACK);
  tft.drawString(getTimeText(), 110, 5, 4);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(getDateText(), 110, 36, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawAlarmIndicator() {
  tft.fillRect(0, 54, 225, 18, TFT_BLACK);
  bool anyOn = false;
  for (auto& a : alarms) if (a.enabled) { anyOn=true; break; }
  if (anyOn) {
    tft.fillCircle(8, 63, 5, TFT_RED);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("ALM", 18, 56, 2);
  }

  // Keep the main clock screen clean: do not print "WiFi" text here.
  // Show only NTP when date/time has actually synced.
  if (ntpSynced) {
    tft.setTextColor(accentCol(), TFT_BLACK);
    tft.drawString("NTP", 60, 56, 2);
  }
}

void drawBottomInfo() {
  tft.fillRect(0, 200, 225, 40, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  for (int i=0; i<MAX_ALARMS; i++) {
    if (alarms[i].enabled) {
      tft.drawString("ALM:" + getAlarmText(i), 4, 205, 2);
      break;
    }
  }
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  String ipStr = currentWifiIp();
  tft.drawString(ipStr, 4, 218, 1);
}

void drawRingingBanner() {
  tft.fillRect(0, 38, 225, 28, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("! ALARM RINGING !", 112, 52, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawWifiIcon() {
  // Small symbol-only signal indicator. No WiFi text on the main screen.
  tft.fillRect(278, 0, 42, 16, TFT_BLACK);
  uint16_t col = wifiConnected ? accentCol() : TFT_DARKGREY;

  tft.fillRect(282, 10, 3, 2, col);
  tft.fillRect(287, 8,  3, 4, col);
  tft.fillRect(292, 6,  3, 6, col);
  tft.fillRect(297, 4,  3, 8, col);

  if (!wifiConnected) {
    tft.drawLine(281, 3, 302, 13, TFT_RED);
  }
}

void drawMainScreen() {
  screenMode = SCREEN_MAIN;
  tft.fillScreen(TFT_BLACK);
  drawDigitalTime();
  drawAlarmIndicator();
  drawAnalogClock();
  drawBottomInfo();
  drawWifiIcon();

  // Right side buttons
  drawButton(btnSet,      accentCol(), accentCol());
  drawButton(btnSW,       accentCol(), accentCol());
  drawButton(btnSettings, accentCol(), accentCol());
  drawButton(btnWifi,     accentCol(), accentCol());

  if (alarmRinging) {
    drawButton(btnStop, TFT_RED, TFT_WHITE, TFT_RED);
    drawRingingBanner();
  }
}

// ─── ALARM SET SCREEN ─────────────────────────────────────────
void drawAlarmSetScreen() {
  screenMode = SCREEN_ALARM_SET;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(accentCol(), TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("ALARMS", 160, 6, 4);
  tft.setTextDatum(TL_DATUM);

  Alarm& a = alarms[currentAlarmIndex];

  // Alarm index and name
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(String(currentAlarmIndex+1)+"/"+String(MAX_ALARMS)+" "+String(a.label), 10, 35, 2);

  // Time display
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(getAlarmText(currentAlarmIndex), 80, 55, 6);
  tft.setTextDatum(TL_DATUM);

  // Toggle button
  if (a.enabled) drawButton(btnAlmToggle, TFT_GREEN, TFT_BLACK, TFT_GREEN);
  else           drawButton(btnAlmToggle, TFT_RED,   TFT_RED);

  tft.setTextColor(a.enabled?TFT_BLACK:TFT_RED, a.enabled?TFT_GREEN:TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(a.enabled?"ENABLED":"DISABLED", btnAlmToggle.x+btnAlmToggle.w/2, btnAlmToggle.y+btnAlmToggle.h/2, 2);
  tft.setTextDatum(TL_DATUM);

  drawButton(btnAlmH_up, accentCol(), accentCol());
  drawButton(btnAlmH_dn, accentCol(), accentCol());
  drawButton(btnAlmM_up, accentCol(), accentCol());
  drawButton(btnAlmM_dn, accentCol(), accentCol());

  // Day labels
  String dayNames[7] = {"Mo","Tu","We","Th","Fr","Sa","Su"};
  bool*  dayPtrs[7]  = {&a.onMon,&a.onTue,&a.onWed,&a.onThu,&a.onFri,&a.onSat,&a.onSun};
  for (int d=0; d<7; d++) {
    int dx = 145 + d*13, dy = 105;
    bool on = *dayPtrs[d];
    tft.fillRoundRect(dx, dy, 11, 14, 2, on ? accentCol() : TFT_DARKGREY);
    tft.setTextColor(on ? TFT_BLACK : TFT_DARKGREY, on ? accentCol() : TFT_DARKGREY);
    tft.drawString(dayNames[d].substring(0,1), dx+1, dy+2, 1);
  }
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("TAP DAYS:", 145, 95, 1);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Date: " + getDateText(), 10, 145, 2);
  tft.setTextColor(accentCol(), TFT_BLACK);
  tft.drawString("Current: " + getTimeText(), 10, 165, 2);

  drawButton(btnBack,    accentCol(), accentCol());
  drawButton(btnPrevAlm, TFT_DARKGREY, TFT_DARKGREY);
  drawButton(btnNextAlm, TFT_DARKGREY, TFT_DARKGREY);

  if (alarmRinging) drawButton(btnStop, TFT_RED, TFT_WHITE, TFT_RED);
}

// ─── STOPWATCH SCREEN ─────────────────────────────────────────
void drawStopwatchScreen() {
  screenMode = SCREEN_STOPWATCH;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(accentCol(), TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("STOPWATCH", 160, 6, 4);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(getSwText(), 160, 70, 4);
  tft.setTextDatum(TL_DATUM);

  if (swRunning) drawButton(btnSwStop,  TFT_RED,   TFT_WHITE, TFT_RED);
  else           drawButton(btnSwStart, TFT_GREEN, TFT_BLACK, TFT_GREEN);
  drawButton(btnSwReset, TFT_DARKGREY, TFT_DARKGREY);
  drawButton(btnSwBack,  accentCol(), accentCol());
}

// ─── WIFI SCREEN ──────────────────────────────────────────────
void drawWifiScreen() {
  screenMode = SCREEN_WIFI;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(accentCol(), TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Wi-Fi", 160, 6, 4);
  tft.setTextDatum(TL_DATUM);

  String ip   = currentWifiIp();

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Mode: " + activeWifiModeText(), 10, 50, 2);
  tft.setTextColor(accentCol(), TFT_BLACK);
  tft.drawString("IP:   " + ip, 10, 80, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Open browser: http://"+ip, 10, 120, 2);
  tft.drawString("to access dashboard", 10, 140, 2);

  if (ntpSynced) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("NTP Synced", 10, 168, 2);
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("NTP Not synced", 10, 168, 2);
  }

  drawButton(btnWifiBack, accentCol(), accentCol());
}

// ─── SETTINGS SCREEN ──────────────────────────────────────────
void drawSettingsScreen() {
  screenMode = SCREEN_SETTINGS;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(accentCol(), TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("SETTINGS", 160, 6, 4);
  tft.setTextDatum(TL_DATUM);

  drawButton(btnTheme,  accentCol(), accentCol());
  drawButton(btnMode24, accentCol(), accentCol());
  drawButton(btnNTP,    accentCol(), accentCol());
  drawButton(btnSetBack,accentCol(), accentCol());

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(use24HourMode?"24-Hour":"12-Hour", 170, 133, 2);

  // Theme swatches: 8 unique accent themes in 2 rows
  String themeNames[THEME_COUNT] = {
    "CYBER", "AMBER", "NEON", "PINK",
    "ROYAL", "PURPLE", "SUNSET", "ICE"
  };

  for (int t = 0; t < THEME_COUNT; t++) {
    int x = 170 + (t % 4) * 22;
    int y = 88  + (t / 4) * 24;
    tft.fillRoundRect(x, y, 16, 18, 3, THEMES[t][0]);
    if (t == currentTheme) {
      tft.drawRoundRect(x - 2, y - 2, 20, 22, 3, TFT_WHITE);
    }
  }

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(themeNames[currentTheme], 170, 65, 1);
}

// ─── CLOCK UPDATE ─────────────────────────────────────────────
bool dayMatches(Alarm& a) {
  bool days[7] = {a.onSun,a.onMon,a.onTue,a.onWed,a.onThu,a.onFri,a.onSat};

  // Use NTP/system date when available, otherwise use the internal date state.
  struct tm ti;
  if (ntpSynced && getLocalTime(&ti,0)) {
    return days[ti.tm_wday]; // 0=Sun
  }

  int dow = constrain(clockDow, 0, 6);
  return days[dow];
}

void updateClock() {
  unsigned long now = millis();
  while (now - lastClockMillis >= 1000) {
    lastClockMillis += 1000;
    if (++clockSecond >= 60) {
      clockSecond = 0;
      if (++clockMinute >= 60) {
        clockMinute = 0;
        if (++clockHour >= 24) {
          clockHour = 0;
          incrementDate();
        }
      }
    }

    // Countdown timer
    if (timerRunning && timerRemaining > 0) {
      timerRemaining--;
      if (timerRemaining == 0) {
        timerRunning = false;
        alarmRinging = true; // reuse alarm ringing
        ringAlarmIndex = -2;
      }
    }

    // Alarm check
    if (!alarmRinging) {
      int minuteOfDay = clockHour*60+clockMinute;
      int todayStamp  = dateStamp();
      for (int i=0; i<MAX_ALARMS; i++) {
        if (alarms[i].enabled && alarms[i].hour==clockHour && alarms[i].minute==clockMinute &&
            clockSecond==0 && (lastAlarmMinute!=minuteOfDay || lastAlarmDateStamp!=todayStamp) && dayMatches(alarms[i])) {
          alarmRinging  = true;
          ringAlarmIndex= i;
          lastAlarmMinute=minuteOfDay;
          lastAlarmDateStamp=todayStamp;
          drawMainScreen();
          break;
        }
      }
    }

    // Refresh screen
    if (screenMode==SCREEN_MAIN) {
      drawDigitalTime(); drawAnalogClock(); drawBottomInfo(); drawAlarmIndicator();
      if (alarmRinging) { drawButton(btnStop,TFT_RED,TFT_WHITE,TFT_RED); drawRingingBanner(); }
    } else if (screenMode==SCREEN_ALARM_SET) {
      tft.fillRect(10, 145, 205, 38, TFT_BLACK);
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.drawString("Date: "+getDateText(), 10, 145, 2);
      tft.setTextColor(accentCol(), TFT_BLACK);
      tft.drawString("Current: "+getTimeText(), 10, 165, 2);
    } else if (screenMode==SCREEN_STOPWATCH) {
      tft.fillRect(30, 65, 260, 40, TFT_BLACK);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString(getSwText(), 160, 70, 4);
      tft.setTextDatum(TL_DATUM);
    }
  }
}

// ─── BUZZER + LED ─────────────────────────────────────────────
void updateBuzzerAndLed() {
  if (alarmRinging) {
    bool s = (millis()/200) % 2;
    digitalWrite(BUZZER_PIN, s);
    digitalWrite(ALARM_LED_PIN, s);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    bool anyOn = false;
    for (auto& a: alarms) if(a.enabled){anyOn=true;break;}
    digitalWrite(ALARM_LED_PIN, anyOn ? HIGH : LOW);
  }
}

void stopAlarm() {
  alarmRinging   = false;
  ringAlarmIndex = -1;
  digitalWrite(BUZZER_PIN, LOW);
  if (screenMode==SCREEN_MAIN) drawMainScreen();
  else if (screenMode==SCREEN_ALARM_SET) drawAlarmSetScreen();
}

// ─── TOUCH HANDLERS ───────────────────────────────────────────
void handleMainTouch(uint16_t x, uint16_t y) {
  if (alarmRinging && isInside(btnStop,x,y))  { stopAlarm(); return; }
  if (isInside(btnSet,x,y))       { drawAlarmSetScreen(); return; }
  if (isInside(btnSW,x,y))        { drawStopwatchScreen(); return; }
  if (isInside(btnSettings,x,y))  { drawSettingsScreen(); return; }
  if (isInside(btnWifi,x,y))      { drawWifiScreen(); return; }
}

void handleAlarmTouch(uint16_t x, uint16_t y) {
  if (alarmRinging && isInside(btnStop,x,y)) { stopAlarm(); return; }
  if (isInside(btnBack,x,y))      { drawMainScreen(); return; }
  if (isInside(btnPrevAlm,x,y))   { currentAlarmIndex=(currentAlarmIndex+MAX_ALARMS-1)%MAX_ALARMS; drawAlarmSetScreen(); return; }
  if (isInside(btnNextAlm,x,y))   { currentAlarmIndex=(currentAlarmIndex+1)%MAX_ALARMS; drawAlarmSetScreen(); return; }
  if (isInside(btnAlmToggle,x,y)) { alarms[currentAlarmIndex].enabled=!alarms[currentAlarmIndex].enabled; savePrefs(); drawAlarmSetScreen(); return; }
  if (isInside(btnAlmH_up,x,y))   { if(++alarms[currentAlarmIndex].hour>=24)alarms[currentAlarmIndex].hour=0; savePrefs(); drawAlarmSetScreen(); return; }
  if (isInside(btnAlmH_dn,x,y))   { if(--alarms[currentAlarmIndex].hour<0)alarms[currentAlarmIndex].hour=23; savePrefs(); drawAlarmSetScreen(); return; }
  if (isInside(btnAlmM_up,x,y))   { if(++alarms[currentAlarmIndex].minute>=60)alarms[currentAlarmIndex].minute=0; savePrefs(); drawAlarmSetScreen(); return; }
  if (isInside(btnAlmM_dn,x,y))   { if(--alarms[currentAlarmIndex].minute<0)alarms[currentAlarmIndex].minute=59; savePrefs(); drawAlarmSetScreen(); return; }

  // Day toggles
  Alarm& a = alarms[currentAlarmIndex];
  bool* dayPtrs[7]={&a.onMon,&a.onTue,&a.onWed,&a.onThu,&a.onFri,&a.onSat,&a.onSun};
  for (int d=0; d<7; d++) {
    int dx=145+d*13, dy=105;
    if (x>=dx && x<=dx+11 && y>=dy && y<=dy+14) {
      *dayPtrs[d] = !*dayPtrs[d]; savePrefs(); drawAlarmSetScreen(); return;
    }
  }
}

void handleStopwatchTouch(uint16_t x, uint16_t y) {
  if (isInside(btnSwBack,x,y)) { drawMainScreen(); return; }
  if (isInside(swRunning?btnSwStop:btnSwStart,x,y)) {
    if (swRunning) { swElapsed+=millis()-swStartMillis; swRunning=false; }
    else           { swStartMillis=millis(); swRunning=true; }
    drawStopwatchScreen(); return;
  }
  if (isInside(btnSwReset,x,y)) { swElapsed=0; swRunning=false; drawStopwatchScreen(); return; }
}

void handleWifiTouch(uint16_t x, uint16_t y) {
  if (isInside(btnWifiBack,x,y)) { drawMainScreen(); return; }
}

void handleSettingsTouch(uint16_t x, uint16_t y) {
  if (isInside(btnSetBack,x,y)) { drawMainScreen(); return; }
  if (isInside(btnMode24,x,y))  { use24HourMode=!use24HourMode; savePrefs(); drawSettingsScreen(); return; }
  if (isInside(btnNTP,x,y))     { syncNTP(); drawSettingsScreen(); return; }
  if (isInside(btnTheme,x,y))   { currentTheme=(currentTheme+1)%THEME_COUNT; savePrefs(); drawSettingsScreen(); return; }
}

void handleTouch() {
  uint16_t x, y;
  if (!tft.getTouch(&x,&y)) return;
  if (millis()-lastTouchMillis < 250) return;
  lastTouchMillis = millis();

  Serial.printf("Touch X:%d Y:%d\n",x,y);

  switch(screenMode) {
    case SCREEN_MAIN:       handleMainTouch(x,y); break;
    case SCREEN_ALARM_SET:  handleAlarmTouch(x,y); break;
    case SCREEN_STOPWATCH:  handleStopwatchTouch(x,y); break;
    case SCREEN_WIFI:       handleWifiTouch(x,y); break;
    case SCREEN_SETTINGS:   handleSettingsTouch(x,y); break;
  }
}

// ─── COMPILE TIME SEED ────────────────────────────────────────
void setClockFromCompileTime() {
  int h,m,s;
  sscanf(__TIME__,"%d:%d:%d",&h,&m,&s);
  clockHour=h; clockMinute=m; clockSecond=s;

  char mon[4] = {0};
  int d, y;
  if (sscanf(__DATE__, "%3s %d %d", mon, &d, &y) == 3) {
    clockMonth = monthFromName(mon);
    clockDay   = d;
    clockYear  = y;
    updateDowFromDate();
  }
}

void runTouchCalibration() {
  uint16_t cal[5];
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Touch corners", 70, 100, 2);
  tft.calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 15);
  Serial.print("uint16_t touchCalData[5] = { ");
  for (int i=0;i<5;i++){if(i)Serial.print(", ");Serial.print(cal[i]);}
  Serial.println(" };");
  delay(3000);
}

// ─── SETUP & LOOP ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN,    OUTPUT);
  pinMode(ALARM_LED_PIN, OUTPUT);
  pinMode(TOUCH_IRQ,     INPUT);
  digitalWrite(BUZZER_PIN,    LOW);
  digitalWrite(ALARM_LED_PIN, LOW);

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);

  // Splash
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CLOCK", 160, 105, 6);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Starting...", 160, 160, 2);
  tft.setTextDatum(TL_DATUM);

  loadPrefs();

  if (RUN_TOUCH_CALIBRATION) runTouchCalibration();
  tft.setTouch(touchCalData);

  setClockFromCompileTime();
  lastClockMillis = millis();

  // WiFi
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting...", 160, 185, 2);
  tft.setTextDatum(TL_DATUM);
  initWifi();

  setupRoutes();
  server.begin();

  Serial.println("=== Clock Ready ===");
  String ip = currentWifiIp();
  Serial.print("Dashboard: http://"); Serial.println(ip);

  delay(1200);
  drawMainScreen();
}

void loop() {
  server.handleClient();
  updateClock();
  updateBuzzerAndLed();
  handleTouch();
}
