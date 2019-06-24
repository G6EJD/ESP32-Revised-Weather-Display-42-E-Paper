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
#include "owm_credentials.h" // See 'owm_credentials' tab and enter your OWM API key and set the Wifi SSID and PASSWORD
#include "Wx_Icons.h"        // Weather Icons
#include <ArduinoJson.h>     // https://github.com/bblanchon/ArduinoJson
#include <WiFi.h>            // Built-in
#include <SPI.h>             // Built-in 
#include <HTTPClient.h>      // Built-in
#include "time.h"            // Built-in
#define  ENABLE_GxEPD2_display 0
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include "epaper_fonts.h"

#define SCREEN_WIDTH  400.0
#define SCREEN_HEIGHT 300.0

enum alignment {LEFT, RIGHT, CENTER};

// pins_arduino.h, e.g. LOLIN32 D32
static const uint8_t EPD_BUSY = 4;  //Pin-4  on Lolin D32
static const uint8_t EPD_SS   = 5;  //Pin-5  on Lolin D32
static const uint8_t EPD_RST  = 16; //Pin-16 on Lolin D32
static const uint8_t EPD_DC   = 17; //Pin-17 on Lolin D32
static const uint8_t EPD_SCK  = 18; //Pin-18 on Lolin D32
static const uint8_t EPD_MISO = 19; //Pin-19 on Lolin D32 Master-In Slave-Out not used, as no data from display
static const uint8_t EPD_MOSI = 23; //Pin-23 on Lolin D32

GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(GxEPD2_420(/*CS=5*/ SS, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4));

//################  VERSION  ##########################
String version = "v5";       // Version of this program
//################ VARIABLES ###########################

long    SleepDuration = 30; // Sleep time in minutes, aligned to the nearest minute boundary, so if 30 will always update at 00 or 30 past the hour
boolean LargeIcon =  true, SmallIcon =  false, RxWeather = false, RxForecast = false;
#define Large  10
#define Small  4
String  time_str, Day_time_str; // strings to hold time and date
int     wifisection;
int     rssi, StartTime = 0, CurrentHour = 0, CurrentMin = 0, CurrentSec = 0;;

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

#define max_readings 5

Forecast_record_type  WxConditions[1];
Forecast_record_type  WxForecast[max_readings];

#define autoscale_on  true
#define autoscale_off false
#define barchart_on   true
#define barchart_off  false

float pressure_readings[max_readings]    = {0};
float temperature_readings[max_readings] = {0};
float rain_readings[max_readings]        = {0};

int WakeupTime = 7;  // Don't wakeup until after 07:00 to save battery
int SleepTime  = 23; // Don't sleep  until after 00:00 to save battery

//#########################################################################################
void setup() {
  Serial.begin(115200);
  StartTime = millis();
  // Now only refresh the screen if all the data was received OK, otherwise wait until the next timed check otherwise wait until the next timed check
  if (StartWiFi() == WL_CONNECTED && SetupTime() == true) {
    if (CurrentHour >= WakeupTime && CurrentHour <= SleepTime) {
      InitialiseDisplay(); // Give it time to do this initialisation!
      byte Attempts = 1;
      WiFiClient client; // wifi client object
      while ((RxWeather == false || RxForecast == false) && Attempts <= 2) { // Try up-to twice for Weather and Forecast data
        if (RxWeather  == false) RxWeather  = obtain_wx_data(client, "weather");
        if (RxForecast == false) RxForecast = obtain_wx_data(client, "forecast");
        Attempts++;
      }
      // Refresh screen if data was received OK, otherwise wait until the next timed check
      if (RxWeather || RxForecast) {
        StopWiFi(); // Reduces power consumption
        DisplayWeatherInfo();
        display.display(false); // full update
      }
    }
  }
  BeginSleep();
}
//#########################################################################################
void loop() { // this will never run!
}
//#########################################################################################
void BeginSleep() {
  display.powerOff();
  long SleepTimer = (SleepDuration * 60 - ((CurrentMin % SleepDuration) * 60 + CurrentSec)); //Some ESP32 are too fast to maintain accurate time
  esp_sleep_enable_timer_wakeup(SleepTimer * 1000000LL);
#ifdef BUILTIN_LED
  pinMode(BUILTIN_LED, INPUT); // If it's On, turn it off and some boards use GPIO-5 for SPI-SS, which remains low after screen use
  digitalWrite(BUILTIN_LED, HIGH);
#endif
  Serial.println("Entering " + String(SleepTimer) + "-secs of sleep time");
  Serial.println("Awake for : " + String((millis() - StartTime) / 1000.0, 3) + "-secs");
  Serial.println("Starting deep-sleep period...");
  esp_deep_sleep_start();      // Sleep for e.g. 30 minutes
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
  DisplaySystemStatus(293, 238);
}
//#########################################################################################
void DisplayTempHumiSection(int x, int y) {
  display.drawRect(x, y, 144, 130, GxEPD_BLACK);
  display.setFont(&DSEG7_Classic_Bold_21);
  display.setTextSize(2);
  if (WxConditions[0].Temperature < 0) drawString(x - 10, y + 61, "-", LEFT); // Show temperature sign to compensate for non-proportional font spacing
  drawString(x + 12, y + 25, String(fabs(WxConditions[0].Temperature), 1), LEFT); // Show current Temperature without a '-' minus sign
  display.setTextSize(1);
  drawString(x + 117, y + 25, "'" + String((Units == "M" ? "C" : "F")), LEFT);    // Add-in ° symbol ' in this font plus units
  drawString(x + 25,  y + 89, String(WxConditions[0].High, 0) + "'/" + String(WxConditions[0].Low, 0) + "'", LEFT); // Show forecast high and Low, in the font ' is a °
  display.setFont(&DejaVu_Sans_Bold_11);
  drawString(x + 75, y + 115, String(WxConditions[0].Humidity, 0) + "% RH", CENTER);
  if (WxConditions[0].Cloudcover > 0) CloudCover(x + 60, y + 10, WxConditions[0].Cloudcover);
}
//#########################################################################################
void DisplayHeadingSection() {
  drawString(SCREEN_WIDTH / 2, 2, City, CENTER);
  drawString(SCREEN_WIDTH, 2, Day_time_str, RIGHT);
  drawString(2, 2, time_str, LEFT);
  drawString(115, 2, version, CENTER);
  display.drawLine(0, 15, SCREEN_WIDTH, 15, GxEPD_BLACK);
}
//#########################################################################################
void DisplayMainWeatherSection(int x, int y) {
  display.drawRect(x, y - 4, SCREEN_WIDTH, 27, GxEPD_BLACK);
  String Wx_Description = WxConditions[0].Forecast0;
  if (WxConditions[0].Forecast1 != "") Wx_Description += ", " + WxConditions[0].Forecast1;
  if (WxConditions[0].Forecast2 != "") Wx_Description += ", "  + WxConditions[0].Forecast2;
  if (!RxWeather)  Wx_Description += " ### Failed to receive Weather data ###";
  if (!RxForecast) Wx_Description += " ### Failed to receive Forecast data ###";
  display.setFont(&FreeMonoBold12pt7b);
  unsigned int MsgWidth = 28;
  if (Wx_Description.length() > MsgWidth) {
    display.setFont(&DejaVu_Sans_Bold_11); // Drop to smaller font to allow all weather description to be displayed
    MsgWidth = 52;
    y = y - 7;
  }
  drawStringMaxWidth(x + 3, y + 15, MsgWidth, TitleCase(Wx_Description), LEFT); // 28 character screen width at this font size
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
  DisplayForecastWeather(x + offset * 0, y, offset, 0);
  DisplayForecastWeather(x + offset * 1, y, offset, 1);
  DisplayForecastWeather(x + offset * 2, y, offset, 2);
  DisplayForecastWeather(x + offset * 3, y, offset, 3);
  DisplayForecastWeather(x + offset * 4, y, offset, 4);
  for (int r = 1; r <= max_readings; r++) {
    if (Units == "I") pressure_readings[r] = WxForecast[r].Pressure * 0.02953;
    else              pressure_readings[r] = WxForecast[r].Pressure;
    if (r <= max_readings) temperature_readings[r] = WxForecast[r].Temperature;
    if (Units == "I") rain_readings[r]     = WxForecast[r].Rainfall * 0.0393701;
    else              rain_readings[r]     = WxForecast[r].Rainfall;
  }
}
//#########################################################################################
void DisplayForecastWeather(int x, int y, int offset, int index) {
  display.drawRect(x, y, offset - 1, 65, GxEPD_BLACK);
  display.drawLine(x, y + 13, x + offset - 2, y + 13, GxEPD_BLACK);
  DisplayWxIcon(x + offset / 2 + 1, y + 35, WxForecast[index].Icon, SmallIcon);
  drawString(x + offset / 2, y + 3, String(WxForecast[index].Period.substring(11, 16)), CENTER);
  drawString(x + offset / 2, y + 50, String(WxForecast[index].High, 0) + "/" + String(WxForecast[index].Low, 0), CENTER); //+ "*", LEFT); if you want the ° symbol in this font
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
    if (a == 135) drawString(dxo + x + 5 + offset, dyo + y + 5 + offset,   "SE", CENTER);
    if (a == 225) drawString(dxo + x - 10 + offset, dyo + y + offset,    "SW", CENTER);
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
  drawString(x + offset, y - 5 + offset, String(windspeed, 1) + (Units == "M" ? " m/s" : " mph"), CENTER);
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
  if (Units == "I") drawString(x - 38, y - 95, String(pressure, 2), LEFT);
  else drawString(x - 38, y - 95, String(pressure, 0), LEFT);
  display.setFont(&DejaVu_Sans_Bold_11);
  drawString(x + 37, y - 90, String((Units == "M" ? "hPa" : "in ")), LEFT);
  if (slope == "0") display.drawInvertedBitmap(x + 62, y - 96, FL_Arrow, 18, 18, GxEPD_BLACK); // Steady
  if (slope == "-") display.drawInvertedBitmap(x + 62, y - 96, DN_Arrow, 18, 18, GxEPD_BLACK); // Falling
  if (slope == "+") display.drawInvertedBitmap(x + 62, y - 96, UP_Arrow, 18, 18, GxEPD_BLACK); // Rising
}
//#########################################################################################
void DisplayRain(int x, int y) {
  if (WxForecast[1].Rainfall > 0) drawString(x, y, String(WxForecast[1].Rainfall, (WxForecast[1].Rainfall > 0.5 ? 2 : 3)) + (Units == "M" ? "mm" : "in") + " Rain", CENTER); // Only display rainfall if > 0
}
//#########################################################################################
void DisplayAstronomySection(int x, int y) {
  display.drawRect(x, y + 64, 161, 62, GxEPD_BLACK);
  drawString(x + 4,  y + 67,  "Sun Rise/Set", LEFT);
  drawString(x + 20, y + 82,  ConvertUnixTime(WxConditions[0].Sunrise).substring(0, 5), LEFT);
  drawString(x + 20, y + 96,  ConvertUnixTime(WxConditions[0].Sunset).substring(0, 5), LEFT);
  time_t now = time(NULL);
  struct tm * now_utc  = gmtime(&now);
  const int day_utc = now_utc->tm_mday;
  const int month_utc = now_utc->tm_mon + 1;
  const int year_utc = now_utc->tm_year + 1900;
  drawString(x + 4,  y + 109, MoonPhase(day_utc, month_utc, year_utc, Hemisphere), LEFT);
  DrawMoon(x + 95,   y + 52, day_utc, month_utc, year_utc, Hemisphere);
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
double NormalizedMoonPhase(int d, int m, int y) {
  int j = JulianDate(d, m, y);
  //Calculate the approximate phase of the moon
  double Phase = (j + 4.867) / 29.53059;
  return (Phase - (int) Phase);
}
//#########################################################################################
void DrawMoon(int x, int y, int dd, int mm, int yy, String hemisphere) {
  const int diameter = 38;
  double Phase = NormalizedMoonPhase(dd, mm, yy);
  if (hemisphere == "south") Phase = 1 - Phase;
  // Draw dark part of moon
  display.fillCircle(x + diameter - 1, y + diameter, diameter / 2 + 1, GxEPD_BLACK);
  const int number_of_lines = 90;
  for (double Ypos = 0; Ypos <= number_of_lines / 2; Ypos++) {
    double Xpos = sqrt(number_of_lines / 2 * number_of_lines / 2 - Ypos * Ypos);
    // Determine the edges of the lighted part of the moon
    double Rpos = 2 * Xpos;
    double Xpos1, Xpos2;
    if (Phase < 0.5) {
      Xpos1 = -Xpos;
      Xpos2 = Rpos - 2 * Phase * Rpos - Xpos;
    }
    else {
      Xpos1 = Xpos;
      Xpos2 = Xpos - 2 * Phase * Rpos + Rpos;
    }
    // Draw light part of moon
    double pW1x = (Xpos1 + number_of_lines) / number_of_lines * diameter + x;
    double pW1y = (number_of_lines - Ypos)  / number_of_lines * diameter + y;
    double pW2x = (Xpos2 + number_of_lines) / number_of_lines * diameter + x;
    double pW2y = (number_of_lines - Ypos)  / number_of_lines * diameter + y;
    double pW3x = (Xpos1 + number_of_lines) / number_of_lines * diameter + x;
    double pW3y = (Ypos + number_of_lines)  / number_of_lines * diameter + y;
    double pW4x = (Xpos2 + number_of_lines) / number_of_lines * diameter + x;
    double pW4y = (Ypos + number_of_lines)  / number_of_lines * diameter + y;
    display.drawLine(pW1x, pW1y, pW2x, pW2y, GxEPD_WHITE);
    display.drawLine(pW3x, pW3y, pW4x, pW4y, GxEPD_WHITE);
  }
  display.drawCircle(x + diameter - 1, y + diameter, diameter / 2, GxEPD_BLACK);
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
  jd  = c + e + d - 694039.09;     /* jd is total days elapsed */
  jd /= 29.53059;                        /* divide by the moon cycle (29.53 days) */
  b   = jd;                              /* int(jd) -> b, take integer part of jd */
  jd -= b;                               /* subtract integer part to leave fractional part of original jd */
  b   = jd * 8 + 0.5;                /* scale fraction from 0-8 and round by adding 0.5 */
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
    DisplayRain(x + 60, y + 115);
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
bool obtain_wx_data(WiFiClient& client, const String& RequestType) {
  const String units = (Units == "M" ? "metric" : "imperial");
  client.stop(); // close connection before sending a new request
  HTTPClient http;
  String uri = "/data/2.5/" + RequestType + "?q=" + City + "," + Country + "&APPID=" + apikey + "&mode=json&units=" + units + "&lang=" + Language;
  if (RequestType != "weather")
  {
    uri += "&cnt=" + String(max_readings);
  }
  //http.begin(uri,test_root_ca); //HTTPS example connection
  http.begin(client, server, 80, uri);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    if (!DecodeWeather(http.getStream(), RequestType)) return false;
    client.stop();
    http.end();
    return true;
  }
  else
  {
    Serial.printf("connection failed, error: %s", http.errorToString(httpCode).c_str());
    client.stop();
    http.end();
    return false;
  }
  return true;
}
//#########################################################################################
// Problems with stucturing JSON decodes, see here: https://arduinojson.org/assistant/
bool DecodeWeather(WiFiClient& json, String Type) {
  Serial.print(F("Creating object...and "));
  DynamicJsonDocument doc(20 * 1024); // allocate the JsonDocument
  DeserializationError error = deserializeJson(doc, json);
  if (error) { // Test if parsing succeeds.// Deserialize the JSON document
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }
  JsonObject root = doc.as<JsonObject>(); // convert it to a JsonObject
  Serial.println(" Decoding " + Type + " data");
  if (Type == "weather") {
    // All Serial.println statements are for diagnostic purposes and not required, remove if not needed
    WxConditions[0].lon         = root["coord"]["lon"];
    WxConditions[0].lat         = root["coord"]["lat"];
    WxConditions[0].Main0       = root["weather"][0]["main"].as<char*>();         Serial.print("Main0: "); Serial.println(WxConditions[0].Main0);
    WxConditions[0].Forecast0   = root["weather"][0]["description"].as<char*>();  Serial.print("Fore0: "); Serial.println(WxConditions[0].Forecast0);
    WxConditions[0].Forecast1   = root["weather"][1]["main"].as<char*>();         Serial.print("Fore1: "); Serial.println(WxConditions[0].Forecast1);
    WxConditions[0].Forecast2   = root["weather"][2]["main"].as<char*>();         Serial.print("Fore2: "); Serial.println(WxConditions[0].Forecast2);
    WxConditions[0].Icon        = root["weather"][0]["icon"].as<char*>();         Serial.print("Icon : "); Serial.println(WxConditions[0].Icon);
    WxConditions[0].Temperature = root["main"]["temp"];                           Serial.print("Temp : "); Serial.println(WxConditions[0].Temperature);
    WxConditions[0].Pressure    = root["main"]["pressure"];                       Serial.print("Pres : "); Serial.println(WxConditions[0].Pressure);
    WxConditions[0].Humidity    = root["main"]["humidity"];                       Serial.print("Humi : "); Serial.println(WxConditions[0].Humidity);
    WxConditions[0].Low         = root["main"]["temp_min"];                       Serial.print("TLow : "); Serial.println(WxConditions[0].Low);
    WxConditions[0].High        = root["main"]["temp_max"];                       Serial.print("THigh: "); Serial.println(WxConditions[0].High);
    WxConditions[0].Windspeed   = root["wind"]["speed"];                          Serial.print("WSpd : "); Serial.println(WxConditions[0].Windspeed);
    WxConditions[0].Winddir     = root["wind"]["deg"];                            Serial.print("WDir : "); Serial.println(WxConditions[0].Winddir);
    WxConditions[0].Cloudcover  = root["clouds"]["all"];                          Serial.print("CCov : "); Serial.println(WxConditions[0].Cloudcover); // in % of cloud cover
    WxConditions[0].Visibility  = root["visibility"];                             Serial.print("Visi : "); Serial.println(WxConditions[0].Visibility); // in metres
    WxConditions[0].Country     = root["sys"]["country"].as<char*>();             Serial.print("Coun : "); Serial.println(WxConditions[0].Country);
    WxConditions[0].Sunrise     = root["sys"]["sunrise"];                         Serial.print("SunR : "); Serial.println(WxConditions[0].Sunrise);
    WxConditions[0].Sunset      = root["sys"]["sunset"];                          Serial.print("SunS : "); Serial.println(WxConditions[0].Sunset);
  }
  if (Type == "forecast") {
    //Serial.println(json);
    JsonArray list              = root["list"];
    Serial.print("\nReceiving Forecast period-"); //------------------------------------------------
    for (byte r = 0; r < max_readings; r++) {
      Serial.println("\nPeriod-" + String(r) + "--------------");
      WxForecast[r].Dt          = list[r]["dt"].as<char*>();
      WxForecast[r].Temperature = list[r]["main"]["temp"];                          Serial.print("Temp : "); Serial.println(WxForecast[r].Temperature);
      WxForecast[r].Low         = list[r]["main"]["temp_min"];                      Serial.print("TLow : "); Serial.println(WxForecast[r].Low);
      WxForecast[r].High        = list[r]["main"]["temp_max"];                      Serial.print("THig : "); Serial.println(WxForecast[r].High);
      WxForecast[r].Pressure    = list[r]["main"]["pressure"];                      Serial.print("Pres : "); Serial.println(WxForecast[r].Pressure);
      WxForecast[r].Humidity    = list[r]["main"]["humidity"];                      Serial.print("Humi : "); Serial.println(WxForecast[r].Humidity);
      WxForecast[r].Forecast0   = list[r]["weather"][0]["main"].as<char*>();        Serial.print("Fore0: "); Serial.println(WxForecast[r].Forecast0);
      WxForecast[r].Forecast0   = list[r]["weather"][1]["main"].as<char*>();        Serial.print("Fore1: "); Serial.println(WxForecast[r].Forecast1);
      WxForecast[r].Forecast0   = list[r]["weather"][2]["main"].as<char*>();        Serial.print("Fore2: "); Serial.println(WxForecast[r].Forecast2);
      WxForecast[r].Description = list[r]["weather"][0]["description"].as<char*>(); Serial.print("Desc : "); Serial.println(WxForecast[r].Description);
      WxForecast[r].Icon        = list[r]["weather"][0]["icon"].as<char*>();        Serial.print("Icon : "); Serial.println(WxForecast[r].Icon);
      WxForecast[r].Cloudcover  = list[r]["clouds"]["all"];                         Serial.print("CCov : "); Serial.println(WxForecast[0].Cloudcover); // in % of cloud cover
      WxForecast[r].Windspeed   = list[r]["wind"]["speed"];                         Serial.print("WSpd : "); Serial.println(WxForecast[r].Windspeed);
      WxForecast[r].Winddir     = list[r]["wind"]["deg"];                           Serial.print("WDir : "); Serial.println(WxForecast[r].Winddir);
      WxForecast[r].Rainfall    = list[r]["rain"]["3h"];                            Serial.print("Rain : "); Serial.println(WxForecast[r].Rainfall);
      WxForecast[r].Snowfall    = list[r]["snow"]["3h"];                            Serial.print("Snow : "); Serial.println(WxForecast[r].Snowfall);
      WxForecast[r].Period      = list[r]["dt_txt"].as<char*>();                    Serial.print("Peri : "); Serial.println(WxForecast[r].Period);
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
  WxConditions[0].Pressure = hPa_to_inHg(WxConditions[0].Pressure);
  WxForecast[1].Rainfall   = mm_to_inches(WxForecast[1].Rainfall);
  WxForecast[1].Snowfall   = mm_to_inches(WxForecast[1].Snowfall);
}
//#########################################################################################
float mm_to_inches(float value_mm)
{
  return 0.0393701 * value_mm;
}
//#########################################################################################

float hPa_to_inHg(float value_hPa)
{
  return 0.02953 * value_hPa;
}
//#########################################################################################
uint8_t StartWiFi() {
  Serial.print(F("\r\nConnecting to: ")); Serial.println(String(ssid));
  IPAddress dns(8, 8, 8, 8); // Google DNS
  WiFi.disconnect();
  WiFi.mode(WIFI_STA); // switch off AP
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  uint8_t connectionStatus;
  bool AttemptConnection = true;
  while (AttemptConnection) {
    connectionStatus = WiFi.status();
    if (millis() > start + 15000) { // Wait 15-secs maximum
      AttemptConnection = false;
    }
    if (connectionStatus == WL_CONNECTED || connectionStatus == WL_CONNECT_FAILED) {
      AttemptConnection = false;
    }
    delay(100);
  }
  if (connectionStatus == WL_CONNECTED) {
    rssi = WiFi.RSSI();
    Serial.println("WiFi connected at: " + WiFi.localIP().toString());
  }
  else Serial.println("WiFi connection *** FAILED ***");
  return connectionStatus;
}
//#########################################################################################
void StopWiFi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  wifisection    = millis() - wifisection;
}
//#########################################################################################
boolean SetupTime() {
  configTime(0, 0, "0.uk.pool.ntp.org", "time.nist.gov");
  setenv("TZ", Timezone, 1);
  delay(100);
  bool TimeStatus = UpdateLocalTime();
  return TimeStatus;
}
//#########################################################################################
boolean UpdateLocalTime() {
  struct tm timeinfo;
  char   output[30], day_output[30];
  while (!getLocalTime(&timeinfo, 5000)) { // Wait for up to 5-secs
    Serial.println(F("Failed to obtain time"));
    return false;
  }
  CurrentHour = timeinfo.tm_hour;
  CurrentMin  = timeinfo.tm_min;
  CurrentSec  = timeinfo.tm_sec;
  //See http://www.cplusplus.com/reference/ctime/strftime/
  //Serial.println(&timeinfo, "%H:%M:%S");                               // Displays: 14:05:49
  if (Units == "M") {
    strftime(day_output, 30, "%a  %d-%b-%y", &timeinfo);                 // Displays: Sat 24-Jun-17
    strftime(output, sizeof(output), "%H:%M:%S", &timeinfo);             // Creates: '14:05:49'
  }
  else {
    strftime(day_output, sizeof(day_output), "%a  %b-%d-%y", &timeinfo); // Creates: Sat Jun-24-17
    strftime(output, sizeof(output), "%r", &timeinfo);                   // Creates: '2:05:49pm'
  }
  Day_time_str = day_output;
  time_str     = output;
  return true;
}
//#########################################################################################
String ConvertUnixTime(int unix_time) {
  // Returns either '21:12  ' or ' 09:12pm' depending on Units mode
  time_t tm = unix_time;
  struct tm *now_tm = localtime(&tm);
  char output[40];
  if (Units == "M") {
    strftime(output, sizeof(output), "%H:%M %d/%m/%y", now_tm);
  }
  else {
    strftime(output, sizeof(output), "%I:%M%P %m/%d/%y", now_tm);
  }
  return output;
}
//#########################################################################################
// Symbols are drawn on a relative 10x10grid and 1 scale unit = 1 drawing unit
void addcloud(int x, int y, int scale, int linesize) {
  //Draw cloud outer
  display.fillCircle(x - scale * 3, y, scale, GxEPD_BLACK);                  // Left most circle
  display.fillCircle(x + scale * 3, y, scale, GxEPD_BLACK);                  // Right most circle
  display.fillCircle(x - scale, y - scale, scale * 1.4, GxEPD_BLACK);      // left middle upper circle
  display.fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75, GxEPD_BLACK); // Right middle upper circle
  display.fillRect(x - scale * 3 - 1, y - scale, scale * 6, scale * 2 + 1, GxEPD_BLACK); // Upper and lower lines
  //Clear cloud inner
  display.fillCircle(x - scale * 3, y, scale - linesize, GxEPD_WHITE);     // Clear left most circle
  display.fillCircle(x + scale * 3, y, scale - linesize, GxEPD_WHITE);     // Clear right most circle
  display.fillCircle(x - scale, y - scale, scale * 1.4 - linesize, GxEPD_WHITE); // left middle upper circle
  display.fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75 - linesize, GxEPD_WHITE); // Right middle upper circle
  display.fillRect(x - scale * 3 + 2, y - scale + linesize - 1, scale * 5.9, scale * 2 - linesize * 2 + 2, GxEPD_WHITE); // Upper and lower lines
}
//#########################################################################################
void addrain(int x, int y, int scale) {
  //arrow(int x, int y, int asize, float aangle, int pwidth, int plength) {
  for (int i = 0; i < 6; i++) {
    display.fillCircle(x - scale * 4 + scale * i * 1.3, y + scale * 1.9 + (scale == Small ? 3 : 0), scale / 3, GxEPD_BLACK);
    arrow(x - scale * 4 + scale * i * 1.3 + (scale == Small ? 6 : 4), y + scale * 1.6 + (scale == Small ? -3 : -1), scale / 6, 40, scale / 1.6, scale * 1.2);
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
void addsun(int x, int y, int scale, boolean IconSize) {
  int linesize = 3;
  if (IconSize == SmallIcon) linesize = 1;
  int dxo, dyo, dxi, dyi;
  display.fillCircle(x, y, scale, GxEPD_BLACK);
  display.fillCircle(x, y, scale - linesize, GxEPD_WHITE);
  for (float i = 0; i < 360; i = i + 45) {
    dxo = 2.2 * scale * cos((i - 90) * 3.14 / 180); dxi = dxo * 0.6;
    dyo = 2.2 * scale * sin((i - 90) * 3.14 / 180); dyi = dyo * 0.6;
    if (i == 0   || i == 180) {
      display.drawLine(dxo + x - 1, dyo + y, dxi + x - 1, dyi + y, GxEPD_BLACK);
      if (IconSize == LargeIcon) {
        display.drawLine(dxo + x + 0, dyo + y, dxi + x + 0, dyi + y, GxEPD_BLACK);
        display.drawLine(dxo + x + 1, dyo + y, dxi + x + 1, dyi + y, GxEPD_BLACK);
      }
    }
    if (i == 90  || i == 270) {
      display.drawLine(dxo + x, dyo + y - 1, dxi + x, dyi + y - 1, GxEPD_BLACK);
      if (IconSize == LargeIcon) {
        display.drawLine(dxo + x, dyo + y + 0, dxi + x, dyi + y + 0, GxEPD_BLACK);
        display.drawLine(dxo + x, dyo + y + 1, dxi + x, dyi + y + 1, GxEPD_BLACK);
      }
    }
    if (i == 45  || i == 135 || i == 225 || i == 315) {
      display.drawLine(dxo + x - 1, dyo + y, dxi + x - 1, dyi + y, GxEPD_BLACK);
      if (IconSize == LargeIcon) {
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
    display.fillRect(x - scale * 3, y + scale * 2.5, scale * 6, linesize, GxEPD_BLACK);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addcloud(x, y + offset, scale, linesize);
  addsun(x - scale * 1.8, y - scale * 1.8 + offset, scale, LargeSize);
  addcloud(x, y + offset, scale, linesize);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addcloud(x, y + offset, scale, linesize);
  addsun(x - scale * 1.8, y - scale * 1.8 + offset, scale, LargeSize);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addcloud(x, y + offset, scale, linesize);
  addrain(x, y + offset, scale);
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
    if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
    linesize = 1;
    addcloud(x, y + offset, scale, linesize);
  }
  else {
    if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
    addcloud(x + 30, y - 20 + offset, 4, linesize); // Cloud top right
    addcloud(x - 20, y - 10 + offset, 6, linesize); // Cloud top left
    addcloud(x, y + offset + 15, scale, linesize); // Main cloud
  }
}
//#########################################################################################
void Sunny(int x, int y, bool LargeSize, String IconName) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 10;
  }
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  scale = scale * 1.5;
  addsun(x, y + offset, scale, LargeSize);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addsun(x - scale * 1.8, y - scale * 1.8 + offset, scale, LargeSize);
  addcloud(x, y + offset, scale, linesize);
  addrain(x, y + offset, scale);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addsun(x - scale * 1.8, y - scale * 1.8 + offset, scale, LargeSize);
  addcloud(x, y + offset, scale, linesize);
  addrain(x, y + offset, scale);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addcloud(x, y + offset, scale, linesize);
  addtstorm(x, y + offset, scale);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addcloud(x, y + offset, scale, linesize);
  addsnow(x, y + offset, scale);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addcloud(x, y + offset, scale, linesize);
  addfog(x, y + offset, scale, linesize);
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
  if (IconName.endsWith("n")) addmoon(x, y + offset, scale);
  addsun(x, y + offset, scale * 1.4, LargeSize);
  addfog(x, y + offset, scale * 1.4, linesize);
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
void CloudCover(int x, int y, int CCover) {
  addcloud(x, y,     Small * 0.5,  1); // Main cloud
  addcloud(x + 5, y - 5, Small * 0.35, 1); // Cloud top right
  addcloud(x - 8, y - 5, Small * 0.35, 1); // Cloud top left
  drawString(x + 30, y - 5, String(CCover) + "%", CENTER);
}
//#########################################################################################
void Nodata(int x, int y, bool LargeSize) {
  int scale = Small, offset = 0;
  if (LargeSize) {
    scale = Large;
    offset = 7;
  }
  if (scale == Large)  display.setFont(&FreeMonoBold12pt7b); else display.setFont(&DejaVu_Sans_Bold_11);
  drawString(x - 20, y - 10 + offset, "N/A", LEFT);
}
//#########################################################################################
void DisplaySystemStatus(int x, int y) {
  display.drawRect(x, y, 107, 62, GxEPD_BLACK);
  DisplayBattery(x + 60, y);
  DisplayRSSI(x + 5, y + 45);
}
//#########################################################################################
void DisplayBattery(int x, int y) {
  uint8_t percentage = 100;
  float voltage = analogRead(35) / 4095.0 * 7.24;         // Lolin D32 voltage divider is Vbat---100K---ADC---100K---Gnd, ADC range is 4096, Max. 3.5v So 4095 = 3.5*2 = 7v max
  Serial.println(voltage);                                    // Devices vary in their internal impedance which loads the the lower 100K, so adjust 7.24 for ultimate accuracy
  if (voltage > 1) {                                          // If any voltage detected
    percentage = 2808.3808 * pow(voltage, 4) - 43560.9157 * pow(voltage, 3) + 252848.5888 * pow(voltage, 2) - 650767.4615 * voltage + 626532.5703;
    if (voltage > 4.20) percentage = 100;
    else if (voltage < 3.50) percentage = 0;
    drawString(x - 55, y + 7, String(voltage, 1) + "v", LEFT);
    drawString(x + 5, y + 7, String((int)percentage) + "%", LEFT);
    display.drawRect(x - 22, y + 7, 20, 10, GxEPD_BLACK); // Draw battery pack
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
void drawStringMaxWidth(int x, int y, unsigned int text_width, String text, alignment align) {
  int16_t  x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
  uint16_t w, h;
  if (text.length() > text_width * 2) text = text.substring(0, text_width * 2); // Truncate if too long for 2 rows of text
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (align == RIGHT)  x = x - w;
  if (align == CENTER) x = x - w / 2;
  display.setCursor(x, y);
  display.println(text.substring(0, text_width));
  if (text.length() > text_width) {
    display.setCursor(x, y + h);
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

void InitialiseDisplay() {
  display.init(115200);
  display.setRotation(1);
  display.setTextSize(0);
  display.setFont(&DejaVu_Sans_Bold_11);
  display.setTextColor(GxEPD_BLACK);
  display.setRotation(0);
  display.fillScreen(GxEPD_WHITE);
  display.setFullWindow();
}
