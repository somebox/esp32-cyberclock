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

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/*

A large four digit 7-segment LED clock, controlled by an ESP32. 
The LED modules are quite large, 5" tall, and require high voltage to drive the many
leds in series for each segment (12-14v). To do this, four shift registers are used in 
series (TPIC6C596). These chips were purchased on breakout boards, based on a board
design by Sparkfun (https://learn.sparkfun.com/tutorials/large-digit-driver-hookup-guide/all).
It allows each digit of the large seven segments to be controlled with higer voltages.
As the ESP8266 is 3.3v logic, and the TPIC chip requires 5v logic, a level conversion was
required. A four channel 75HTC125N chip  does the job here.

There are four connections to the ESP8266. Three for the shift register chips, and one
for the blinky dots (animated with PWM). 

*/

WiFiManager wm;

#define PIN_CLK   18    // Clock on the TPIC6C596
#define PIN_LATCH 19    // Latch 
#define PIN_DATA  23    // Serial data
#define PIN_BRIGHTNESS 14 // PWM Enable on TPIC6C596
#define LIGHT_SENSOR 33  // light-dependent resistor

#define DISPLAY_SIZE 6  // Number of digits

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 7200, 600000); // proto, host, timeOffset, updateInterval

// NeoPixels
const uint16_t PixelCount = 25; // make sure to set this to the number of pixels in your strip
const uint16_t PixelPin = 5;  // make sure to set this to the correct pin, ignored for Esp8266
const uint16_t AnimCount = 1; // we only need one
const uint16_t TailLength = 4; // length of the tail, must be shorter than PixelCount
const float MaxLightness = 0.2f; // max lightness at the head of the tail (0.5f is full bright)

NeoGamma<NeoGammaTableMethod> colorGamma; // for any fade animations, best to correct gamma

NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);
// for esp8266 omit the pin
//NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount);

NeoPixelAnimator animations(AnimCount); // NeoPixel animation management object


int number = 0;
int t = 0;
int hour, minute, second = 0;
String formattedDate;
String dayStamp;

// Days of week. Day 1 = Sunday
String DoW[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// Months
String Months[] { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };


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

void LoopAnimUpdate(const AnimationParam& param)
{
    // wait for this animation to complete,
    // we are using it as a timer of sorts
    if (param.state == AnimationState_Completed)
    {
        // done, time to restart this position tracking animation/timer
        animations.RestartAnimation(param.index);

        // rotate the complete strip one pixel to the right on every update
        strip.RotateRight(1, 0, strip.PixelCount()-5);
    }
}

float hue;
void DrawTailPixels()
{
    // using Hsl as it makes it easy to pick from similiar saturated colors
    hue = random(360) / 360.0f;
    for (uint16_t index = 0; index < strip.PixelCount()-4 && index <= TailLength; index++)
    {
        float lightness = index * MaxLightness / TailLength;
        RgbColor color = HslColor(hue, 1.0f, lightness);

        strip.SetPixelColor(index, colorGamma.Correct(color));
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


void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Large Digit Driver Example");
  
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Clear the buffer
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,0);             // Start at top-left corner
  display.setTextSize(2);             // Normal 1:1 pixel scale
  display.println("Connecting...");
  display.display();
  

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

  // Draw the tail that will be rotated through all the rest of the pixels
  DrawTailPixels();

  // we use the index 0 animation to time how often we rotate all the pixels
  animations.StartAnimation(0, 60, LoopAnimUpdate); 

  log_status_info();
  ConnectToWifi();
  formattedDate = timeClient.getFormattedDate();
  
  display.clearDisplay();
  display.display();
}

int pmax = 16383;
int p = pmax;
int brightness = 0; 
bool debug = true;
int level = 0;
int last_level = 0;
int dot_level = 0;
int last_minute=0;
int animation=0;


void loop()
{ 
// SIMPLE TEST
// p = random(100);
// showNumber(p);
// delay(500);
// Serial.println(p);

  if (p++ > pmax){
    p=0;
  } 

  if (minute != last_minute){
    Serial.println(" -> new minute");
    DrawTailPixels();
    formattedDate = timeClient.getFormattedDate();
    Serial.println(formattedDate);    
    animation = 45;
  }
  
  // brightness adjustment to sensor (optional)
  // if (p % 200 == 0){
  //   last_level = level;
  //   brightness = 800; // analogRead(LIGHT_SENSOR);
  //   level = map(brightness, 50, 1024, 1024, 0);
  //   level = last_level + (level - last_level)/30;  // slew changes, avoid jumps
  //   level = max(50, level);
  //   level = min(1005, level);
  //   Serial.print(" Light Level: ");
  //   Serial.println(brightness);
  //   ledcWrite(1, level);
  //   // OVERRIDE, no sensor
  //   level = 50;
  // }

  // dot_level = abs(map(millis() % 2000, 0, 1999, -1023, 1023));
  // ledcWrite(0, map(dot_level, 0, 1024, 0, 1024 - level * 0.9));

  if (p % 117) {
    last_minute = minute;
    minute = timeClient.getMinutes();
    hour = timeClient.getHours();
    if (animation > 0){
      showRandom();
      animation--;
      delayMicroseconds(animation*animation*20);
    } else {
      second = timeClient.getSeconds();
      showNumber(hour*10000+minute*100+second);
    }
  }

  if (p % 3 == 0) {
   // Serial.print(" sensor: ");
  //  Serial.print(analogRead(LIGHT_SENSOR));
  //  Serial.print(" level: ");
  //  Serial.println(level);
    if (animation > 0){ 
      Serial.println("Animation...");
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);        // Draw white text
    display.setCursor(0,0);             // Start at top-left corner
    display.setTextSize(2);             // Normal 1:1 pixel scale

    display.print(timeClient.getFormattedTime());
    display.print(".");
    display.println(millis()/100 % 10);
    display.println(DoW[timeClient.getDay()]);
    
    int splitT = formattedDate.indexOf("T");
    dayStamp = formattedDate.substring(0, splitT);
    display.println(dayStamp);
    display.setTextSize(1); 
    //display.println();
    display.println(WiFi.localIP());
    display.print("WIFI RSSI: ");
    display.print(WiFi.RSSI());
    display.println(" dB");
    
    display.display();
  }

  if (p % 5 == 0) {
    for (uint16_t dot = 0; dot < 4; dot++){
      RgbColor color = HslColor(hue, 1.0f, abs(sin(p/200.0*(dot+1)))/2.0);
      strip.SetPixelColor(strip.PixelCount()+(3-dot)-4, colorGamma.Correct(color));
    }
  }

  animations.UpdateAnimations();
  strip.Show();

  // wait 1 millisecond
  long m = millis();
  while (millis() == m){ NOP(); }
  //Serial.println(p);
}

