/*

A large six digit 7-segment LED clock, controlled by an ESP32, with extra blinkenlights.

The LED modules are quite large, 2.3" tall, and require high voltage to drive the many
leds in series for each segment (7-9v). To do this, six shift registers are used in 
series, which handles the switching. A custom PCB is used to mount each digit, designed 
around a TPIC6C596 IC, current-limiting resistors, plus headers for passing SPI, power 
and brightness PWM between the boards.

There are three voltage rails involved: 5v for the ESP32 and LED driver chips, ~7v DC to
power the 7-segment digits, and 3.3v for microcontroller logic. A separate custom PCB has 
been made to mount the ESP32, two DC/DC buck converters, a 74HTC125N IC for level-shifting,
and various switches and headers. A small SSD1306 OLED display is mounted to this board 
for showing system status, as well as a couple of power LEDs. The board also drives 25 WS2812b
LEDs, plus an I2C ambient light sensor. 

The clock connects via WiFiManager, which means it creates a hotspot on the first use for 
configuration (SSID "ESP32_CyberclockV1"). Once connected it will get the current time using NTP. 
The time zone offset is currently hard-coded.

*/

// Timezone config
/* 
  Enter your time zone (https://remotemonitoringsystems.ca/time-zone-abbreviations.php)
  See https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv for Timezone codes for your region
  based on https://github.com/SensorsIot/NTP-time-for-ESP8266-and-ESP32/blob/master/NTP_Example/NTP_Example.ino
*/
const char* NTP_SERVER = "ch.pool.ntp.org";
const char* TZ_INFO    = "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";  // Switzerland


#include <Arduino.h>
#include <WiFiManager.h>
#include <SPI.h>
#include "time.h"
#include <Wire.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <BH1750.h>

// Wifi
WiFiManager wm;   // looking for credentials? don't need em! ... google "ESP32 WiFiManager"

// PINs
#define PIN_CLK   18    // Clock on the TPIC6C596
#define PIN_LATCH 19    // Latch 
#define PIN_DATA  23    // Serial data
#define PIN_BRIGHTNESS 14 // PWM Enable on TPIC6C596
#define LIGHT_SENSOR 33  // light-dependent resistor

// Misc Config
#define DISPLAY_SIZE 6  // Number of digits
bool debug = true;

// OLED display
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Custom Code
#include "pixel_trail.h"
#include "led_digits.h"

// Time 
tm timeinfo;
time_t now;
int hour = 0;
int minute = 0;
int second = 0;

// NeoPixels
const uint16_t PixelCount = 25; // make sure to set this to the number of pixels in your strip
const uint16_t PixelPin = 5;  // make sure to set this to the correct pin, ignored for Esp8266 
MyNeoPixelBus strip(PixelCount, PixelPin);
NeoGamma<NeoGammaTableMethod> colorGamma; // for any fade animations, best to correct gamma
const uint16_t AnimCount = 10; // max number of NeoPixelAnimator objects
NeoPixelAnimator animations(AnimCount, NEO_CENTISECONDS); // NeoPixel animation management object

// NeoPixelAnimator channels
#define ANI_CIRCLE 0
#define ANI_DOT0 1
#define ANI_DOT1 2
#define ANI_DOT2 3
#define ANI_DOT3 4
#define ANI_LIGHT_LEVEL 5
#define ANI_OLED 6
#define ANI_CLOCK 7

// animation settings
#define MAX_TRAILS 3
const uint16_t TailLength = 4; // length of the tail, must be shorter than PixelCount
PixelTrail *pixel_trails[MAX_TRAILS];
float current_hue = 6000;  // tracking current cycle color (changes each minute)

// light levels
BH1750 lightMeter;
float lux = 0.0;
int level = 0;
int last_level = 0;
float brightness = 0.8;  // 0.0 .. 1.0

// Time, date, and tracking state
int t = 0;
int number = 0;
int animation=0;
String formattedDate;
String dayStamp;
long millis_offset=0;
int last_minute=0;

// Days of week. Day 1 = Sunday
String DoW[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// Months
String Months[] { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };


void InitTrails(){
  for (int i=0; i<MAX_TRAILS; i++){
    pixel_trails[i] = new PixelTrail(random(360) / 360.0f);
    pixel_trails[i]->randomize();
  }
}


String getFormattedDate(){
  char time_output[30];
  strftime(time_output, 30, "%a  %d-%m-%y %T", &timeinfo);
  return String(time_output);
}

String getFormattedTime(){
  char time_output[30];
  strftime(time_output, 30, "%H:%M:%S", &timeinfo);
  return String(time_output);
}

void SetRandomSeed()
{
    uint32_t seed;

    // random works best with a seed that can use 31 bits
    // analogRead on a unconnected pin tends toward less than four bits
    seed = analogRead(0);
    delay(1);

    for (int shifts = 3; shifts < 31; shifts += 3)
    {
        seed ^= analogRead(0) << shifts;
        delay(1);
    }

    // Serial.println(seed);
    randomSeed(seed);
}

void setDigitBrightness(float bright){
  if (second % 10 == 7 or second % 10 == 1){ bright -= 0.04; } // fix for power diff in digits
  int ledc_level = 1024 - (int)(NeoEase::QuadraticIn(bright*0.85+0.10)*1023);
  ledcWrite(1, ledc_level);
}

void LEDClockUpdate(const AnimationParam& param){
  if (animation > 0 and millis()%animation <= 10){
    showRandom();
    animation--;
  }
  if (param.state == AnimationState_Completed){
    animations.RestartAnimation(param.index);
  } else if (animation == 0){
    float bright = brightness;
    setDigitBrightness(brightness);
    showNumber(hour*10000+minute*100+second);
  }
}

void DrawTrailPixels(){
  for (int i=0; i<MAX_TRAILS; i++){
    pixel_trails[i]->draw(strip, colorGamma, brightness);
  }
}

void LoopAnimUpdate(const AnimationParam& param){
    // wait for this animation to complete,
    // we are using it as a timer of sorts
    strip.ClearTo(RgbColor(0,0,0), 0, strip.PixelCount()-5);
    DrawTrailPixels();
    if (param.state == AnimationState_Completed){
        animations.RestartAnimation(param.index);
        int index = random(MAX_TRAILS);
        pixel_trails[index]->randomize();
        pixel_trails[index]->hue = current_hue;
        // rotate the complete strip one pixel to the right on every update
        //strip.RotateRight(1, 0, strip.PixelCount()-5);
    }
    strip.Show();
}

void DotAnimUpdate(const AnimationParam& param){
  if (param.state == AnimationState_Completed){
      animations.RestartAnimation(param.index);
  }

  float dot_level = 0;
  if (param.progress <= 0.5){
    dot_level = NeoEase::QuadraticOut(param.progress);
  } else {
    dot_level = 1.0 - NeoEase::QuadraticIn(param.progress);
  }
  RgbColor color = HslColor(current_hue, 1.0f, dot_level * brightness);
  strip.SetPixelColor(strip.PixelCount() - 5 + param.index, colorGamma.Correct(color));
}

void AutomaticLightLevel(const AnimationParam& param){
  if (param.state == AnimationState_Completed){
    animations.RestartAnimation(param.index);
    lux = lightMeter.readLightLevel();
    last_level = level;
    level = constrain(int(lux*10), 1, 300); 
    level = last_level + (level - last_level)/20;  // slew changes, avoid jumps
    brightness = map(level, 1, 300, 100, 1000)/1000.0;

    if (debug && millis()%100 == 0){
      Serial.print("lux: ");
      Serial.print(lux);
      Serial.print("   brightness: ");
      Serial.println(brightness);
    }
  }
}

void OLEDAnimUpdate(const AnimationParam& param){
  if (param.state == AnimationState_Completed){
    animations.RestartAnimation(param.index);  
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);        // Draw white text
    display.setCursor(0,0);             // Start at top-left corner
    display.setTextSize(2);             // Normal 1:1 pixel scale

    display.print(getFormattedTime());
    display.print(".");
    display.println((millis() - millis_offset)/100 % 10);
    display.println(DoW[timeinfo.tm_wday]);
    
    String short_date = String(hour) + ":" + String(minute) + ":" + String(second);
    display.println(short_date);

    display.setTextSize(1); 
    display.print("IP:");
    display.println(WiFi.localIP());
    display.print("WiFi:");
    display.print(WiFi.RSSI());
    display.print("dB");

    display.print("  Lux:");
    display.printf( "%0.1f", lux);
    
    display.display();
  }
}

void log_status_info()
{
  Serial.print("ESP version: ");
  Serial.println(ESP.getChipRevision());
  Serial.print(" flash size: ");
  Serial.println(ESP.getFlashChipSize());
  Serial.print(" CPU MHz: ");
  Serial.println(ESP.getCpuFreqMHz());
  Serial.print(" SDK Version: ");
  Serial.println(ESP.getSdkVersion());
  Serial.print(" sketch size: ");
  Serial.println(ESP.getSketchSize());  
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(wm.getConfigPortalSSID());
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);  
  display.println("Please configure!");
  display.println("Connect to WiFi: ");
  display.println(wm.getConfigPortalSSID());
  display.display();
}

bool getNTPtime(int sec) {
  if (WiFi.isConnected()) {
    bool timeout = false;
    bool date_is_valid = false;
    long start = millis();

    Serial.println(" updating:");
    configTime(0, 0, NTP_SERVER);
    setenv("TZ", TZ_INFO, 1);

    do {
      timeout = (millis() - start) > (1000 * sec);
      time(&now);
      localtime_r(&now, &timeinfo);
      Serial.print(" . ");
      date_is_valid = timeinfo.tm_year > (2016 - 1900);
      delay(100);
      digitalWrite(PIN_LATCH, LOW);
      for (int n=0; n<6; n++){ postNumber(random(2)==1 ? '-' : '_', false); }
      digitalWrite(PIN_LATCH, HIGH);
    } while (!timeout && !date_is_valid);
    
    Serial.println("\nSystem time is now:");
    Serial.println(getFormattedDate());
    Serial.println(getFormattedTime());
    
    if (!date_is_valid){
      Serial.println("Error: Invalid date received!");
      Serial.println(timeinfo.tm_year);
      return false;  // the NTP call was not successful
    } else if (timeout) {
      Serial.println("Error: Timeout while trying to update the current time with NTP");
      return false;
    } else {
      Serial.println("[ok] time updated: ");
      return true;
    }
  } else {
    Serial.println("Error: Update time failed, no WiFi connection!");
    return false;
  }
}

void ConnectToWifi(){
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  wm.setAPCallback(configModeCallback);

  digitalWrite(PIN_LATCH, LOW);
  for (int n=0; n<6; n++){ postNumber('-',false); }
  digitalWrite(PIN_LATCH, HIGH);

  // wm.resetSettings();   // uncomment to force a reset
  bool wifi_connected = wm.autoConnect("ESP32_CyberclockV1");
  int t=0;
  if (wifi_connected){
    Serial.println();
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println("db");

    delay(1000);

    Serial.println("getting current time...");
    
    // timeClient.begin();
    // timeClient.update();
    if (getNTPtime(10)) {  // wait up to 10sec to sync
      Serial.println("Time sync complete");
    } else {
      Serial.println("Error: NTP time update failed!");
    }
  } else {
    Serial.println("ERROR: WiFi connect failure");
    display.clearDisplay();
    display.println("WIFI connect failed");
    display.display();
  }
}

void setup()
{
  Wire.begin();
  Serial.begin(115200);
  delay(1000);
  Serial.println("Large Digit Driver Example");
  
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(LIGHT_SENSOR, INPUT);

  ledcAttachPin(PIN_BRIGHTNESS, 1);
  ledcSetup(1, 1000, 10);
  ledcWrite(1, 512); // half-brightness for startup

  digitalWrite(PIN_CLK, LOW);
  digitalWrite(PIN_DATA, LOW);
  digitalWrite(PIN_LATCH, LOW);

  strip.Begin();
  strip.Show();
  SetRandomSeed();
  lightMeter.begin();

  // we use the index 0 animation to time how often we rotate all the pixels
  animations.StartAnimation(ANI_DOT0, 100, DotAnimUpdate);
  animations.StartAnimation(ANI_DOT1, 200, DotAnimUpdate);
  animations.StartAnimation(ANI_DOT2, 300, DotAnimUpdate);
  animations.StartAnimation(ANI_DOT3, 400, DotAnimUpdate);
  animations.StartAnimation(ANI_CIRCLE, 6000, LoopAnimUpdate);
  animations.StartAnimation(ANI_LIGHT_LEVEL, 10, AutomaticLightLevel);
  animations.StartAnimation(ANI_OLED, 10, OLEDAnimUpdate);
  animations.StartAnimation(ANI_CLOCK, 50, LEDClockUpdate);
  
  // init display and show wifi is connecting
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,0);             // Start at top-left corner
  display.setTextSize(2);             // Normal 1:1 pixel scale
  display.println("Connecting...");
  display.display();

  log_status_info();
  ConnectToWifi();
  
  display.clearDisplay();
  display.display();

  InitTrails();
}

uint16_t pmax = INT16_MAX;
int p = pmax;
int dot_level = 0;

void loop() { 
  if (p++ > pmax){
    p=0;
  }

  // periodically update the current time
  if (p % 100 == 0){
    time(&now);
    localtime_r(&now, &timeinfo);
    hour = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    second = timeinfo.tm_sec;
    p=0;
  } 

  if (minute != last_minute){
    Serial.println(" -> new minute");        
    last_minute = minute;
    Serial.println(getFormattedTime());
    millis_offset = millis();  
    animation = 45;
    current_hue = random(360) / 360.0f;
  }

  animations.UpdateAnimations();

  // wait 1 millisecond
  for(int x = 0; x<1000; x++){ float y=sin(rand()); }

}

