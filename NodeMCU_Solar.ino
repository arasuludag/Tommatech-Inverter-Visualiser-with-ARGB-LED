#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ArduinoJson.h>

#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

#include "secrets.h"  // Define SECRET_SSID, SECRET_WIFI_PASSWORD, SECRET_FINGERPRINT, SECRET_USERNAME and SECRET_PASSWORD, IDEAL_DAILY_PRODUCTION, SYSTEM_SIZE in secrets.h

#define LED_PIN 16
#define NUM_LEDS 39
CRGB leds[NUM_LEDS];

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 6400);

String token;
bool tokenExpired = true;

String jsonPayload;

int fetchFailed = 0;

const int httpsPort = 443;  //HTTPS= 443 and HTTP = 80

// Set web server port number to 80
WiFiServer server(80);

// SHA1 finger print of certificate.
const char fingerprint[] PROGMEM = SECRET_FINGERPRINT;

void setup() {
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);

  leds[NUM_LEDS-1].setRGB( 255, 255, 255);
  FastLED.show();

  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);  //Prevents reconnection issue (taking too long to connect)
  delay(1000);
  WiFi.mode(WIFI_STA);  //Only Station No AP, This line hides the viewing of ESP as wifi hotspot

  WiFi.begin(SECRET_SSID, SECRET_WIFI_PASSWORD);  //Connect to your WiFi router
  Serial.println("\n");
  Serial.print("Connecting");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  //If connection successful show IP address in serial monitor
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(SECRET_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  // IP address assigned to ESP.

  server.begin();
  timeClient.begin();
}

void loop() {

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;

  delay(1000);

  if (tokenExpired) {
      // configure traged server and url
      https.begin(*client, "https://www.tommatech-portal.de/phoebus/login/loginNew?username=" SECRET_USERNAME "&userpwd=" SECRET_PASSWORD);  // HTTP
      https.addHeader("Content-Type", "application/json");

      Serial.print("[HTTPS] POST (For TOKEN)... ");
      // start connection and send HTTP header and body
      int httpCode = https.POST("");

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Serial.printf("code: %d\n", httpCode);

        // file found at server
        if (httpCode == HTTP_CODE_OK) {
          
          DynamicJsonDocument res(4096);
          deserializeJson(res, https.getString());
          JsonObject obj = res.as<JsonObject>();

          token = obj["token"].as<String>();
          Serial.println(token);

        }
      } else {
        Serial.printf("failed, error: %s\n", https.errorToString(httpCode).c_str());

        // If fetching failes 5 consecutive times, reset ESP.
        if (fetchFailed == 5) {
         ESP.restart();
        }

        fetchFailed++;

        leds[NUM_LEDS-1].setRGB( 0, 255, 0);
        FastLED.show();
      }

      https.end();
  }

  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime);
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  int currentYear = ptm->tm_year+1900;
  String currentDate = String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay);
  Serial.print("Current date: ");
  Serial.println(currentDate);

  https.begin(*client, "https://www.tommatech-portal.de/phoebus/userIndex/getCurrentData?token=" + token + "&currentTime=" + currentDate);  // HTTPS
  https.addHeader("Content-Type", "application/json");

  Serial.print("[HTTPS] POST (For POWER)... ");
  // start connection and send HTTP header
  int httpCode = https.POST("");

  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {

      jsonPayload = https.getString();

      StaticJsonDocument<256> res;
      deserializeJson(res, jsonPayload);
      JsonObject obj = res.as<JsonObject>();

//       {
//     "cakeYield": 40.9,
//     "totalEpsEnergy": 0.0,
//     "todayConsumeEnergy": 0.0,
//     "todayFeedInEnergy": 0.0,
//     "consumeenergy": 0.0,
//     "yearEpsEnergy": 0.0,
//     "totalYield": 742.1,
//     "monthEpsEnergy": 0.0,
//     "monthYield": 245.80000000000004,
//     "todayYield": 40.9,
//     "yearYield": 742.1,
//     "feedinenergy": 0.0,
//     "todayEpsEnergy": 0.0,
//     "gridpower": 2946.0
// }

      if (!obj.containsKey("cakeYield")) {
        Serial.println("Token Expired.");
        tokenExpired = true;
        return;
      } else {
        tokenExpired = false;
      }

      int gridPower = obj["gridpower"].as<signed int>();
      int feedInPower = obj["feedinenergy"].as<signed int>();
      float dailyYield = obj["cakeYield"].as<float>();


      // Print values.
      Serial.print("Grid Power: ");
      Serial.print(gridPower);

      Serial.print(" | Feed-In Power: ");
      Serial.println(feedInPower);

      FastLED.delay(1000);
      Light(feedInPower, gridPower, dailyYield);
      FastLED.delay(1000);

      fetchFailed = 0;

    }
  } else {
    Serial.printf("failed, error: %s\n", https.errorToString(httpCode).c_str());


    // If fetching failes 5 consecutive times, reset ESP.
    if (fetchFailed == 5) {
      ESP.restart();
    }

    fetchFailed++;

    leds[NUM_LEDS-1].setRGB( 255, 0, 0);
    FastLED.show();
}

https.end();

// POST the JSON data to the local IP.
// Maybe we can use it later to fetch with another device.
for (int i = 0; i < 240; i++) {
  postLocal(jsonPayload);
  delay(1000);
}

}

void postLocal(String payload) {

  WiFiClient client = server.available();  // Listen for incoming clients

  if (client) {                     // If a new client connects,

    Serial.print("New Client. ");  // print a message out in the serial port
    String currentLine = "";        // make a String to hold incoming data from the client

    while (client.connected()) {  // loop while the client's connected

      if (client.available()) {  // if there's bytes to read from the client,
        char c = client.read();  // read a byte, then
        if (c == '\n') {  // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type: application/json");
            client.println("Connection: close");
            client.println();

            client.println(payload);

            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;

          } else {  // if you got a newline, then clear currentLine
            currentLine = "";
          }

        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }

    // Close the connection
    client.stop();
    Serial.print("Client disconnected.");
  }
}

void Light(int FeedIn, int Grid, float dailyYield) {

  int feedInMapped = map(FeedIn, -SYSTEM_SIZE, SYSTEM_SIZE, -NUM_LEDS, NUM_LEDS);
  int gridMapped = map(Grid, 0, SYSTEM_SIZE, 0, NUM_LEDS);
  int dailyYieldMapped = map(dailyYield, 0, IDEAL_DAILY_PRODUCTION, 0, NUM_LEDS);

  Serial.print("Grid Mapped: ");
  Serial.print(gridMapped);

  Serial.print(" | Feed In Mapped: ");
  Serial.println(feedInMapped);

  Serial.print(" | Daily Yield Mapped: ");
  Serial.println(dailyYieldMapped);

  // Solar usage is gridPower - feedInPower when feedInPower > 0.
  int solarHomeUsage = FeedIn > 0 ? gridMapped - feedInMapped : gridMapped;

  FastLED.clear();
  
  if (dailyYield != 0)
  for (int i = 0; i < NUM_LEDS; i++) {

     if (i < dailyYieldMapped) {
      leds[i].setRGB( 255, 70, 0);
      continue;
    }

      leds[i].setRGB( 255, 255, 255);
    
  }

  // Whichever is needed. I'm not erasing this.
  // for (int i = 0; i < NUM_LEDS; i++) {

  //    if (i < solarHomeUsage) {
  //     leds[i].setRGB( 0, 200, 255);
  //     continue;
  //   }

  //   else if (FeedIn < 0 && i < -feedInMapped + solarHomeUsage) {
  //     leds[i].setRGB( 255, 20, 0);
  //   }

  //   else if (FeedIn > 0 && i < feedInMapped + solarHomeUsage) {
  //     leds[i].setRGB( 255, 70, 0);
  //   }
  // }

  FastLED.show();  
}
