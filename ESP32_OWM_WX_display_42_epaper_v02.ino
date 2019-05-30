/* ESP32 Weather Display using an EPD 4.2" Display, obtains data from Open Weather Map, decodes the weather data and then displays it.
  ####################################################################################################################################
  This software, the ideas and concepts is Copyright (c) David Bird 2019. All rights to this software are reserved.

  Any redistribution or reproduction of any part or all of the contents in any form is prohibited other than the following:
  1. You may print or download to a local hard disk extracts for your personal and non-commercial use only.
  2. You may copy the content to individual third parties for their personal use, but only if you acknowledge the author David Bird as the source of the material.
  3. You may not, except with my express written permission, distribute or commercially exploit the content.
  4. You may not transmit it or store it in any other website or other form of electronic retrieval system for commercial purposes.

  The above copyright ('as annotated') notice and this permission notice shall be included in all copies or substantial portions of the Software and where the
  software use is visible to an end-user.

  THE SOFTWARE IS PROVIDED "AS IS" FOR PRIVATE USE ONLY, IT IS NOT FOR COMMERCIAL USE IN WHOLE OR PART OR CONCEPT. FOR PERSONAL USE IT IS SUPPLIED WITHOUT WARRANTY
  OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  See more at http://www.dsbird.org.uk
*/
#include "owm_credentials.h"  // See 'owm_credentials' tab and enter your OWM API key and set the Wifi SSID and PASSWORD
#include "Wx_Icons.h"          // Weather Icons
#include <ArduinoJson.h>       // https://github.com/bblanchon/ArduinoJson
#include <WiFi.h>              // Built-in
#include "time.h"              // Built-in
#include <SPI.h>               // Built-in 
#define ENABLE_GxEPD2_display 0
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include "epaper_fonts.h"

#define SCREEN_WIDTH  400.0    // Set for landscape mode, don't remove the decimal place!
#define SCREEN_HEIGHT 300.0    // Not used but included for completeness

enum alignment {LEFT, RIGHT, CENTER};

// pins_arduino.h, e.g. LOLIN32 LITE
static const uint8_t EPD_BUSY = 4;
static const uint8_t EPD_SS   = 5;
static const uint8_t EPD_RST  = 16;
static const uint8_t EPD_DC   = 17;
static const uint8_t EPD_SCK  = 18;
static const uint8_t EPD_MISO = 19; // Master-In Slave-Out not used, as no data from display
static const uint8_t EPD_MOSI = 23;

GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(GxEPD2_420(/*CS=5*/ SS, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4));

//################  VERSION  ##########################
String version = "02";       // Version of this program
//################ VARIABLES ###########################

const unsigned long UpdateInterval = (30L * 60L - 3) * 1000000L; // Delay between updates, in milliseconds, WU allows 500 requests per-day maximum, set to every 15-mins or more
bool LargeIcon   =  true;
bool SmallIcon   =  false;
#define Large  10
#define Small  4
String time_str, Day_time_str, rxtext; // strings to hold time and received weather data;
int    wifisection, displaysection, MoonDay, MoonMonth, MoonYear;
int    Sunrise, Sunset, rssi;

//################ PROGRAM VARIABLES and OBJECTS ################

typedef struct { // For current Day and Day 1, 2, 3, etc
  float    lat;
  float    lon;
  String   Dt;
  String   Period;
  float    Temperature;
  float    Humidity;
  String   Icon;
  float    High;
  float    Low;
  float    Rainfall;
  float    Snowfall;
  float    Pressure;
  int      Cloudcover;
  int      Visibility;
  String   Trend;
  float    Winddir;
  float    Windspeed;
  String   Main0;
  String   Forecast0;
  String   Forecast1;
  String   Forecast2;
  String   Description;
  String   Time;
  int      Sunrise;
  int      Sunset;
  String   Country;
} Forecast_record_type;

#define max_readings 24

Forecast_record_type  WxConditions[1];
Forecast_record_type  WxForecast[max_readings];

#define autoscale_on  true
#define autoscale_off false
#define barchart_on   true
#define barchart_off  false

float pressure_readings[max_readings]    = {0};
float temperature_readings[max_readings] = {0};
float rain_readings[max_readings]        = {0};

WiFiClient client; // wifi client object

//#########################################################################################
void setup() {
  Serial.begin(115200);
  display.init(115200);
  display.setRotation(1);
  display.setTextSize(0);
  display.setFont(&DejaVu_Sans_Bold_11);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  StartWiFi();
  SetupTime();
  bool Received_WxData_OK = false;
  Received_WxData_OK = obtain_wx_data("weather");
  Received_WxData_OK = obtain_wx_data("forecast");
  // Now only refresh the screen if all the data was received OK, otherwise wait until the next timed check otherwise wait until the next timed check
  if (Received_WxData_OK) {
    StopWiFi(); // Reduces power consumption
    display.setRotation(0);
    display.fillScreen(GxEPD_WHITE);
    DisplayWeatherInfo();
    DisplaySystemStatus(293, 238);
    display.display(false); // full update
    delay(2000);
    begin_sleep();
  }
  Serial.println(F("Starting deep-sleep period..."));
}
//#########################################################################################
void loop() { // this will never run!
}
//#########################################################################################
void begin_sleep() {
  display.powerOff();
  esp_sleep_enable_timer_wakeup(UpdateInterval);
  esp_deep_sleep_start(); // Sleep for e.g. 30 minutes
}
//#########################################################################################
void DisplayWeatherInfo() {                                // 4.2" e-paper display is 400x300 resolution
  DisplayHeadingSection();                                 // Top line of the display
  DisplayTempHumiSection(0, 15);                           // Current temperature with Max/Min and Humidity
  DisplayWxPerson(145, 15, WxConditions[0].Icon);          // Weather person depiction of weather
  DisplayWxIcon(276, 15, WxConditions[0].Icon, LargeIcon); // Weather icon
  DisplayMainWeatherSection(0, 148);                       // Weather forecast text
  DisplayForecastSection(131, 172);                        // 3hr interval forecast boxes
  DisplayAstronomySection(131, 174);                       // Astronomy section Sun rise/set, Moon phase and Moon icon
  DisplayWindSection(50, 220, WxConditions[0].Winddir, WxConditions[0].Windspeed, 50); // Wind direction info
}
//#########################################################################################
void DisplayTempHumiSection(int x, int y) {
  display.drawRect(x, y, 144, 130, GxEPD_BLACK);
  display.setFont(&DSEG7_Classic_Bold_21);
  display.setTextSize(2);
  if (WxConditions[0].Temperature < 0) drawString(x-6, y + 65, "-", LEFT);           // Show temperature sign to compensate for non-proportional font spacing
  drawString(x+16, y + 25, String(abs(WxConditions[0].Temperature), 1) + "'", LEFT); // Show current Temperature
  display.setTextSize(1);
  drawString(x+127, y + 45, (Units == "M" ? "C" : "F"), LEFT); // Add-in smaller Temperature unit
  drawString(x+25,  y + 89, String(WxConditions[0].High, 0) + "'/" + String(WxConditions[0].Low, 0) + "'", LEFT);   // Show forecast high and Low, in the font ' is a °
  display.setFont(&DejaVu_Sans_Bold_11);
  drawString(x+75, y + 115, String(WxConditions[0].Humidity,0)+"% RH",CENTER);
}
//#########################################################################################
void DisplayHeadingSection() {
  drawString(SCREEN_WIDTH / 2, 2, City, CENTER);
  drawString(SCREEN_WIDTH, 0, Day_time_str, RIGHT);
  drawString(2, -30, time_str, LEFT);
  display.drawLine(0, 15, SCREEN_WIDTH, 15, GxEPD_BLACK);
}
//#########################################################################################
void DisplayMainWeatherSection(int x, int y) {
  display.drawRect(x, y - 4, SCREEN_WIDTH, 27, GxEPD_BLACK);
  String Wx_Description = WxConditions[0].Forecast0;
  if (WxConditions[0].Forecast1 != "") Wx_Description += " & " +  WxConditions[0].Forecast1;
  if (WxConditions[0].Forecast2 != "" && WxConditions[0].Forecast1 != WxConditions[0].Forecast2) Wx_Description += " & " +  WxConditions[0].Forecast2;
  display.setFont(&FreeMonoBold12pt7b);
  drawStringMaxWidth(x+2, y - 5, 28, TitleCase(Wx_Description), LEFT); // 28 character screen width at this font size
  display.setFont(&DejaVu_Sans_Bold_11);
}
//#########################################################################################
String TitleCase(String text) {
  if (text.length() > 0) {
    String temp_text = text.substring(0, 1);
    temp_text.toUpperCase();
    return temp_text + text.substring(1); // Title-case the string
  }
  return "";
}
//#########################################################################################
void DisplayForecastSection(int x, int y) {
  int offset = 54;
  DisplayForecastWeather(x + offset*0, y, offset, 0);
  DisplayForecastWeather(x + offset*1, y, offset, 1);
  DisplayForecastWeather(x + offset*2, y, offset, 2);
  DisplayForecastWeather(x + offset*3, y, offset, 3);
  DisplayForecastWeather(x + offset*4, y, offset, 4);
  for (int r = 1; r <= max_readings; r++) {
    if (Units == "I") pressure_readings[r] = WxForecast[r].Pressure * 0.02953;
    else              pressure_readings[r] = WxForecast[r].Pressure;
    temperature_readings[r] = WxForecast[r].Temperature;
    if (Units == "I") rain_readings[r]     = WxForecast[r].Rainfall * 0.0393701;
    else              rain_readings[r]     = WxForecast[r].Rainfall;
  }
}
//#########################################################################################
void DisplayForecastWeather(int x, int y, int offset, int index) {
  display.drawRect(x, y, offset-1, 65, GxEPD_BLACK);
  display.drawLine(x, y + 13, x + offset-2, y + 13, GxEPD_BLACK);
  DisplayWxIcon(x + offset/2+1, y + 35, WxForecast[index].Icon, SmallIcon);
  drawString(x + offset/2, y + 3, String(WxForecast[index].Period.substring(11, 16)), CENTER);
  drawString(x + offset/2, y + 50, String(WxForecast[index].High, 0) + "/" + String(WxForecast[index].Low, 0), CENTER);//+ "*", LEFT); if you want the ° symbol in this font
}
//#########################################################################################
void DisplayWindSection(int x, int y, float angle, float windspeed, int Cradius) {
  int offset = 15;
  arrow(x + offset, y + offset, Cradius - 11, angle, 15, 22); // Show wind direction on outer circle of width and length
  display.setTextSize(0);
  display.drawRect(x - Cradius, y - Cradius, 130, 130, GxEPD_BLACK);
  int dxo, dyo, dxi, dyi;
  display.drawCircle(x + offset, y + offset, Cradius, GxEPD_BLACK); // Draw compass circle
  display.drawCircle(x + offset, y + offset, Cradius + 1, GxEPD_BLACK); // Draw compass circle
  display.drawCircle(x + offset, y + offset, Cradius * 0.7, GxEPD_BLACK); // Draw compass inner circle
  for (float a = 0; a < 360; a = a + 22.5) {
    dxo = Cradius * cos((a - 90) * PI / 180);
    dyo = Cradius * sin((a - 90) * PI / 180);
    if (a == 45)  drawString(dxo + x + 10 + offset, dyo + y - 10 + offset, "NE", CENTER);
    if (a == 135) drawString(dxo + x + 5 + offset, dyo + y + 5 + offset, "SE", CENTER);
    if (a == 225) drawString(dxo + x - 10 + offset, dyo + y + offset,   "SW", CENTER);
    if (a == 315) drawString(dxo + x - 10 + offset, dyo + y - 10 + offset, "NW", CENTER);
    dxi = dxo * 0.9;
    dyi = dyo * 0.9;
    display.drawLine(dxo + x + offset, dyo + y + offset, dxi + x + offset, dyi + y + offset, GxEPD_BLACK);
    dxo = dxo * 0.7;
    dyo = dyo * 0.7;
    dxi = dxo * 0.9;
    dyi = dyo * 0.9;
    display.drawLine(dxo + x + offset, dyo + y + offset, dxi + x + offset, dyi + y + offset, GxEPD_BLACK);
  }
  drawString(x + offset, y - Cradius - 11 + offset, "N", CENTER);
  drawString(x + offset, y + 3 + offset + Cradius, "S", CENTER);
  drawString(x - Cradius - 8 + offset, y - 5 + offset, "W", CENTER);
  drawString(x + Cradius + offset + 7, y - 3 + offset, "E", CENTER);
  drawString(x + offset, y - 23 + offset, WindDegToDirection(angle), CENTER);
  drawString(x + offset, y + 12 + offset, String(angle, 0) + "°", CENTER);
  drawString(x + offset, y - 5  + offset, String(windspeed, 1) + (Units == "M" ? " m/s" : " mph"), CENTER);
}
//#########################################################################################
String WindDegToDirection(float winddirection) {
  if (winddirection >= 348.75 || winddirection < 11.25)  return "N";
  if (winddirection >=  11.25 && winddirection < 33.75)  return "NNE";
  if (winddirection >=  33.75 && winddirection < 56.25)  return "NE";
  if (winddirection >=  56.25 && winddirection < 78.75)  return "ENE";
  if (winddirection >=  78.75 && winddirection < 101.25) return "E";
  if (winddirection >= 101.25 && winddirection < 123.75) return "ESE";
  if (winddirection >= 123.75 && winddirection < 146.25) return "SE";
  if (winddirection >= 146.25 && winddirection < 168.75) return "SSE";
  if (winddirection >= 168.75 && winddirection < 191.25) return "S";
  if (winddirection >= 191.25 && winddirection < 213.75) return "SSW";
  if (winddirection >= 213.75 && winddirection < 236.25) return "SW";
  if (winddirection >= 236.25 && winddirection < 258.75) return "WSW";
  if (winddirection >= 258.75 && winddirection < 281.25) return "W";
  if (winddirection >= 281.25 && winddirection < 303.75) return "WNW";
  if (winddirection >= 303.75 && winddirection < 326.25) return "NW";
  if (winddirection >= 326.25 && winddirection < 348.75) return "NNW";
  return "?";
}
//#########################################################################################
void DrawPressureAndTrend(int x, int y, float pressure, String slope) {
  display.setFont(&DSEG7_Classic_Bold_21);
  if (Units == "I") drawString(x - 35, y - 95, String(pressure, 2), LEFT);
  else drawString(x - 35, y - 95, String(pressure, 0), LEFT);
  display.setFont(&DejaVu_Sans_Bold_11);
  drawString(x + 40, y - 90, String((Units == "M" ? "hPa" : "in ") + slope), LEFT);
}
//#########################################################################################
void DisplayRain(int x, int y) {
  if (WxForecast[1].Rainfall > 0) drawString(x, y, String(WxForecast[1].Rainfall, 3) + (Units == "M" ? "mm" : "in") + " Rain", LEFT); // Only display rainfall if > 0
}
//#########################################################################################
void DisplayAstronomySection(int x, int y) {
  display.drawRect(x, y + 64, 161, 62, GxEPD_BLACK);
  drawString(x + 4,  y + 67,  "Sun Rise/Set", LEFT);
  drawString(x + 20, y + 82,  ConvertUnixTime(WxConditions[0].Sunrise).substring(0, 5), LEFT);
  drawString(x + 20, y + 96,  ConvertUnixTime(WxConditions[0].Sunset).substring(0, 5), LEFT);
  drawString(x + 4,  y + 109, MoonPhase(MoonDay, MoonMonth, MoonYear, Hemisphere), LEFT);
  DrawMoon(x + 95,   y + 52,  MoonDay, MoonMonth, MoonYear, Hemisphere);
}
//#########################################################################################
void DrawMoon(int x, int y, int dd, int mm, int yy, String hemisphere) {
  int diameter = 38;
  float Xpos, Ypos, Rpos, Xpos1, Xpos2, mage;
  for (Ypos = 0; Ypos <= 45; Ypos++) {
    Xpos = sqrt(45 * 45 - Ypos * Ypos);
    // Draw dark part of moon
    double pB1x = (90   - Xpos) / 90 * diameter + x;
    double pB1y = (Ypos + 90) / 90   * diameter + y;
    double pB2x = (Xpos + 90) / 90   * diameter + x;
    double pB2y = (Ypos + 90) / 90   * diameter + y;
    double pB3x = (90   - Xpos) / 90 * diameter + x;
    double pB3y = (90   - Ypos) / 90 * diameter + y;
    double pB4x = (Xpos + 90) / 90   * diameter + x;
    double pB4y = (90   - Ypos) / 90 * diameter + y;
    display.drawLine(pB1x, pB1y, pB2x, pB2y, GxEPD_BLACK);
    display.drawLine(pB3x, pB3y, pB4x, pB4y, GxEPD_BLACK);
    // Determine the edges of the lighted part of the moon
    int j = JulianDate(dd, mm, yy);
    //Calculate the approximate phase of the moon
    double Phase = (j + 4.867) / 29.53059;
    Phase = Phase - (int)Phase;
    // Calculate moon age if required
    if (Phase < 0.5) mage = Phase * 29.53059 + 29.53059 / 2; else mage = Phase * 29.53059 - 29.53059 / 2; // Moon's age in days
    if (hemisphere == "south") Phase = 1 - Phase;
    Rpos = 2 * Xpos;
    if (Phase < 0.5) {
      Xpos1 = - Xpos;
      Xpos2 = (Rpos - 2 * Phase * Rpos - Xpos);
    }
    else {
      Xpos1 = Xpos;
      Xpos2 = (Xpos - 2 * Phase * Rpos + Rpos);
    }
    // Draw light part of moon
    double pW1x = (Xpos1 + 90) / 90 * diameter + x;
    double pW1y = (90 - Ypos)  / 90 * diameter + y;
    double pW2x = (Xpos2 + 90) / 90 * diameter + x;
    double pW2y = (90 - Ypos)  / 90 * diameter + y;
    double pW3x = (Xpos1 + 90) / 90 * diameter + x;
    double pW3y = (Ypos + 90)  / 90 * diameter + y;
    double pW4x = (Xpos2 + 90) / 90 * diameter + x;
    double pW4y = (Ypos + 90)  / 90 * diameter + y;
    display.drawLine(pW1x, pW1y, pW2x, pW2y, GxEPD_WHITE);
    display.drawLine(pW3x, pW3y, pW4x, pW4y, GxEPD_WHITE);
  }
  display.drawCircle(x + diameter - 1, y + diameter, diameter / 2 + 1, GxEPD_BLACK);
}
//#########################################################################################
int JulianDate(int d, int m, int y) {
  int mm, yy, k1, k2, k3, j;
  yy = y - (int)((12 - m) / 10);
  mm = m + 9;
  if (mm >= 12) mm = mm - 12;
  k1 = (int)(365.25 * (yy + 4712));
  k2 = (int)(30.6001 * mm + 0.5);
  k3 = (int)((int)((yy / 100) + 49) * 0.75) - 38;
  // 'j' for dates in Julian calendar:
  j = k1 + k2 + d + 59 + 1;
  if (j > 2299160) j = j - k3; // 'j' is the Julian date at 12h UT (Universal Time) For Gregorian calendar:
  return j;
}
//#########################################################################################
String MoonPhase(int d, int m, int y, String hemisphere) {
  int c, e;
  double jd;
  int b;
  if (m < 3) {
    y--;
    m += 12;
  }
  ++m;
  c   = 365.25 * y;
  e   = 30.6 * m;
  jd  = c + e + d - 694039.09;           /* jd is total days elapsed */
  jd /= 29.53059;                        /* divide by the moon cycle (29.53 days) */
  b   = jd;                              /* int(jd) -> b, take integer part of jd */
  jd -= b;                               /* subtract integer part to leave fractional part of original jd */
  b   = jd * 8 + 0.5;                    /* scale fraction from 0-8 and round by adding 0.5 */
  b   = b & 7;                           /* 0 and 8 are the same phase so modulo 8 for 0 */
  if (hemisphere == "south") b = 7 - b;
  if (b == 0) return "New";              // New; 0% illuminated
  if (b == 1) return "Waxing crescent";  // Waxing crescent; 25% illuminated
  if (b == 2) return "First quarter";    // First quarter; 50% illuminated
  if (b == 3) return "Waxing gibbous";   // Waxing gibbous; 75% illuminated
  if (b == 4) return "Full";             // Full; 100% illuminated
  if (b == 5) return "Waning gibbous";   // Waning gibbous; 75% illuminated
  if (b == 6) return "Third quarter";    // Last quarter; 50% illuminated
  if (b == 7) return "Waning crescent";  // Waning crescent; 25% illuminated
  return "";
}
//#########################################################################################
void arrow(int x, int y, int asize, float aangle, int pwidth, int plength) {
  float dx = (asize - 10) * cos((aangle - 90) * PI / 180) + x; // calculate X position
  float dy = (asize - 10) * sin((aangle - 90) * PI / 180) + y; // calculate Y position
  float x1 = 0;         float y1 = plength;
  float x2 = pwidth / 2;  float y2 = pwidth / 2;
  float x3 = -pwidth / 2; float y3 = pwidth / 2;
  float angle = aangle * PI / 180 - 135;
  float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
  float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
  float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
  float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
  float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
  float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
  display.fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, GxEPD_BLACK);
}
//#########################################################################################
void DisplayWxIcon(int x, int y, String IconName, bool LargeSize) {
  Serial.println(IconName);
  if (LargeSize) {
    display.drawRect(x, y, 124, 130, GxEPD_BLACK);
    DrawPressureAndTrend(x + 45, y + 100, WxConditions[0].Pressure, WxConditions[0].Trend);
    DisplayRain(x + 12, y + 115);
    x = x + 65;
    y = y + 65;
  }
  if      (IconName == "01d" || IconName == "01n")  Sunny(x, y,       LargeSize, IconName);
  else if (IconName == "02d" || IconName == "02n")  MostlySunny(x, y, LargeSize, IconName);
  else if (IconName == "03d" || IconName == "03n")  Cloudy(x, y,      LargeSize, IconName);
  else if (IconName == "04d" || IconName == "04n")  MostlySunny(x, y, LargeSize, IconName);
  else if (IconName == "09d" || IconName == "09n")  ChanceRain(x, y,  LargeSize, IconName);
  else if (IconName == "10d" || IconName == "10n")  Rain(x, y,        LargeSize, IconName);
  else if (IconName == "11d" || IconName == "11n")  Tstorms(x, y,     LargeSize, IconName);
  else if (IconName == "13d" || IconName == "13n")  Snow(x, y,        LargeSize, IconName);
  else if (IconName == "50d")                       Haze(x, y,        LargeSize, IconName);
  else if (IconName == "50n")                       Fog(x, y,         LargeSize, IconName);
  else                                              Nodata(x, y,      LargeSize);
}
//#########################################################################################
bool obtain_wx_data(String RequestType) {
  rxtext = "";
  String units = (Units == "M" ? "metric" : "imperial");
  client.stop(); // close connection before sending a new request
  if (client.connect(server, 80)) { // if the connection succeeds
    Serial.println("connecting...");
    // send the HTTP PUT request:
    if (RequestType == "weather")
      client.println("GET /data/2.5/" + RequestType + "?q=" + City + "," + Country + "&APPID=" + apikey + "&mode=json&units=" + units + "&lang=" + Language + " HTTP/1.1");
    else
      client.println("GET /data/2.5/" + RequestType + "?q=" + City + "," + Country + "&APPID=" + apikey + "&mode=json&units=" + units + "&lang=" + Language + "&cnt=24 HTTP/1.1");
    client.println("Host: api.openweathermap.org");
    client.println("User-Agent: ESP OWM Receiver/1.1");
    client.println("Connection: close");
    client.println();
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout !");
        client.stop();
        return false;
      }
    }
    char c = 0;
    bool startJson = false;
    int jsonend = 0;
    while (client.available()) {
      c = client.read();
      // JSON formats contain an equal number of open and close curly brackets, so check that JSON is received correctly by counting open and close brackets
      if (c == '{') {
        startJson = true; // set true to indicate JSON message has started
        jsonend++;
      }
      if (c == '}') {
        jsonend--;
      }
      if (startJson == true) {
        rxtext += c;  // Add in the received character
      }
      // if jsonend = 0 then we have have received equal number of curly braces
      if (jsonend == 0 && startJson == true) {
        Serial.println("Received OK...");
        Serial.println(rxtext);
        if (!DecodeWeather(rxtext, RequestType)) return false;
        client.stop();
        return true;
      }
    }
  }
  else {
    // if no connction was made:
    Serial.println("connection failed");
    return false;
  }
  rxtext = "";
  return true;
}
//#########################################################################################
// Problems with stucturing JSON decodes, see here: https://arduinojson.org/assistant/
bool DecodeWeather(String json, String Type) {
  Serial.print(F("Creating object...and "));
  // allocate the JsonDocument
  DynamicJsonDocument doc(20 * 1024);
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, json);
  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }
  // convert it to a JsonObject
  JsonObject root = doc.as<JsonObject>();
  Serial.println(" Decoding " + Type + " data");
  if (Type == "weather") {
    // All Serial.println statements are for diagnostic purposes and not required, remove if not needed
    WxConditions[0].lon         = root["coord"]["lon"];
    WxConditions[0].lat         = root["coord"]["lat"];
    WxConditions[0].Main0       = root["weather"][0]["main"].as<char*>();         Serial.println(WxConditions[0].Main0);
    WxConditions[0].Forecast0   = root["weather"][0]["description"].as<char*>();  Serial.println(WxConditions[0].Forecast0);
    WxConditions[0].Icon        = root["weather"][0]["icon"].as<char*>();         Serial.println(WxConditions[0].Icon);
    WxConditions[0].Forecast1   = root["weather"][1]["main"].as<char*>();         Serial.println(WxConditions[0].Forecast1);
    WxConditions[0].Forecast2   = root["weather"][2]["main"].as<char*>();         Serial.println(WxConditions[0].Forecast2);
    WxConditions[0].Temperature = root["main"]["temp"];                           Serial.println(WxConditions[0].Temperature);
    WxConditions[0].Pressure    = root["main"]["pressure"];                       Serial.println(WxConditions[0].Pressure);
    WxConditions[0].Humidity    = root["main"]["humidity"];                       Serial.println(WxConditions[0].Humidity);
    WxConditions[0].Low         = root["main"]["temp_min"];                       Serial.println(WxConditions[0].Low);
    WxConditions[0].High        = root["main"]["temp_max"];                       Serial.println(WxConditions[0].High);
    WxConditions[0].Windspeed   = root["wind"]["speed"];                          Serial.println(WxConditions[0].Windspeed);
    WxConditions[0].Winddir     = root["wind"]["deg"];                            Serial.println(WxConditions[0].Winddir);
    WxConditions[0].Cloudcover  = root["clouds"]["all"];                          Serial.println(WxConditions[0].Cloudcover); // in % of cloud cover
    WxConditions[0].Visibility  = root["visibility"];                             Serial.println(WxConditions[0].Visibility); // in metres
    WxConditions[0].Country     = root["sys"]["country"].as<char*>();             Serial.println(WxConditions[0].Country);
    WxConditions[0].Sunrise     = root["sys"]["sunrise"];                         Serial.println(WxConditions[0].Sunrise);
    WxConditions[0].Sunset      = root["sys"]["sunset"];                          Serial.println(WxConditions[0].Sunset);
  }
  if (Type == "forecast") {
    //Serial.println(json);
    JsonArray list              = root["list"];
    Serial.print("\nReceiving Forecast period - "); //------------------------------------------------
    for (byte r = 0; r < max_readings; r++) {
      Serial.println("\nPeriod-" + String(r) + "--------------");
      WxForecast[r].Dt                = list[r]["dt"].as<char*>();
      WxForecast[r].Temperature       = list[r]["main"]["temp"];                          Serial.println(WxForecast[r].Temperature);
      WxForecast[r].Low               = list[r]["main"]["temp_min"];                      Serial.println(WxForecast[r].Low);
      WxForecast[r].High              = list[r]["main"]["temp_max"];                      Serial.println(WxForecast[r].High);
      WxForecast[r].Pressure          = list[r]["main"]["pressure"];                      Serial.println(WxForecast[r].Pressure);
      WxForecast[r].Humidity          = list[r]["main"]["humidity"];                      Serial.println(WxForecast[r].Humidity);
      WxForecast[r].Forecast0         = list[r]["weather"][0]["main"].as<char*>();        Serial.println(WxForecast[r].Forecast0);
      WxForecast[r].Forecast0         = list[r]["weather"][1]["main"].as<char*>();        Serial.println(WxForecast[r].Forecast1);
      WxForecast[r].Forecast0         = list[r]["weather"][2]["main"].as<char*>();        Serial.println(WxForecast[r].Forecast2);
      WxForecast[r].Description       = list[r]["weather"][0]["description"].as<char*>(); Serial.println(WxForecast[r].Description);
      WxForecast[r].Icon              = list[r]["weather"][0]["icon"].as<char*>();        Serial.println(WxForecast[r].Icon);
      WxForecast[r].Cloudcover        = list[r]["clouds"]["all"];                         Serial.println(WxForecast[0].Cloudcover); // in % of cloud cover
      WxForecast[r].Windspeed         = list[r]["wind"]["speed"];                         Serial.println(WxForecast[r].Windspeed);
      WxForecast[r].Winddir           = list[r]["wind"]["deg"];                           Serial.println(WxForecast[r].Winddir);
      WxForecast[r].Rainfall          = list[r]["rain"]["3h"];                            Serial.println(WxForecast[r].Rainfall);
      WxForecast[r].Snowfall          = list[r]["snow"]["3h"];                            Serial.println(WxForecast[r].Rainfall);
      WxForecast[r].Period            = list[r]["dt_txt"].as<char*>();                    Serial.println(WxForecast[r].Period);
    }
    //------------------------------------------
    float pressure_trend = WxForecast[0].Pressure - WxForecast[1].Pressure; // Measure pressure slope between ~now and later
    pressure_trend = ((int)(pressure_trend * 10)) / 10.0; // Remove any small variations less than 0.1
    WxConditions[0].Trend = "0";
    if (pressure_trend > 0)  WxConditions[0].Trend = "+";
    if (pressure_trend < 0)  WxConditions[0].Trend = "-";
    if (pressure_trend == 0) WxConditions[0].Trend = "0";
    if (Units == "I") Convert_Readings_to_Imperial();
  }
  return true;
}
//#########################################################################################
void Convert_Readings_to_Imperial() {
  WxConditions[0].Pressure    = WxConditions[0].Pressure * 0.02953;  // hPa to ins
  WxForecast[1].Rainfall      = WxForecast[1].Rainfall  * 0.0393701; // mm to inches of rainfall
}
//#########################################################################################
int StartWiFi() {
  int connAttempts = 0;
  Serial.print(F("\r\nConnecting to: ")); Serial.println(String(ssid));
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED ) {
    delay(500); Serial.print(".");
    if (connAttempts > 20) {
      WiFi.disconnect();
      begin_sleep();
    }
    connAttempts++;
  }
  Serial.println("WiFi connected at: " + String(WiFi.localIP()));
  rssi = WiFi.RSSI(); // Record signal strength
  return true;
}
//#########################################################################################
void StopWiFi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  wifisection    = millis() - wifisection;
}
//#########################################################################################
void SetupTime() {
  configTime(0, 0, "0.uk.pool.ntp.org", "time.nist.gov");
  setenv("TZ", Timezone, 1);
  delay(500);
  UpdateLocalTime();
}
//#########################################################################################
void UpdateLocalTime() {
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println(F("Failed to obtain time"));
  }
  //See http://www.cplusplus.com/reference/ctime/strftime/
  //Serial.println(&timeinfo, "%a %b %d %Y   %H:%M:%S");     // Displays: Saturday, June 24 2017 14:05:49
  Serial.println(&timeinfo, "%H:%M:%S");                     // Displays: 14:05:49
  char output[30], day_output[30];
  if (Units == "M") {
    strftime(day_output, 30, "%a  %d-%b-%y", &timeinfo);     // Displays: Sat 24-Jun-17
    strftime(output, 30, "(@ %H:%M:%S )", &timeinfo);        // Creates: '@ 14:05:49'
  }
  else {
    strftime(day_output, 30, "%a  %b-%d-%y", &timeinfo);     // Creates: Sat Jun-24-17
    strftime(output, 30, "(@ %r )", &timeinfo);              // Creates: '@ 2:05:49pm'
  }
  Day_time_str = day_output;
  time_str     = output;
}
//#########################################################################################
String ConvertUnixTime(int unix_time) {
  struct tm *now_tm;
  int hour, min, second, day, month, year, wday;
  // timeval tv = {unix_time,0};
  time_t tm = unix_time;
  now_tm = localtime(&tm);
  hour   = now_tm->tm_hour;
  min    = now_tm->tm_min;
  second = now_tm->tm_sec;
  wday   = now_tm->tm_wday;
  day    = now_tm->tm_mday;
  month  = now_tm->tm_mon + 1;
  year   = 1900 + now_tm->tm_year; // To get just YY information
  MoonDay   = day;
  MoonMonth = month;
  MoonYear  = year;
  if (Units == "M") {
    time_str =  (hour < 10 ? "0" + String(hour) : String(hour)) + ":" + (min < 10 ? "0" + String(min) : String(min)) + ":" + "  ";                     // HH:MM   05/07/17
    time_str += (day < 10 ? "0" + String(day) : String(day)) + "/" + (month < 10 ? "0" + String(month) : String(month)) + "/" + (year < 10 ? "0" + String(year) : String(year)); // HH:MM   05/07/17
  }
  else {
    String ampm = "am";
    if (hour > 11) ampm = "pm";
    hour = hour % 12; if (hour == 0) hour = 12;
    time_str =  (hour % 12 < 10 ? "0" + String(hour % 12) : String(hour % 12)) + ":" + (min < 10 ? "0" + String(min) : String(min)) + ampm + " ";      // HH:MMam 07/05/17
    time_str += (month < 10 ? "0" + String(month) : String(month)) + "/" + (day < 10 ? "0" + String(day) : String(day)) + "/" + "/" + (year < 10 ? "0" + String(year) : String(year)); // HH:MMpm 07/05/17
  }
  // Returns either '21:12  ' or ' 09:12pm' depending on Units
  //Serial.println(time_str);
  return time_str;
}
//#########################################################################################
// Symbols are drawn on a relative 10x10grid and 1 scale unit = 1 drawing unit
void addcloud(int x, int y, int scale, int linesize) {
  //Draw cloud outer
  display.fillCircle(x - scale * 3, y, scale, GxEPD_BLACK);                      // Left most circle
  display.fillCircle(x + scale * 3, y, scale, GxEPD_BLACK);                      // Right most circle
  display.fillCircle(x - scale, y - scale, scale * 1.4, GxEPD_BLACK);            // left middle upper circle
  display.fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75, GxEPD_BLACK); // Right middle upper circle
  display.fillRect(x - scale * 3 - 1, y - scale, scale * 6, scale * 2 + 1, GxEPD_BLACK); // Upper and lower lines
  //Clear cloud inner
  display.fillCircle(x - scale * 3, y, scale - linesize, GxEPD_WHITE);           // Clear left most circle
  display.fillCircle(x + scale * 3, y, scale - linesize, GxEPD_WHITE);           // Clear right most circle
  display.fillCircle(x - scale, y - scale, scale * 1.4 - linesize, GxEPD_WHITE); // left middle upper circle
  display.fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75 - linesize, GxEPD_WHITE); // Right middle upper circle
  display.fillRect(x - scale * 3 + 2, y - scale + linesize - 1, scale * 5.9, scale * 2 - linesize * 2 + 2, GxEPD_WHITE); // Upper and lower lines
}
//#########################################################################################
void addrain(int x, int y, int scale) {
  y = y + scale / 2;
  for (int i = 0; i < 6; i++) {
    display.drawLine(x - scale * 4 + scale * i * 1.3 + 0, y + scale * 1.9, x - scale * 3.5 + scale * i * 1.3 + 0, y + scale, GxEPD_BLACK);
    if (scale != Small) {
      display.drawLine(x - scale * 4 + scale * i * 1.3 + 1, y + scale * 1.9, x - scale * 3.5 + scale * i * 1.3 + 1, y + scale, GxEPD_BLACK);
      display.drawLine(x - scale * 4 + scale * i * 1.3 + 2, y + scale * 1.9, x - scale * 3.5 + scale * i * 1.3 + 2, y + scale, GxEPD_BLACK);
    }
  }
}
//#########################################################################################
void addsnow(int x, int y, int scale) {
  int dxo, dyo, dxi, dyi;
  for (int flakes = 0; flakes < 5; flakes++) {
    for (int i = 0; i < 360; i = i + 45) {
      dxo = 0.5 * scale * cos((i - 90) * 3.14 / 180); dxi = dxo * 0.1;
      dyo = 0.5 * scale * sin((i - 90) * 3.14 / 180); dyi = dyo * 0.1;
      display.drawLine(dxo + x + 0 + flakes * 1.5 * scale - scale * 3, dyo + y + scale * 2, dxi + x + 0 + flakes * 1.5 * scale - scale * 3, dyi + y + scale * 2, GxEPD_BLACK);
    }
  }
}
//#########################################################################################
void addtstorm(int x, int y, int scale) {
  y = y + scale / 2;
  for (int i = 0; i < 5; i++) {
    display.drawLine(x - scale * 4 + scale * i * 1.5 + 0, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 0, y + scale, GxEPD_BLACK);
    if (scale != Small) {
      display.drawLine(x - scale * 4 + scale * i * 1.5 + 1, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 1, y + scale, GxEPD_BLACK);
      display.drawLine(x - scale * 4 + scale * i * 1.5 + 2, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 2, y + scale, GxEPD_BLACK);
    }
    display.drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 0, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 0, GxEPD_BLACK);
    if (scale != Small) {
      display.drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 1, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 1, GxEPD_BLACK);
      display.drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 2, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 2, GxEPD_BLACK);
    }
    display.drawLine(x - scale * 3.5 + scale * i * 1.4 + 0, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5, GxEPD_BLACK);
    if (scale != Small) {
      display.drawLine(x - scale * 3.5 + scale * i * 1.4 + 1, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 1, y + scale * 1.5, GxEPD_BLACK);
      display.drawLine(x - scale * 3.5 + scale * i * 1.4 + 2, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 2, y + scale * 1.5, GxEPD_BLACK);
    }
  }
}
//#########################################################################################
void addsun(int x, int y, int scale) {
  int linesize = 3;
  if (scale == Small) linesize = 1;
  int dxo, dyo, dxi, dyi;
  display.fillCircle(x, y, scale, GxEPD_BLACK);
  display.fillCircle(x, y, scale - linesize, GxEPD_WHITE);
  for (float i = 0; i < 360; i = i + 45) {
    dxo = 2.2 * scale * cos((i - 90) * 3.14 / 180); dxi = dxo * 0.6;
    dyo = 2.2 * scale * sin((i - 90) * 3.14 / 180); dyi = dyo * 0.6;
    if (i == 0   || i == 180) {
      display.drawLine(dxo + x - 1, dyo + y, dxi + x - 1, dyi + y, GxEPD_BLACK);
      if (scale != Small) {
        display.drawLine(dxo + x + 0, dyo + y, dxi + x + 0, dyi + y, GxEPD_BLACK);
        display.drawLine(dxo + x + 1, dyo + y, dxi + x + 1, dyi + y, GxEPD_BLACK);
      }
    }
    if (i == 90  || i == 270) {
      display.drawLine(dxo + x, dyo + y - 1, dxi + x, dyi + y - 1, GxEPD_BLACK);
      if (scale != Small) {
        display.drawLine(dxo + x, dyo + y + 0, dxi + x, dyi + y + 0, GxEPD_BLACK);
        display.drawLine(dxo + x, dyo + y + 1, dxi + x, dyi + y + 1, GxEPD_BLACK);
      }
    }
    if (i == 45  || i == 135 || i == 225 || i == 315) {
      display.drawLine(dxo + x - 1, dyo + y, dxi + x - 1, dyi + y, GxEPD_BLACK);
      if (scale != Small) {
        display.drawLine(dxo + x + 0, dyo + y, dxi + x + 0, dyi + y, GxEPD_BLACK);
        display.drawLine(dxo + x + 1, dyo + y, dxi + x + 1, dyi + y, GxEPD_BLACK);
      }
    }
  }
}
//#########################################################################################
void addfog(int x, int y, int scale, int linesize) {
  if (scale == Small) y -= 10;
  if (scale == Small) linesize = 1;
  for (int i = 0; i < 6; i++) {
    display.fillRect(x - scale * 3, y + scale * 1.5, scale * 6, linesize, GxEPD_BLACK);
    display.fillRect(x - scale * 3, y + scale * 2.0, scale * 6, linesize, GxEPD_BLACK);
    display.fillRect(x - scale * 3, y + scale * 2.7, scale * 6, linesize, GxEPD_BLACK);
  }
}
//#########################################################################################
void MostlyCloudy(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 10;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addcloud(x, y+offset, scale, linesize);
  addsun(x - scale * 1.8, y - scale * 1.8 + offset, scale);
  addcloud(x, y+offset, scale, linesize);
}
//#########################################################################################
void MostlySunny(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 17;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addcloud(x, y+offset, scale, linesize);
  addsun(x - scale * 1.8, y - scale * 1.8+offset, scale);
}
//#########################################################################################
void Rain(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 12;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addcloud(x, y+offset, scale, linesize);
  addrain(x, y+offset, scale);
}
//#########################################################################################
void Cloudy(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 12;
  }
  int linesize = 3;
  if (scale == Small) {
    if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
    linesize = 1;
    addcloud(x, y+offset, scale, linesize);
  }
  else {
    if (IconName.endsWith("n")) addmoon(x, y +offset, scale);
    addcloud(x + 30, y - 20+offset, 4, linesize); // Cloud top right
    addcloud(x - 20, y - 10+offset, 6, linesize); // Cloud top left
    addcloud(x, y+offset+15, scale, linesize);    // Main cloud
  }
}
//#########################################################################################
void Sunny(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 10;
  }
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  scale = scale * 1.5;
  addsun(x, y+offset, scale);
}
//#########################################################################################
void ExpectRain(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 12;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addsun(x - scale * 1.8, y - scale * 1.8+offset, scale);
  addcloud(x, y+offset, scale, linesize);
  addrain(x, y+offset, scale);
}
//#########################################################################################
void ChanceRain(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 12;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addsun(x - scale * 1.8, y - scale * 1.8+offset, scale);
  addcloud(x, y+offset, scale, linesize);
  addrain(x, y+offset, scale);
}
//#########################################################################################
void Tstorms(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 12;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addcloud(x, y+offset, scale, linesize);
  addtstorm(x, y+offset, scale);
}
//#########################################################################################
void Snow(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 12;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addcloud(x, y+offset, scale, linesize);
  addsnow(x, y+offset, scale);
}
//#########################################################################################
void Fog(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 12;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addcloud(x, y+offset, scale, linesize);
  addfog(x, y+offset, scale, linesize);
}
//#########################################################################################
void Haze(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 7;
  }
  int linesize = 3;
  if (scale == Small) linesize = 1;
  if (IconName.endsWith("n")) addmoon(x, y+offset, scale);
  addsun(x, y+offset, scale * 1.4);
  addfog(x, y+offset, scale * 1.4, linesize);
}
//#########################################################################################
void addmoon (int x, int y, int scale) {
  if (scale == Large) {
    display.fillCircle(x - 37, y - 33, scale, GxEPD_BLACK);
    display.fillCircle(x - 27, y - 33, scale * 1.6, GxEPD_WHITE);
  }
  else
  {
    display.fillCircle(x - 20, y - 15, scale, GxEPD_BLACK);
    display.fillCircle(x - 15, y - 15, scale * 1.6, GxEPD_WHITE);
  }
}
//#########################################################################################
void Nodata(int x, int y, bool LargeSize) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 7;
  }
  if (scale == Large)  display.setFont(&FreeMonoBold12pt7b); else display.setFont(&DejaVu_Sans_Bold_11);
  drawString(x-20, y - 10+offset, "N/A", LEFT);
}
//#########################################################################################
void DisplaySystemStatus(int x, int y) {
  display.drawRect(x, y, 107, 62, GxEPD_BLACK);
  DisplayBattery(x + 60, y);
  DisplayRSSI(x+5, y + 45);
}
//#########################################################################################
void DisplayBattery(int x, int y) {
  uint8_t percentage = 100;
  float voltage = analogRead(35) / 4096.0 * 7.084;            // Lolin D32 voltage divider is Vbat---100K---ADC---100K---Gnd, ADC range is 3096, Max. 3.5v So 3095 = 3.5*2 = 7v max
  Serial.println(voltage);                                    // Devices vary in their internal impedance that loads the the lower 100K, so adjust 7.084 for ultimate accuracy
  if (voltage > 1) {                                          // If any voltage detected
    if (voltage > 4.19) percentage = 100;
    else if (voltage < 3.50) percentage = 0;
    else percentage = (voltage - 3.50) * 100 / (4.20 - 3.50); // 4.2v is the chosen upper limit and 3.5v the lower limit of the range
    drawString(x - 55, y + 7, String(voltage, 1) + "v", LEFT);
    drawString(x + 5, y + 7, String((int)percentage) + "%", LEFT);
    display.drawRect(x - 22, y + 7, 20, 10, GxEPD_BLACK);     // Draw battery pack
    display.fillRect(x - 3, y + 9, 3, 5, GxEPD_BLACK);
    display.fillRect(x - 20, y + 9, 16 * percentage / 100.0, 6, GxEPD_BLACK);
  }
}
//#########################################################################################
void DisplayRSSI(int x, int y) {
  int WIFIsignallevel = 0;
  int xpos = 1;
  for (int _rssi = -100; _rssi <= rssi; _rssi = _rssi + 20) {
    if (_rssi <= -20)  WIFIsignallevel = 20; //            <-20dbm displays 5-bars
    if (_rssi <= -40)  WIFIsignallevel = 16; //  -40dbm to  -21dbm displays 4-bars
    if (_rssi <= -60)  WIFIsignallevel = 12; //  -60dbm to  -41dbm displays 3-bars
    if (_rssi <= -80)  WIFIsignallevel = 8;  //  -80dbm to  -61dbm displays 2-bars
    if (_rssi <= -100) WIFIsignallevel = 4;  // -100dbm to  -81dbm displays 1-bar
    display.fillRect(x + xpos * 5 + 60, y - WIFIsignallevel, 4, WIFIsignallevel, GxEPD_BLACK);
    xpos++;
  }
  display.fillRect(x + 60, y - 1, 4, 1, GxEPD_BLACK);
  drawString(x, y - 9, String(rssi) + "dBm", LEFT);
}
//#########################################################################################
void drawString(int x, int y, String text, alignment align) {
  int16_t  x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
  uint16_t w, h;
  display.setTextWrap(false);
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (align == RIGHT)  x = x - w;
  if (align == CENTER) x = x - w / 2;
  display.setCursor(x, y + h);
  display.print(text);
}
//#########################################################################################
void drawStringMaxWidth(int x, int y, int text_width, String text, alignment align) {
  int16_t  x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
  uint16_t w, h;
  if (text.length() > text_width * 2) text = text.substring(0, text_width * 2); // Truncate if too long for 2 rows of text
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (align == RIGHT)  x = x - w;
  if (align == CENTER) x = x - w / 2;
  display.setCursor(x, y + h);
  display.println(text.substring(0, text_width));
  if (text.length() > text_width) {
    display.setCursor(x, y + h * 2);
    display.println(text.substring(text_width));
  }
}
//#########################################################################################
void DisplayWxPerson(int x, int y, String IconName) {
  display.drawRect(x, y, 130, 130, GxEPD_BLACK);
  // NOTE: Using 'drawInvertedBitmap' and not 'drawBitmap' so that images are WYSIWYG, otherwise all images need to be inverted
  if      (IconName == "01d" || IconName == "01n")  display.drawInvertedBitmap(x, y, WX_Sunny,       128, 128, GxEPD_BLACK);
  else if (IconName == "02d" || IconName == "02n")  display.drawInvertedBitmap(x, y, WX_MostlySunny, 128, 128, GxEPD_BLACK);
  else if (IconName == "03d" || IconName == "03n")  display.drawInvertedBitmap(x, y, WX_Cloudy,      128, 128, GxEPD_BLACK);
  else if (IconName == "04d" || IconName == "04n")  display.drawInvertedBitmap(x, y, WX_MostlySunny, 128, 128, GxEPD_BLACK);
  else if (IconName == "09d" || IconName == "09n")  display.drawInvertedBitmap(x, y, WX_ChanceRain,  128, 128, GxEPD_BLACK);
  else if (IconName == "10d" || IconName == "10n")  display.drawInvertedBitmap(x, y, WX_Rain,        128, 128, GxEPD_BLACK);
  else if (IconName == "11d" || IconName == "11n")  display.drawInvertedBitmap(x, y, WX_TStorms,     128, 128, GxEPD_BLACK);
  else if (IconName == "13d" || IconName == "13n")  display.drawInvertedBitmap(x, y, WX_Snow,        128, 128, GxEPD_BLACK);
  else if (IconName == "50d")                       display.drawInvertedBitmap(x, y, WX_Haze,        128, 128, GxEPD_BLACK);
  else if (IconName == "50n")                       display.drawInvertedBitmap(x, y, WX_Fog,         128, 128, GxEPD_BLACK);
  else                                              display.drawInvertedBitmap(x, y, WX_Nodata,      128, 128, GxEPD_BLACK);
}
