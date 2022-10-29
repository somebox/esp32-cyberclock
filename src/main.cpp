#include <Arduino.h>
#include <WiFiManager.h>
#include <SPI.h>
#include "time.h"
#include <Wire.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <BH1750.h>
#include "pixel_trail.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/*

A large four digit 7-segment LED clock, controlled by an ESP32, with extra blinkenlights.

The LED modules are quite large, 2.3" tall, and require high voltage to drive the many
leds in series for each segment (7-9v). To do this, four shift registers are used in 
series (TPIC6C596), which handles the switching.

As the ESP8266 is 3.3v logic, and the TPIC chip requires 5v logic, a level conversion is
required. A four-channel 75HTC125N chip does the job here.

There is also an SSD1306 OLED display, 25 WS2812b LEDs, plus an ambient light sensor attached. 

*/

WiFiManager wm;

#define PIN_CLK   18    // Clock on the TPIC6C596
#define PIN_LATCH 19    // Latch 
#define PIN_DATA  23    // Serial data
#define PIN_BRIGHTNESS 14 // PWM Enable on TPIC6C596
#define LIGHT_SENSOR 33  // light-dependent resistor

#define DISPLAY_SIZE 6  // Number of digits

// Time Sync
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 7200, 600000); // proto, host, timeOffset, updateInterval

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
const uint16_t TailLength = 4; // length of the tail, must be shorter than PixelCount
#define MAX_TRAILS 3
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
int hour, minute, second = 0;
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

int reverse(int n){
  int rev = 0;
  int i = DISPLAY_SIZE;
  while (i>0){
    int rem = n % 10;
    rev = rev*10 + rem;
    n /= 10;
    i--;
  }
  return rev;
}

//Given a number, or '-', shifts it out to the display
void postNumber(byte number, boolean decimal)
{
  //       ---   A
  //     /   /   F, B
  //     ---     G
  //   /   /     E, C
  //   ---  .    D, DP

  #define a  1<<0
  #define b  1<<6
  #define c  1<<5
  #define d  1<<4
  #define e  1<<3
  #define f  1<<1
  #define g  1<<2
  #define dp 1<<7

  byte segments = 0;

  switch (number)
  {
    case 1: segments = b | c; break;
    case 2: segments = a | b | d | e | g; break;
    case 3: segments = a | b | c | d | g; break;
    case 4: segments = f | g | b | c; break;
    case 5: segments = a | f | g | c | d; break;
    case 6: segments = a | f | g | e | c | d; break;
    case 7: segments = a | b | c; break;
    case 8: segments = a | b | c | d | e | f | g; break;
    case 9: segments = a | b | c | d | f | g; break;
    case 0: segments = a | b | c | d | e | f; break;
    case ' ': segments = 0; break;
    case 'c': segments = g | e | d; break;
    case '-': segments = g; break;
  }

  if (decimal) segments |= dp;

  shiftOut(PIN_DATA, PIN_CLK, MSBFIRST, segments);
  //Clock these bits out to the drivers
  // for (byte x = 0 ; x < 8 ; x++)
  // {
  //   digitalWrite(PIN_CLK, LOW);
  //   digitalWrite(PIN_DATA, segments & 1 << (7 - x));
  //   digitalWrite(PIN_CLK, HIGH); //Data transfers to the register on the rising edge of SRCK
  // }
}

//Takes a number and displays it with leading zeroes
void showNumber(float value)
{
  int number = reverse(abs(value)); 

  digitalWrite(PIN_LATCH, LOW);

  // update all digits of the display
  for (int x = 0 ; x < DISPLAY_SIZE ; x++)
  {
    byte remainder = number % 10;
    postNumber(remainder, false);
    number /= 10;
  }
  //Latch the current segment data
  digitalWrite(PIN_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
}

void showRandom(){
  digitalWrite(PIN_LATCH, LOW);
  shiftOut(PIN_DATA, PIN_CLK, MSBFIRST, random(128));
  //Latch the current segment data
  digitalWrite(PIN_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
}

void blankDisplay(){
  digitalWrite(PIN_LATCH, LOW);
  for (int i=0; i<4; i++){
      shiftOut(PIN_DATA, PIN_CLK, MSBFIRST, 0);
  }
  digitalWrite(PIN_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
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

void LEDClockUpdate(const AnimationParam& param){
  if (animation > 0 and millis()%animation <= 10){
    showRandom();
    animation--;
  }
  if (param.state == AnimationState_Completed){
    animations.RestartAnimation(param.index);
    minute = timeClient.getMinutes();
    hour = timeClient.getHours();
  } else if (animation == 0){
    second = timeClient.getSeconds();
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
    level = constrain(int(lux*10), 1, 500); 
    level = last_level + (level - last_level)/20;  // slew changes, avoid jumps
    brightness = map(level, 1, 500, 100, 1000)/1000.0;
    Serial.print("lux: ");
    Serial.print(lux);
    Serial.print("   brightness: ");
    Serial.println(brightness);
    int ledc_level = 1024 - (int)(NeoEase::QuadraticIn(brightness)*1023);
    ledcWrite(1, ledc_level);
  }
}

void OLEDAnimUpdate(const AnimationParam& param){
  if (param.state == AnimationState_Completed){
    animations.RestartAnimation(param.index);  
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);        // Draw white text
    display.setCursor(0,0);             // Start at top-left corner
    display.setTextSize(2);             // Normal 1:1 pixel scale

    display.print(timeClient.getFormattedTime());
    display.print(".");
    display.println((millis() - millis_offset)/100 % 10);
    display.println(DoW[timeClient.getDay()]);
    
    int splitT = formattedDate.indexOf("T");
    dayStamp = formattedDate.substring(0, splitT);
    display.println(dayStamp);
    display.setTextSize(1); 
    
    display.print("IP:");
    display.println(WiFi.localIP());
    display.print("WiFi:");
    display.print(WiFi.RSSI());
    display.print("dB");

    display.print("  Lux:");
    display.printf( "%0.1f",lux);
    
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
    timeClient.begin();
    timeClient.update();

    Serial.println(timeClient.getFormattedTime());
    // WiFi.mode(WiFiMode::WIFI_OFF);
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
  ledcSetup(1, 5000, 10);  

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
  animations.StartAnimation(ANI_LIGHT_LEVEL, 30, AutomaticLightLevel);
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
  formattedDate = timeClient.getFormattedDate();
  
  display.clearDisplay();
  display.display();

  InitTrails();
}

uint16_t pmax = INT16_MAX;
int p = pmax;
bool debug = true;
int dot_level = 0;

void loop() { 
  if (p++ > pmax){
    p=0;
  } 

  if (minute != last_minute){
    Serial.println(" -> new minute");        
    last_minute = minute;
    formattedDate = timeClient.getFormattedDate();
    Serial.println(formattedDate);  
    millis_offset = millis();  
    animation = 45;
    current_hue = random(360) / 360.0f;
  }

  animations.UpdateAnimations();

  // wait 1 millisecond
  for(int x = 0; x<1000; x++){ float y=sin(rand()); }

}

