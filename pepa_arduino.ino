//#include <Preferences.h> //To store things in memory even when you reset the MCU. Only for RAM items or items that might be lost after reset.
#include <AudioFileSourceLittleFS.h> //This reads the file
#include <AudioGeneratorMP3.h> //Decodes the .mp3 file
#include <AudioOutputI2S.h> //This sends the sound
#include <WiFi.h> //The wifi library 
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
//#include <TFT_eSPI.h>
#include <SPI.h>
#include "LittleFS.h"
#include "secrets.h"

// Teddy sprite dimensions
#define SPRITE_W 24
#define SPRITE_H 24


AudioFileSourceLittleFS *file = nullptr; //nullptr is a null pointer. If you haven't initialized them and you call the 
AudioGeneratorMP3 *mp3 = nullptr; //file or mp3 or out, you might run into an error. But given the current structure of my code
AudioOutputI2S *out = nullptr; //playSound is always going to be called first, so I'm not sure if I should remove it or leave it.

const int SW_PIN = D7;
const int BCLK_PIN = D0; //Amplifier pin
const int LRCLK_PIN = D1; //Amplifier pin
const int DIN_PIN = D3; //Amplifier pin 
const int DISPLAYDIN_PIN = D10; //Screen pin
const int CLK_PIN = D8; //Screen pin
const int CS_PIN = D4; //Screen pin
const int DS_PIN = D9; //Screen pin
const int RST_PIN = D5; //Screen pin

Adafruit_ST7789 tft = Adafruit_ST7789(CS_PIN,DS_PIN,RST_PIN);

int prevSwState = HIGH;
int currSwState = HIGH; //Press is detected when it goes from Low -> High 
long apiInterval = 900000;
long prevAPICall = 0;
unsigned int prevDebounceTime = 0;

bool musicPlaying = false;
bool wifiRetry = false;
bool wifiErrorText = false;
bool littleFSMount = false;
bool WiFiConnected = false;

String weatherMeaning = "";
String outfitRec = "";

float windSpeed;

// Color palette (RGB565)
// 0 = transparent
const uint16_t TEDDY_PALETTE[] PROGMEM = {
  0x0000, // 0 Transparent
  0x3102, // 1 Dark outline
  0x7A25, // 2 Deep brown
  0xA56B, // 3 Warm brown
  0xFFE2, // 4 Cream
  0xF6A0, // 5 Pink cheeks
  0x0000, // 6 Eyes (black)
  0xFE20, // 7 Golden sun
  0xFF60, // 8 Sun highlight
  0x95BE, // 9 Moon blue
  0xFEA0, // 10 Stars
  0x6B6E  // 11 Night purple
};

// Each character represents one pixel.
// 0 = transparent, 1-9 and A-B = palette indexes.

const char DAY_TEDDY[SPRITE_H][SPRITE_W + 1] PROGMEM = {
  "000111000000000000111000",
  "001333100000000001333100",
  "013333310000000013333310",
  "013333310111111113333310",
  "013333311222222211333310",
  "011331333222222233313110",
  "001113333322222333331100",
  "000113333322222333331000",
  "000123333322222333332100",
  "000122366222222663322100",
  "000122266244444662222100",
  "000122222441144422222100",
  "000155224441144442551100",
  "001355224441114442551310",
  "013331122441114422113331",
  "013333311244444211333331",
  "013333322177771122333331",
  "001333222278772222233310",
  "000133332277872223333100",
  "001344433277772233444310",
  "001444443222222234444410",
  "001444443222222234444410",
  "000144431122222113444100",
  "000011110111111101111000"
};

const char NIGHT_TEDDY[SPRITE_H][SPRITE_W + 1] PROGMEM = {
  "000111000000000000111000",
  "001222100000000001222100",
  "012222210000000012222210",
  "012222210111111112222210",
  "012222211BBBBBBB11222210",
  "011221222BBBBBBB22212110",
  "0011122222BBBBB222221100",
  "0001122222BBBBB222221000",
  "0001B22222BBBBB22222B100",
  "0001BB266BBBBBB6622BB100",
  "0001BBB66B4444466BBBB100",
  "0001BBBBB4411444BBBBB100",
  "000155BB444114444B551100",
  "001255BB444111444B551210",
  "0122211BB4411144BB112221",
  "012222211B44444B11222221",
  "0122222BB199BBB1BB222221",
  "001222BBAB99BBBBBBB22210",
  "00012222BB99BBBAB2222100",
  "001244422B99BBBB22444210",
  "001444442B9999BB24444410",
  "001444442BBBABBB24444410",
  "0001444211BBBBB112444100",
  "000011110111111101111000"
};

// Convert character into palette index
uint8_t getColorIndex(char pixel)
{
  if (pixel >= '0' && pixel <= '9')
    return pixel - '0';

  if (pixel == 'A')
    return 10;

  if (pixel == 'B')
    return 11;

  return 0;
}

// Draw a teddy sprite
void drawTeddy(const char sprite[SPRITE_H][SPRITE_W + 1], int x, int y, uint8_t scale)
{
  for (int row = 0; row < SPRITE_H; row++)
  {
    for (int col = 0; col < SPRITE_W; col++)
    {
      char pixel = pgm_read_byte(&sprite[row][col]);

      uint8_t colorIndex = getColorIndex(pixel);

      // Skip transparent pixels
      if (colorIndex != 0)
      {
        uint16_t color =
          pgm_read_word(&TEDDY_PALETTE[colorIndex]);

        tft.fillRect(
          x + col * scale,
          y + row * scale,
          scale,
          scale,
          color
        );
      }
    }
  }
}


void stopSound()
{
  if (mp3) //If we try delete an empty mp3(audio generator) we will run into an error, this check helps us fix that.
  {
    if (mp3 -> isRunning())
    {
      mp3->stop();
    }
    delete mp3; //If you don't delete the old/current mp3 and file from the memory, it will just be there and inaccessible 
    mp3 = nullptr;
  }
  if (file)
  {
    delete file; //when you assign it to another decoder(until the MCU resets)
    file = nullptr;
  }
  musicPlaying = false;
  Serial.println("Too..da...loo"); //lol
}

void playSound(String climate)
{
  if (climate == "")
  {
    Serial.println("No matching song found");
    return;
  }
  //Climate -> rain, wind, snow, sunny
  file = new AudioFileSourceLittleFS(("/songs/"+ climate +".mp3").c_str()); 
  mp3 = new AudioGeneratorMP3();

  if(mp3->begin(file,out))//tells the decoder which file to decode and where to send it to
  { 
    musicPlaying = true;
  }
  else
  {
    Serial.println("Couldn't start audio");
    delete mp3;
    mp3 = nullptr;

    delete file;
    file = nullptr;

    musicPlaying = false;
  }
}
String songName(String meaning)
{
  if (meaning.indexOf("Sunny") >= 0 || meaning.indexOf("cloudy") >= 0)
  {
    return "sunny";
  }
  else if (meaning.indexOf("rain") >= 0 || meaning.indexOf("drizzle") >= 0)
  {
    return "rain";
  }
  else if (meaning.indexOf("thunderstorm") >= 0 || meaning.indexOf("fog") >= 0)
  {
    return "wind";
  }
  else if (meaning.indexOf("snow") >= 0)
  {
    return "snow";
  }
  return "";
}

void connectWiFi()
{
  //Setting up wifi connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000)
  {
    delay(500);
    Serial.println("Not connected, trying again");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi Connected"); 
    WiFiConnected = true;
  }
  else
  {
    Serial.println("WiFi not connected, connection timed out");
  } 
}

void getWeather()
{
  HTTPClient https;
  https.begin(weatherAPI);
  int httpCode = https.GET();
  if (httpCode == 200)
  {
    String response = https.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc,response); //deserialize response into doc and gets error if any occurs
    if (error)
    {
      Serial.println("Error occured with JSON: ");
      Serial.println(error.c_str());
      https.end();
      return;
    }

    //current weather
    String datetime = doc["current"]["time"]; //returns "2026-10-30T14:30"
    datetime.replace("T"," ");
    float temperature = doc["current"]["temperature_2m"]; //2f
    float apparentTemp = doc["current"]["apparent_temperature"]; //2f
    int weatherCode = doc["current"]["weather_code"];
    int isDay = doc["current"]["is_day"]; //1 is day, 0 is night 
    windSpeed = doc["current"]["wind_speed_10m"];


    //daily weather
    float highestTemp = doc["daily"]["temperature_2m_max"][0];
    float lowestTemp = doc["daily"]["temperature_2m_min"][0];
    float moonPhase = doc["daily"]["moon_phase"][0];

    //hourly weather
    int hours = datetime.substring(11,13).toInt();
    float precipitation = doc["hourly"]["precipitation_probability"][hours]; //gets the percipitation probability of the hour
    
    weatherMeaning = decodeWeather(weatherCode);
    outfitRec = outfitRecommendation(weatherMeaning,apparentTemp,precipitation);

    displayItems(temperature, weatherMeaning, datetime, highestTemp, lowestTemp, moonName(moonPhase));
    displayPet(isDay, 124, 150, 3); //124, 50, 3 are placeholders for the X coordinate, Y coordinate and size
  }
  else
  {
    tft.print("Error occured: ");
    tft.println(httpCode);
  }
  https.end();
}
String moonName(float moonPhase)
{
  // moonPhase ranges from 0.0 to 1.0
  String moon = "";

  if (moonPhase < 0.03 || moonPhase >= 0.97)
  {
    moon = "New Moon";
  }
  else if (moonPhase < 0.22)
  {
    moon = "Waxing Crescent";
  }
  else if (moonPhase < 0.28)
  {
    moon = "First Quarter";
  }
  else if (moonPhase < 0.47)
  {
    moon = "Waxing Gibbous";
  }
  else if (moonPhase < 0.53)
  {
    moon = "Full Moon";
  }
  else if (moonPhase < 0.72)
  {
    moon = "Waning Gibbous";
  }
  else if (moonPhase < 0.78)
  {
    moon = "Last Quarter";
  }
  else
  {
    moon = "Waning Crescent";
  }

  return moon;
}
String decodeWeather(int weatherCode)
{
  switch (weatherCode) {
    case 0:  return "Sunny";
    case 1:  return "Sunny";
    case 2:  return "Partly cloudy";
    case 3:  return "cloudy"; //originally overcast
    case 45: return "fog";
    case 48: return "Depositing rime fog";
    case 51: return "Light drizzle";
    case 53: return "Moderate drizzle";
    case 55: return "Dense drizzle";
    case 56: return "Light freezing drizzle";
    case 57: return "Dense freezing drizzle";
    case 61: return "Slight rain";
    case 63: return "Moderate rain";
    case 65: return "Heavy rain";
    case 66: return "Light freezing rain";
    case 67: return "Heavy freezing rain";
    case 71: return "Slight snowfall";
    case 73: return "Moderate snowfall";
    case 75: return "Heavy snowfall";
    case 77: return "snow grains";
    case 80: return "Slight rain showers";
    case 81: return "Moderate rain showers";
    case 82: return "Violent rain showers";
    case 85: return "Slight snow showers";
    case 86: return "Heavy snow showers";
    case 95: return "thunderstorm";
    case 96: return "thunderstorm with slight hail";
    case 97: return "Heavy thunderstorm";
    case 99: return "thunderstorm with heavy hail";
    default: return "Unknown weather condition";
}
}
String outfitRecommendation(String meaning, float apparentTemp, float precipitation)
{
  String text = "";

  if (meaning.indexOf("thunderstorm") >= 0 || windSpeed > 50)
  {
    text = "Girl, STAY INSIDE";
  }

  else if (meaning.indexOf("snow") >= 0)
  {
    if (apparentTemp < 40)
    {
      text = "Bundle up, it's freezing";
    }
    else
    {
      text = "Body go tell you";
    }
  }

  else if (meaning.indexOf("rain") >= 0 ||
           meaning.indexOf("drizzle") >= 0 ||
           precipitation >= 40)
  {
    if (apparentTemp < 55)
    {
      text = "It's a little rain, you can handle it....and cold";
    }
    else
    {
      text = "Don't forget your umbrella";
    }

    if (meaning.indexOf("freezing") >= 0)
    {
      text += " It's freezing";
    }
  }

  else if (meaning == "Sunny")
  {
    if (apparentTemp < 55)
    {
      text = "It's kinda cold";
    }
    else if (random(0, 2) == 0)
    {
      text = "It's giving sunshine";
    }
    else
    {
      text = "Show some skin today";
    }
  }

  else
  {
    text = "Dress comfortably for the weather";
  }

  return text;
}

//Make changes to this function display when you get yor screen
void displayItems(float temperature, String weatherMeaning, String dateTime, float highestTemp, float lowestTemp, String moonPhase)
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(true);
  //Change the cursor values to it's actual position (x,y)

  //weather condition
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(3);
  tft.setCursor(1,10);
  tft.println(weatherMeaning);

  //temperature
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(5);
  tft.setCursor(2,10); //put in the cursor when you have your screen.
  tft.print(temperature,0); //wraps it up as a whole number
  tft.println("F");
  //Decide if you want to set it to bold and #include the bold font of your choice

  //Date and time
  tft.setTextSize(3);
  tft.setCursor(3,10);
  tft.println(dateTime);
  
  //Day high and low
  tft.setCursor(4,10);
  tft.setTextSize(3);
  tft.print("H: ");
  tft.print(highestTemp,0);
  tft.println("F");

  tft.setCursor(5,10);
  tft.setTextSize(3);
  tft.print("L: ");
  tft.print(lowestTemp,0);
  tft.print("F");

  //Moon Phase
  tft.setCursor(6,10);
  tft.println(moonPhase);

  //Outfit Recommendation
  tft.setCursor(7,10);
  tft.setTextSize(4);
  tft.println(outfitRec);
}

void displayPet(int isDay, int x, int y, uint8_t scale)
{
  if (isDay == 1)
  {
    drawTeddy(DAY_TEDDY, x, y, scale);
  }
  else
  {
    drawTeddy(NIGHT_TEDDY, x, y, scale);
  }
}
void setup()
{
  Serial.begin(115200);
  pinMode(SW_PIN, INPUT_PULLUP); //since it's a switch and the other end is GND. 
  tft.init(240,320); //width,height
  tft.setRotation(1); //Landscape
  tft.fillScreen(ST77XX_BLACK);//clears screen
  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mounting failed");
    littleFSMount = false;
  }
  else
  {
    littleFSMount = true; 
  }

  out = new AudioOutputI2S(); //This only needs to be initialized once since it's just the output connection and that ain't chaning.
  out ->SetPinout(BCLK_PIN,LRCLK_PIN,DIN_PIN); //BCLK,LRCLK,DIN
  out->SetGain(0.5); //0.5 is a placeholder, determine the level you want to set it to after testing. The range is 0.0 - 4.0

  connectWiFi();
  if (WiFiConnected)
  {
    getWeather();
  }
  prevAPICall = millis();
}

void loop()
{
  if(millis() - prevAPICall >= apiInterval && WiFiConnected) //gets the weather updates every 15 mins
  {
    getWeather();
    prevAPICall = millis();
  }
  int swReading = digitalRead(SW_PIN);
  if (swReading != prevSwState)
  {
    prevDebounceTime = millis(); //To debounce switch errors.
  }

  if ((millis() - prevDebounceTime) > 30)
  {
    if (swReading != currSwState)
    {
      currSwState = swReading;

      if (currSwState == HIGH)
      {
        if (musicPlaying)
        {
          stopSound();
        }
        else if (littleFSMount)
        {
          playSound(songName(weatherMeaning)); 
        }
        
      }
    }
  }
  prevSwState = swReading;

  if (mp3 != nullptr && mp3 -> isRunning()) //checks that mp3 is not still nullptr(because if the btn is not pressed and this piece of code runs, it will give an error)  
  //and whether the AudioGeneratorMP3 has been initialized 
  {
    if (!mp3->loop()) // Keep the song playing continuously
    {
      stopSound();
      playSound(songName(weatherMeaning));
    }
  }

  if (!wifiRetry && WiFi.status() != WL_CONNECTED)
  {
    WiFiConnected = false;
    WiFi.begin(ssid, password);
    wifiRetry = true;
    
    unsigned long retryStart = millis();

    // Wait up to 10 seconds for reconnection
    while (WiFi.status() != WL_CONNECTED && millis() - retryStart < 10000)
    {
      delay(500);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      wifiRetry = false; // Allow another attempt if Wi-Fi disconnects later
      Serial.println("WiFi reconnected");
      WiFiConnected = true;
    }
    else
    {
      Serial.println("WiFi reconnection failed");
      tft.fillScreen(ST77XX_BLACK);
      tft.setTextSize(4); //placeholder
      tft.setTextColor(ST77XX_CYAN);
      tft.setCursor(6, 10); //placeholder
      tft.println("No Internet Connection");

      wifiErrorText = true;
    }
  }

  // Clear the disconnection message after Wi-Fi reconnects
  if (WiFi.status() == WL_CONNECTED && wifiErrorText)
  {
    WiFiConnected = true;
    wifiErrorText = false;
    wifiRetry = false;
    getWeather();
  }
}