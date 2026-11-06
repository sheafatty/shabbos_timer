#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Update.h>
#include <math.h>
#include "yom_tov_table.h"

const char* ap_ssid = "MelachaTimer";
const char* ap_pass = "48592104";

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
RTC_DS3231 rtc;

// Set the LCD address to 0x27 or 0x3F for a 20 chars and 4 lines display
LiquidCrystal_I2C lcd(0x27, 20, 4);

const int RELAY_PIN = 18;

// Configurable Settings (NVS Flash)
int opMode = 0;            // 0 = Shabbos/Yom Tov Mode, 1 = 7-Day Standard Timer Mode
int manualMode = 0;        // 0 = Auto, 1 = Force ON, 2 = Force OFF
int dstRule = 0;           // 0 = Disabled, 1 = UK / Europe, 2 = USA

// Shabbos / Yom Tov Trigger Settings
int startTriggerType = 0;   
int fixedStartHour = 18;    
int fixedStartMin = 0;      
int sunsetStartOffset = 18; 

int endTriggerType = 0;     
int fixedEndHour = 22;      
int fixedEndMin = 0;        
int sunsetEndOffset = 42;   

// Location & Wiring
double storedLat = 40.7128; 
double storedLon = -74.0060;
int tzOffset = -5;        
int invertRelay = 0;      

// Wi-Fi Station Credentials (Home Network)
String homeSsid = "";
String homePass = "";

// 7-Day Timer Schedule
bool d1_en[7] = {true, true, true, true, true, true, true};
int d1_on_h[7] = {6,6,6,6,6,6,6}; int d1_on_m[7] = {0,0,0,0,0,0,0};
int d1_off_h[7]= {9,9,9,9,9,9,9}; int d1_off_m[7] = {0,0,0,0,0,0,0};

bool d2_en[7] = {false, false, false, false, false, false, false};
int d2_on_h[7] = {17,17,17,17,17,17,17}; int d2_on_m[7] = {0,0,0,0,0,0,0};
int d2_off_h[7]= {23,23,23,23,23,23,23}; int d2_off_m[7] = {0,0,0,0,0,0,0};

// LCD Page-Flipping Tracking Variables
unsigned long lastLcdFlip = 0;
bool lcdFlipState = false;

bool checkAuth();
void handleRoot();
void handleSchedule();
void handleSettings();
void handleTime();
void handleSaveOverride();
void handleSaveSchedule();
void handleSaveSettings();
void handleSaveTime();
bool isHolyDay(DateTime dt);
bool shouldRelayBeOn(DateTime dtCheck);
bool isDST(DateTime dt);
DateTime getLocalTime(DateTime rawRtcTime);
double calculateSunsetHour(int year, int month, int day, double lat, double lon, int tz, bool dstActive);
String getNextTriggers(DateTime now);
String getNextTriggerShort(DateTime now);
void loadPreferences();
void updateLCD();

// Basic Authentication Check (Username: admin, Password: shabbos123)
bool checkAuth() {
  return server.authenticate("admin", "shabbos123");
}

// Modern UI Stylesheet & Header Helper
String getHeader(String title) {
  String html = "<html lang='en'><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += ":root { --primary: #0066cc; --success: #28a745; --danger: #dc3545; --bg: #f4f6f9; --card-bg: #ffffff; --text: #333333; --border: #e1e4e8; }";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 15px; display: flex; justify-content: center; }";
  html += ".container { width: 100%; max-width: 440px; }";
  html += "h2 { font-size: 1.4rem; margin-bottom: 15px; color: #1a1a1a; font-weight: 700; }";
  html += "h3 { font-size: 1.05rem; margin-top: 0; color: #444; border-bottom: 2px solid var(--border); padding-bottom: 6px; margin-bottom: 12px; }";
  html += ".card { background: var(--card-bg); padding: 18px; margin-bottom: 15px; border-radius: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.03); border: 1px solid var(--border); }";
  html += ".nav { display: flex; justify-content: space-between; background: var(--card-bg); padding: 12px 15px; border-radius: 10px; margin-bottom: 15px; box-shadow: 0 2px 6px rgba(0,0,0,0.03); border: 1px solid var(--border); flex-wrap: wrap; gap: 5px; }";
  html += ".nav a { text-decoration: none; color: var(--primary); font-weight: 600; font-size: 0.85rem; padding: 4px 8px; border-radius: 6px; transition: background 0.2s; }";
  html += ".nav a:hover { background: rgba(0,102,204,0.1); }";
  html += "label { display: block; font-weight: 500; font-size: 0.85rem; margin-bottom: 4px; color: #555; }";
  html += "input[type='text'], input[type='number'], input[type='password'], input[type='time'], input[type='datetime-local'], select { width: 100%; padding: 9px 12px; margin-bottom: 12px; border: 1px solid var(--border); border-radius: 8px; font-size: 0.95rem; box-sizing: border-box; background: #fff; }";
  html += "button { background: var(--primary); color: white; border: none; padding: 10px; width: 100%; border-radius: 8px; font-size: 0.95rem; font-weight: 600; cursor: pointer; transition: opacity 0.2s; }";
  html += "button:hover { opacity: 0.9; }";
  html += "table { width: 100%; font-size: 0.8rem; border-collapse: collapse; margin-top: 5px; }";
  html += "th, td { padding: 6px 2px; border-bottom: 1px solid var(--border); text-align: center; }";
  html += "th { font-weight: 600; color: #666; }";
  html += "</style></head><body><div class='container'><h2>" + title + "</h2>";
  return html;
}

String getNavMenu() {
  return "<div class='nav'>"
         "<a href='/'>Status</a>"
         "<a href='/schedule'>Schedule</a>"
         "<a href='/settings'>Settings</a>"
         "<a href='/time'>RTC Time</a>"
         "</div>";
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  
  Wire.begin(21, 22);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Shabbos Timer v2.0");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  if (!rtc.begin()) {
    Serial.println("RTC error!");
  }

  loadPreferences();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_pass);

  if (homeSsid.length() > 0) {
    WiFi.begin(homeSsid.c_str(), homePass.c_str());
  }

  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save-override", HTTP_POST, handleSaveOverride);
  server.on("/schedule", HTTP_GET, handleSchedule);
  server.on("/save-schedule", HTTP_POST, handleSaveSchedule);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/save-settings", HTTP_POST, handleSaveSettings);
  server.on("/time", HTTP_GET, handleTime);
  server.on("/save-time", HTTP_POST, handleSaveTime);
  
  // Protected OTA Update Web Handlers
  server.on("/update", HTTP_GET, []() {
    if (!checkAuth()) return server.requestAuthentication();
    String html = getHeader("OTA Firmware Update") + getNavMenu();
    html += "<div class='card'>";
    html += "<h3>Wireless Firmware Update</h3>";
    html += "<p style='font-size:0.85rem;color:#666;'>Select your compiled binary file (.bin) generated from Arduino IDE (Sketch -> Export Compiled Binary).</p>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='update' style='margin-bottom:15px;'><br>";
    html += "<button type='submit' style='background:#ffc107;color:#333;font-weight:bold;'>Upload & Flash</button>";
    html += "</form></div></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/update", HTTP_POST, []() {
    if (!checkAuth()) return server.requestAuthentication();
    server.send(200, "text/html", Update.hasError() ? "UPDATE FAILED! Check serial logs." : "UPDATE SUCCESSFUL! Rebooting device...");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("OTA Update Started: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("OTA Update Success: %u bytes. Rebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.onNotFound(handleRoot);
  server.begin();
  
  delay(1500); 
}

void loadPreferences() {
  preferences.begin("timer-config", true);
  opMode = preferences.getInt("opMode", 0);
  manualMode = preferences.getInt("manMode", 0);
  dstRule = preferences.getInt("dstRule", 0);
  
  startTriggerType = preferences.getInt("stTrig", 0);
  fixedStartHour = preferences.getInt("fStartH", 18);
  fixedStartMin = preferences.getInt("fStartM", 0);
  sunsetStartOffset = preferences.getInt("stOffset", 18);

  endTriggerType = preferences.getInt("endTrig", 0);
  fixedEndHour = preferences.getInt("fEndH", 22);
  fixedEndMin = preferences.getInt("fEndM", 0);
  sunsetEndOffset = preferences.getInt("endOffset", 42);

  storedLat = preferences.getDouble("lat", 40.7128);
  storedLon = preferences.getDouble("lon", -74.0060);
  tzOffset = preferences.getInt("tz", -5);
  invertRelay = preferences.getInt("invRelay", 0);
  
  homeSsid = preferences.getString("wifiSsid", "");
  homePass = preferences.getString("wifiPass", "");

  for (int i = 0; i < 7; i++) {
    d1_en[i]    = preferences.getBool(("d1en" + String(i)).c_str(), true);
    d1_on_h[i]  = preferences.getInt(("d1onh" + String(i)).c_str(), 6);
    d1_on_m[i]  = preferences.getInt(("d1onm" + String(i)).c_str(), 0);
    d1_off_h[i] = preferences.getInt(("d1offh" + String(i)).c_str(), 9);
    d1_off_m[i] = preferences.getInt(("d1offm" + String(i)).c_str(), 0);
    
    d2_en[i]    = preferences.getBool(("d2en" + String(i)).c_str(), false);
    d2_on_h[i]  = preferences.getInt(("d2onh" + String(i)).c_str(), 17);
    d2_on_m[i]  = preferences.getInt(("d2onm" + String(i)).c_str(), 0);
    d2_off_h[i] = preferences.getInt(("d2offh" + String(i)).c_str(), 23);
    d2_off_m[i] = preferences.getInt(("d2offm" + String(i)).c_str(), 0);
  }
  preferences.end();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  DateTime rawNow = rtc.now();
  DateTime now = getLocalTime(rawNow); 
  bool activeState = false;

  if (manualMode == 1) {
    activeState = true;
  } else if (manualMode == 2) {
    activeState = false;
  } else {
    activeState = shouldRelayBeOn(now);
  }

  bool physicalState = (invertRelay == 1) ? !activeState : activeState;
  digitalWrite(RELAY_PIN, physicalState ? HIGH : LOW);

  // Update clean 20x4 LCD screen every second
  updateLCD();

  delay(1000);
}

// Format and display simplified status on the 20x4 I2C LCD
void updateLCD() {
  DateTime rawNow = rtc.now();
  DateTime now = getLocalTime(rawNow);
  bool activeState = (manualMode == 1) ? true : (manualMode == 2 ? false : shouldRelayBeOn(now));
  bool dstActive = isDST(rawNow);

  // Calculate today's sunset time
  double sunsetDec = calculateSunsetHour(now.year(), now.month(), now.day(), storedLat, storedLon, tzOffset, dstActive);
  int ssHour = (int)sunsetDec;
  int ssMin = (int)((sunsetDec - ssHour) * 60);

  // --- ROW 0: Date & Time formatted as DD/MM/YY HH:MM (Padded to clear old boot text) ---
  char line0[21];
  sprintf(line0, "%02d/%02d/%02d %02d:%02d", now.day(), now.month(), now.year() % 100, now.hour(), now.minute());
  while(strlen(line0) < 20) { strcat(line0, " "); }
  lcd.setCursor(0, 0);
  lcd.print(line0);

  // --- ROW 1: Relay State (Just ON or OFF) ---
  char line1[21];
  sprintf(line1, "Relay: %s", activeState ? "ON" : "OFF");
  while(strlen(line1) < 20) { strcat(line1, " "); }
  lcd.setCursor(0, 1);
  lcd.print(line1);

  // --- ROW 2: Sunset Time ---
  char line2[21];
  sprintf(line2, "Sunset: %02d:%02d", ssHour, ssMin);
  while(strlen(line2) < 20) { strcat(line2, " "); }
  lcd.setCursor(0, 2);
  lcd.print(line2);

  // --- ROW 3: Bottom Line Logic ---
  char line3[21];

  if (manualMode == 1) {
    strcpy(line3, "Override: Force ON");
  } 
  else if (manualMode == 2) {
    strcpy(line3, "Override: Force OFF");
  } 
  else {
    // Auto Mode: Alternate every 6 seconds between Mode and Next Trigger
    if (millis() - lastLcdFlip > 6000) {
      lcdFlipState = !lcdFlipState;
      lastLcdFlip = millis();
    }

    if (lcdFlipState) {
      String nextTrigStr = getNextTriggerShort(now);
      sprintf(line3, "%s", nextTrigStr.c_str());
    } else {
      String modeName = (opMode == 0) ? "Shabbos" : "7-Day";
      sprintf(line3, "Mode: %s (Auto)", modeName.c_str());
    }
  }

  while(strlen(line3) < 20) { strcat(line3, " "); }
  lcd.setCursor(0, 3);
  lcd.print(line3);
}

// Helper to get compact 20-character next trigger string for the LCD
String getNextTriggerShort(DateTime now) {
  bool lastState = shouldRelayBeOn(now);
  DateTime scanTime = now;
  for (int m = 1; m <= 1440; m++) { // Scan up to 24 hours ahead
    scanTime = scanTime + TimeSpan(0, 0, 1, 0);
    bool futureState = shouldRelayBeOn(scanTime);

    if (futureState != lastState) {
      const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      char buf[21];
      sprintf(buf, "Nxt: %s %02d:%02d %s", days[scanTime.dayOfTheWeek()], scanTime.hour(), scanTime.minute(), futureState ? "ON" : "OFF");
      return String(buf);
    }
  }
  return "Nxt: No events 24h";
}

bool isDST(DateTime dt) {
  if (dstRule == 0) return false;
  int m = dt.month();
  int d = dt.day();

  if (dstRule == 1) {
    if (m < 3 || m > 10) return false;
    if (m > 3 && m < 10) return true;
    int lastMarchSun = 31 - DateTime(dt.year(), 3, 31).dayOfTheWeek();
    int lastOctSun = 31 - DateTime(dt.year(), 10, 31).dayOfTheWeek();
    if (m == 3) return (d > lastMarchSun || (d == lastMarchSun && dt.hour() >= 1));
    if (m == 10) return (d < lastOctSun || (d == lastOctSun && dt.hour() < 1));
  }

  if (dstRule == 2) {
    if (m < 3 || m > 11) return false;
    if (m > 3 && m < 11) return true;
    int secondMarchSun = (1 + (7 - DateTime(dt.year(), 3, 1).dayOfTheWeek()) % 7) + 7;
    int firstNovSun = 1 + (7 - DateTime(dt.year(), 11, 1).dayOfTheWeek()) % 7;
    if (m == 3) return (d > secondMarchSun || (d == secondMarchSun && dt.hour() >= 2));
    if (m == 11) return (d < firstNovSun || (d == firstNovSun && dt.hour() < 2));
  }

  return false;
}

DateTime getLocalTime(DateTime rawRtcTime) {
  if (isDST(rawRtcTime)) {
    return rawRtcTime + TimeSpan(0, 1, 0, 0); 
  }
  return rawRtcTime; 
}

bool isHolyDay(DateTime dt) {
  if (dt.dayOfTheWeek() == 6) return true;
  for (int i = 0; i < YOM_TOV_TABLE_SIZE; i++) {
    if (pgm_read_word(&yomTovTable[i].year) == dt.year() &&
        pgm_read_byte(&yomTovTable[i].month) == dt.month() &&
        pgm_read_byte(&yomTovTable[i].day) == dt.day()) {
      return true;
    }
  }
  return false;
}

double calculateSunsetHour(int year, int month, int day, double lat, double lon, int tz, bool dstActive) {
  int daysMonths[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  int N = daysMonths[month - 1] + day;
  if (((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) && month > 2) N += 1;
  double lngHour = lon / 15.0;
  double t = N + ((12.0 - lngHour) / 24.0);
  double M = (0.9856 * t) - 3.289;
  double L = M + (1.916 * sin(M * M_PI / 180.0)) + (0.020 * sin(2 * M * M_PI / 180.0)) + 282.634;
  L = fmod(L, 360.0); if (L < 0) L += 360.0;
  double RA = atan(0.91764 * tan(L * M_PI / 180.0)) * 180.0 / M_PI;
  RA = fmod(RA, 360.0); if (RA < 0) RA += 360.0;
  RA = (RA + ((floor(L / 90.0)) * 90.0) - ((floor(RA / 90.0)) * 90.0)) / 15.0;
  double sinDec = 0.39782 * sin(L * M_PI / 180.0);
  double cosDec = cos(asin(sinDec));
  double zenith = 90.833;
  double cosH = (cos(zenith * M_PI / 180.0) - (sinDec * sin(lat * M_PI / 180.0))) / (cosDec * cos(lat * M_PI / 180.0));
  if (cosH > 1.0 || cosH < -1.0) return 18.0;
  double H = acos(cosH) * 180.0 / M_PI;
  H = H / 15.0;
  double T = H + RA - (0.06571 * t) - 6.622;
  double UT = fmod(T - lngHour, 24.0);
  if (UT < 0) UT += 24.0;
  
  int effectiveTz = tz + (dstActive ? 1 : 0);
  return fmod(UT + effectiveTz + 24.0, 24.0);
}

bool shouldRelayBeOn(DateTime dtCheck) {
  int currentMinTotal = (dtCheck.hour() * 60) + dtCheck.minute();
  bool dstActive = isDST(dtCheck);
  
  if (opMode == 1) {
    int dow = dtCheck.dayOfTheWeek();
    bool on1 = false, on2 = false;
    
    if (d1_en[dow]) {
      int m1Start = (d1_on_h[dow] * 60) + d1_on_m[dow];
      int m1End = (d1_off_h[dow] * 60) + d1_off_m[dow];
      on1 = (currentMinTotal >= m1Start && currentMinTotal < m1End);
    }
    if (d2_en[dow]) {
      int m2Start = (d2_on_h[dow] * 60) + d2_on_m[dow];
      int m2End = (d2_off_h[dow] * 60) + d2_off_m[dow];
      on2 = (currentMinTotal >= m2Start && currentMinTotal < m2End);
    }
    return (on1 || on2);
  }

  DateTime tomorrow = dtCheck + TimeSpan(1, 0, 0, 0);
  bool todayHoly = isHolyDay(dtCheck);
  bool tomorrowHoly = isHolyDay(tomorrow);

  int startThresholdMin = (fixedStartHour * 60) + fixedStartMin;
  if (startTriggerType == 1) {
    double sunsetDec = calculateSunsetHour(dtCheck.year(), dtCheck.month(), dtCheck.day(), storedLat, storedLon, tzOffset, dstActive);
    startThresholdMin = (int)(sunsetDec * 60) - sunsetStartOffset;
  }

  int endThresholdMin = (fixedEndHour * 60) + fixedEndMin;
  if (endTriggerType == 1) {
    double sunsetDec = calculateSunsetHour(dtCheck.year(), dtCheck.month(), dtCheck.day(), storedLat, storedLon, tzOffset, dstActive);
    endThresholdMin = (int)(sunsetDec * 60) + sunsetEndOffset;
  }

  if (!todayHoly && !tomorrowHoly) return false;
  if (!todayHoly && tomorrowHoly) return (currentMinTotal >= startThresholdMin);
  if (todayHoly && tomorrowHoly) return true;
  if (todayHoly && !tomorrowHoly) return (currentMinTotal < endThresholdMin);
  return false;
}

// Chronological Next Two Triggers Scanner (Web UI)
String getNextTriggers(DateTime now) {
  bool lastState = shouldRelayBeOn(now);
  String triggers[2];
  int foundCount = 0;

  DateTime scanTime = now;
  for (int m = 1; m <= 10080; m++) {
    scanTime = scanTime + TimeSpan(0, 0, 1, 0);
    bool futureState = shouldRelayBeOn(scanTime);

    if (futureState != lastState) {
      char buf[40];
      const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      const char* action = futureState ? "ON" : "OFF";
      sprintf(buf, "<b>%s</b> at %s %02d:%02d", action, days[scanTime.dayOfTheWeek()], scanTime.hour(), scanTime.minute());
      triggers[foundCount] = String(buf);
      foundCount++;
      lastState = futureState;
      if (foundCount >= 2) break;
    }
  }

  if (foundCount == 0) return "Next Triggers: None found";
  String res = "<b>Next Triggers:</b><br>1. " + triggers[0] + "<br>";
  if (foundCount > 1) {
    res += "2. " + triggers[1];
  }
  return res;
}

void sendRedirectResponse(String targetUrl) {
  String html = "<html lang='en'><head><meta http-equiv='refresh' content='1;url=" + targetUrl + "'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:-apple-system,sans-serif;margin:50px auto;text-align:center;background:#f4f6f9;color:#333;max-width:350px;}</style></head>";
  html += "<body><div style='background:white;padding:25px;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.05);'>";
  html += "<h3 style='color:#28a745;margin-top:0;font-size:1.1rem;'>Saved Successfully!</h3><p style='margin:0;color:#666;font-size:0.85rem;'>Returning...</p></div></body></html>";
  server.send(200, "text/html", html);
}

void handleRoot() {
  if (!checkAuth()) return server.requestAuthentication();

  DateTime rawNow = rtc.now();
  DateTime now = getLocalTime(rawNow); 
  bool activeState = (manualMode == 1) ? true : (manualMode == 2 ? false : shouldRelayBeOn(now));
  bool todayHoly = isHolyDay(now);
  DateTime tomorrow = now + TimeSpan(1, 0, 0, 0);
  bool tomorrowHoly = isHolyDay(tomorrow);
  bool dstActive = isDST(rawNow);
  
  char timeBuf[25];
  sprintf(timeBuf, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  double sunsetDec = calculateSunsetHour(now.year(), now.month(), now.day(), storedLat, storedLon, tzOffset, dstActive);
  int ssHour = (int)sunsetDec;
  int ssMin = (int)((sunsetDec - ssHour) * 60);
  char sunsetBuf[10];
  sprintf(sunsetBuf, "%02d:%02d", ssHour, ssMin);

  String html = getHeader("Shabbos Timer") + getNavMenu();

  html += "<div class='card'><h3>Live Status</h3>";
  html += "Time: <b>" + String(timeBuf) + "</b> <small>(" + String(dstActive ? "Summer" : "Winter") + ")</small><br>";
  html += "Sunset Today: <b>" + String(sunsetBuf) + "</b><br>";
  html += "Relay Output: <b style='color:" + String(activeState ? "#28a745" : "#dc3545") + ";'>" + String(activeState ? "ON" : "OFF") + "</b><br>";
  html += "Mode: <b>" + String(opMode == 0 ? "Shabbos / Yom Tov" : "7-Day Timer") + "</b><br>";
  html += "Today Holy: <b>" + String(todayHoly ? "Yes" : "No") + "</b> | Tomorrow: <b>" + String(tomorrowHoly ? "Yes" : "No") + "</b><br><hr style='border:0;border-top:1px solid #e1e4e8;margin:10px 0;'>";
  html += getNextTriggers(now) + "</div>";

  html += "<div class='card'><form action='/save-override' method='POST'>";
  html += "<h3>Manual Override</h3>";
  html += "<select name='manMode'>";
  html += "<option value='0'" + String(manualMode == 0 ? " selected" : "") + ">Auto (Timer Rules)</option>";
  html += "<option value='1'" + String(manualMode == 1 ? " selected" : "") + ">Force ON</option>";
  html += "<option value='2'" + String(manualMode == 2 ? " selected" : "") + ">Force OFF</option>";
  html += "</select><button type='submit'>Apply Override</button>";
  html += "</form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleSchedule() {
  if (!checkAuth()) return server.requestAuthentication();

  String html = getHeader("Schedule Configuration") + getNavMenu();

  html += "<div class='card'><form action='/save-schedule' method='POST'>";
  html += "<h3>Shabbos / Yom Tov Start (ON)</h3>";
  html += "<label>Start Trigger Type:</label><select name='stTrig'>";
  html += "<option value='0'" + String(startTriggerType == 0 ? " selected" : "") + ">Fixed Time</option>";
  html += "<option value='1'" + String(startTriggerType == 1 ? " selected" : "") + ">Sunset Offset (Before)</option>";
  html += "</select>";
  html += "<label>Fixed Start Time (HH:MM):</label><input type='time' name='fStart' value='" + String(fixedStartHour < 10 ? "0" : "") + String(fixedStartHour) + ":" + String(fixedStartMin < 10 ? "0" : "") + String(fixedStartMin) + "'>";
  html += "<label>Mins Before Sunset:</label><input type='number' name='stOffset' value='" + String(sunsetStartOffset) + "'>";

  html += "<h3>Shabbos / Yom Tov End (OFF)</h3>";
  html += "<label>End Trigger Type:</label><select name='endTrig'>";
  html += "<option value='0'" + String(endTriggerType == 0 ? " selected" : "") + ">Fixed Time</option>";
  html += "<option value='1'" + String(endTriggerType == 1 ? " selected" : "") + ">Sunset Offset (After / Havdalah)</option>";
  html += "</select>";
  html += "<label>Fixed End Time (HH:MM):</label><input type='time' name='fEnd' value='" + String(fixedEndHour < 10 ? "0" : "") + String(fixedEndHour) + ":" + String(fixedEndMin < 10 ? "0" : "") + String(fixedEndMin) + "'>";
  html += "<label>Mins After Sunset:</label><input type='number' name='endOffset' value='" + String(sunsetEndOffset) + "'>";

  html += "<h3>7-Day Timer Slots</h3><p style='font-size:0.8rem;color:#666;margin-top:0;'>Check box to enable slot, uncheck to disable.</p><table><tr><th>Day</th><th>Slot 1 (ON - OFF)</th><th>Slot 2 (ON - OFF)</th></tr>";
  String days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  for (int i = 0; i < 7; i++) {
    char s1_on[6], s1_off[6], s2_on[6], s2_off[6];
    sprintf(s1_on, "%02d:%02d", d1_on_h[i], d1_on_m[i]);
    sprintf(s1_off, "%02d:%02d", d1_off_h[i], d1_off_m[i]);
    sprintf(s2_on, "%02d:%02d", d2_on_h[i], d2_on_m[i]);
    sprintf(s2_off, "%02d:%02d", d2_off_h[i], d2_off_m[i]);

    html += "<tr><td><b>" + days[i] + "</b></td>";
    html += "<td><input type='checkbox' name='d1en" + String(i) + "'" + String(d1_en[i] ? " checked" : "") + "> <input type='time' name='d1on" + String(i) + "' value='" + String(s1_on) + "' style='width:60px;padding:4px;'>-<input type='time' name='d1off" + String(i) + "' value='" + String(s1_off) + "' style='width:60px;padding:4px;'></td>";
    html += "<td><input type='checkbox' name='d2en" + String(i) + "'" + String(d2_en[i] ? " checked" : "") + "> <input type='time' name='d2on" + String(i) + "' value='" + String(s2_on) + "' style='width:60px;padding:4px;'>-<input type='time' name='d2off" + String(i) + "' value='" + String(s2_off) + "' style='width:60px;padding:4px;'></td></tr>";
  }
  html += "</table><button type='submit' style='background:#28a745;margin-top:15px;'>Save Schedule</button>";
  html += "</form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleSettings() {
  if (!checkAuth()) return server.requestAuthentication();

  String html = getHeader("System Settings") + getNavMenu();
  html += "<script>function setCity(lat, lon, tz, dst) {";
  html += "document.getElementById('lat').value = lat;";
  html += "document.getElementById('lon').value = lon;";
  html += "document.getElementById('tz').value = tz;";
  html += "document.getElementById('dstRule').value = dst;";
  html += "}</script>";

  html += "<div class='card'><form action='/save-settings' method='POST'>";
  html += "<label>Operating Mode:</label><select name='opMode'>";
  html += "<option value='0'" + String(opMode == 0 ? " selected" : "") + ">Shabbos / Yom Tov Mode</option>";
  html += "<option value='1'" + String(opMode == 1 ? " selected" : "") + ">7-Day Standard Timer Mode</option>";
  html += "</select>";

  html += "<label>Daylight Saving Time (DST):</label><select name='dstRule' id='dstRule'>";
  html += "<option value='0'" + String(dstRule == 0 ? " selected" : "") + ">Disabled (No Auto DST)</option>";
  html += "<option value='1'" + String(dstRule == 1 ? " selected" : "") + ">UK / Europe (Last Sun Mar - Last Sun Oct)</option>";
  html += "<option value='2'" + String(dstRule == 2 ? " selected" : "") + ">USA (2nd Sun Mar - 1st Sun Nov)</option>";
  html += "</select>";

  html += "<label>Relay Wiring:</label><select name='invRelay'>";
  html += "<option value='0'" + String(invertRelay == 0 ? " selected" : "") + ">Normal (NO)</option>";
  html += "<option value='1'" + String(invertRelay == 1 ? " selected" : "") + ">Inverted (NC Fail-safe)</option>";
  html += "</select>";

  html += "<h3>Location Preset</h3>";
  html += "<label>Quick Select City:</label><select onchange='setCity(this.value.split(\",\")[0], this.value.split(\",\")[1], this.value.split(\",\")[2], this.value.split(\",\")[3])'>";
  html += "<option value=''>-- Choose Preset City --</option>";
  html += "<option value='51.5074,-0.1278,0,1'>London, UK</option>";
  html += "<option value='51.2194,4.4025,1,1'>Antwerp, Belgium</option>";
  html += "<option value='40.7128,-74.0060,-5,2'>New York City, USA</option>";
  html += "<option value='31.7683,35.2137,2,0'>Jerusalem, Israel</option>";
  html += "</select>";

  html += "<label>Latitude:</label><input type='text' id='lat' name='lat' value='" + String(storedLat, 4) + "'>";
  html += "<label>Longitude:</label><input type='text' id='lon' name='lon' value='" + String(storedLon, 4) + "'>";
  html += "<label>Timezone Offset (UTC):</label><input type='number' id='tz' name='tz' value='" + String(tzOffset) + "'>";

  html += "<button type='submit' style='background:#17a2b8;margin-top:5px;'>Save Settings</button>";
  html += "</form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleTime() {
  if (!checkAuth()) return server.requestAuthentication();

  DateTime now = rtc.now();
  char dtBuffer[25];
  sprintf(dtBuffer, "%04d-%02d-%02dT%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute());

  String html = getHeader("RTC Time Sync") + getNavMenu();
  html += "<div class='card'><form action='/save-time' method='POST'>";
  html += "<h3>Set Clock Time</h3>";
  html += "<p style='font-size:0.8rem;color:#d9534f;font-weight:600;'>Note: Set this to GMT / Standard (Non-Summer) Time. The system adds +1 hour automatically in summer if DST is enabled.</p>";
  html += "<input type='datetime-local' name='newdt' value='" + String(dtBuffer) + "'>";
  html += "<button type='submit' style='background:#ffc107;color:#333;font-weight:bold;'>Update RTC Clock</button>";
  html += "</form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleSaveOverride() {
  if (!checkAuth()) return server.requestAuthentication();
  if (server.hasArg("manMode")) manualMode = server.arg("manMode").toInt();
  preferences.begin("timer-config", false);
  preferences.putInt("manMode", manualMode);
  preferences.end();
  sendRedirectResponse("/");
}

void handleSaveSchedule() {
  if (!checkAuth()) return server.requestAuthentication();
  if (server.hasArg("stTrig")) startTriggerType = server.arg("stTrig").toInt();
  if (server.hasArg("fStart")) {
    String t = server.arg("fStart");
    fixedStartHour = t.substring(0, 2).toInt();
    fixedStartMin = t.substring(3, 5).toInt();
  }
  if (server.hasArg("stOffset")) sunsetStartOffset = server.arg("stOffset").toInt();

  if (server.hasArg("endTrig")) endTriggerType = server.arg("endTrig").toInt();
  if (server.hasArg("fEnd")) {
    String t = server.arg("fEnd");
    fixedEndHour = t.substring(0, 2).toInt();
    fixedEndMin = t.substring(3, 5).toInt();
  }
  if (server.hasArg("endOffset")) sunsetEndOffset = server.arg("endOffset").toInt();

  preferences.begin("timer-config", false);
  preferences.putInt("stTrig", startTriggerType);
  preferences.putInt("fStartH", fixedStartHour);
  preferences.putInt("fStartM", fixedStartMin);
  preferences.putInt("stOffset", sunsetStartOffset);

  preferences.putInt("endTrig", endTriggerType);
  preferences.putInt("fEndH", fixedEndHour);
  preferences.putInt("fEndM", fixedEndMin);
  preferences.putInt("endOffset", sunsetEndOffset);

  for (int i = 0; i < 7; i++) {
    d1_en[i] = server.hasArg("d1en" + String(i));
    preferences.putBool(("d1en" + String(i)).c_str(), d1_en[i]);
    if (server.hasArg("d1on" + String(i))) {
      String t = server.arg("d1on" + String(i));
      d1_on_h[i] = t.substring(0, 2).toInt(); d1_on_m[i] = t.substring(3, 5).toInt();
      preferences.putInt(("d1onh" + String(i)).c_str(), d1_on_h[i]);
      preferences.putInt(("d1onm" + String(i)).c_str(), d1_on_m[i]);
    }
    if (server.hasArg("d1off" + String(i))) {
      String t = server.arg("d1off" + String(i));
      d1_off_h[i] = t.substring(0, 2).toInt(); d1_off_m[i] = t.substring(3, 5).toInt();
      preferences.putInt(("d1offh" + String(i)).c_str(), d1_off_h[i]);
      preferences.putInt(("d1offm" + String(i)).c_str(), d1_off_m[i]);
    }

    d2_en[i] = server.hasArg("d2en" + String(i));
    preferences.putBool(("d2en" + String(i)).c_str(), d2_en[i]);
    if (server.hasArg("d2on" + String(i))) {
      String t = server.arg("d2on" + String(i));
      d2_on_h[i] = t.substring(0, 2).toInt(); d2_on_m[i] = t.substring(3, 5).toInt();
      preferences.putInt(("d2onh" + String(i)).c_str(), d2_on_h[i]);
      preferences.putInt(("d2onm" + String(i)).c_str(), d2_on_m[i]);
    }
    if (server.hasArg("d2off" + String(i))) {
      String t = server.arg("d2off" + String(i));
      d2_off_h[i] = t.substring(0, 2).toInt(); d2_off_m[i] = t.substring(3, 5).toInt();
      preferences.putInt(("d2offh" + String(i)).c_str(), d2_off_h[i]);
      preferences.putInt(("d2offm" + String(i)).c_str(), d2_off_m[i]);
    }
  }
  preferences.end();
  sendRedirectResponse("/schedule");
}

void handleSaveSettings() {
  if (!checkAuth()) return server.requestAuthentication();
  if (server.hasArg("opMode")) opMode = server.arg("opMode").toInt();
  if (server.hasArg("dstRule")) dstRule = server.arg("dstRule").toInt();
  if (server.hasArg("invRelay")) invertRelay = server.arg("invRelay").toInt();
  if (server.hasArg("lat")) storedLat = server.arg("lat").toDouble();
  if (server.hasArg("lon")) storedLon = server.arg("lon").toDouble();
  if (server.hasArg("tz")) tzOffset = server.arg("tz").toInt();

  preferences.begin("timer-config", false);
  preferences.putInt("opMode", opMode);
  preferences.putInt("dstRule", dstRule);
  preferences.putInt("invRelay", invertRelay);
  preferences.putDouble("lat", storedLat);
  preferences.putDouble("lon", storedLon);
  preferences.putInt("tz", tzOffset);
  preferences.end();

  sendRedirectResponse("/settings");
}

void handleSaveTime() {
  if (!checkAuth()) return server.requestAuthentication();
  if (server.hasArg("newdt")) {
    String dt = server.arg("newdt");
    if (dt.length() >= 16) {
      int y = dt.substring(0, 4).toInt();
      int m = dt.substring(5, 7).toInt();
      int d = dt.substring(8, 10).toInt();
      int h = dt.substring(11, 13).toInt();
      int min = dt.substring(14, 16).toInt();
      if (y >= 2026) rtc.adjust(DateTime(y, m, d, h, min, 0));
    }
  }
  sendRedirectResponse("/time");
}
