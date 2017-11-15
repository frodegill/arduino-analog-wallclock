/*
 * Based on the Time, Timezone and FastLED libraries
 * Running on a WeMos D1 WiFi board
 */

#include <FastLED.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <TM1637Display.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// 7-segment TM1637
#define SEGMENTS_DIO_PIN               (D6)
#define SEGMENTS_CLK_PIN               (D5)
#define SEGMENTS_BRIGHTNESS             (1) // 0-7

// RGB 60-LED ring 
#define LEDRING_DATA_PIN                (D4)
#define NUM_LEDS                        (60)

static const char ssid[] = "gill-roxrud";
static const char pass[] = "******";

static const char ntpServerName[] = "eeebox.gill-roxrud.dyndns.org";

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

//Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
Timezone CE(CEST, CET);
time_t utc, local_time, prev_local_time=0;

#if (NUM_LEDS < 256)
 typedef byte ledposition_t;
#else
 typedef unsigned short ledposition_t;
#endif

CRGB leds[NUM_LEDS];
TM1637Display segment_display(SEGMENTS_CLK_PIN, SEGMENTS_DIO_PIN);

static const CRGB hour_colour_am = CRGB(0,16,0);
static const CRGB hour_colour_pm = CRGB(0,0,16);
static const CRGB minute_colour = CRGB(16,0,0);
static const CRGB second_colour = CRGB(16,16,0);


void setLED(ledposition_t pos, const struct CRGB& colour)
{
  pos = pos%NUM_LEDS;
  leds[pos] = colour;
}

void setSegments(byte day, byte month)
{
  segment_display.showNumberDecEx(day%100, 0x30, false, 2, 0);
  segment_display.showNumberDec(month%100, false, 2, 2);
}

void setSegmentsSync()
{
  const uint8_t segments[] = {1|32|64|4|8 /*S*/, 32|64|2|4|8 /*y*/, 16|64|4 /*n*/, 64|16|8 /*c*/};
  segment_display.setSegments(segments);
}

void digitalClockDisplay(const time_t& local_time)
{
  ledposition_t pos;  
  for (pos=0; pos<NUM_LEDS; pos++) leds[pos]=CRGB::Black;

  CRGB hour_colour = isAM(local_time) ? hour_colour_am : hour_colour_pm;
  int minute_pos = minute(local_time)*NUM_LEDS/60;
  int hour_pos = hour(local_time)*NUM_LEDS/12 + (minute_pos/12);
  setLED(hour_pos-1, hour_colour);
  setLED(hour_pos, hour_colour);
  setLED(hour_pos+1, hour_colour);

  setLED(minute_pos, minute_colour);

  setLED(second(local_time)*NUM_LEDS/60, second_colour);

  setSegments(day(local_time), month(local_time));

  FastLED.show();
}

/*-------- NTP code ----------*/

static const byte NTP_PORT = 123;
static const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, NTP_PORT);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

time_t getNtpUtcTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets

  IPAddress ntpServerIP;
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL;
    }
  }
  return 0; // return 0 if unable to get the time
}

void setup()
{
  FastLED.addLeds<WS2812B, LEDRING_DATA_PIN, GRB>(leds, NUM_LEDS);

  segment_display.setBrightness(SEGMENTS_BRIGHTNESS);
  setSegmentsSync();

  WiFi.begin(const_cast<char*>(ssid), pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }

  Udp.begin(localPort);
  setSyncProvider(getNtpUtcTime);
  setSyncInterval(300);
}

void loop()
{
  if (timeStatus() != timeNotSet) {
    utc = now();
    local_time = CE.toLocal(utc);

    if (local_time != prev_local_time) {
      prev_local_time = local_time;
      digitalClockDisplay(local_time);
      delay(1000 - (millis()%1000));
    }
  }
}

