#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

#include <ArduinoJson.h>

#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

#include "secrets.h"  // Define SECRET_SSID, SECRET_WIFI_PASSWORD, SECRET_FINGERPRINT, SECRET_USERNAME and SECRET_PASSWORD in secrets.h

#define LED_PIN 16
#define NUM_LEDS 28
CRGB leds[NUM_LEDS];

#define SYSTEM_SIZE 8000  // Peak wattage of the solar system.

const char *ssid = SECRET_SSID;  // WIFI Stuff
const char *password = SECRET_WIFI_PASSWORD;

String token;  // This needs to be global. Session token.
bool tokenExpired = true;

const char *host = "www.tommatech-portal.de";
const int httpsPort = 443;  //HTTPS= 443 and HTTP = 80

// SHA1 finger print of certificate.
const char fingerprint[] PROGMEM = SECRET_FINGERPRINT;

void setup() {
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);

  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);  //Prevents reconnection issue (taking too long to connect)
  delay(1000);
  WiFi.mode(WIFI_STA);  //Only Station No AP, This line hides the viewing of ESP as wifi hotspot

  WiFi.begin(ssid, password);  //Connect to your WiFi router
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
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  // IP address assigned to ESP.
}

void loop() {
  WiFiClientSecure httpsClient;  //Declare object of class WiFiClient
  httpsClient.setFingerprint(fingerprint);

  httpsClient.setTimeout(15000);  // 15 Seconds
  delay(1000);

  Serial.println("HTTPS Connecting");
  int r = 0;  // Retry counter.
  while ((!httpsClient.connect(host, httpsPort)) && (r < 30)) {
    delay(100);
    Serial.print(".");
    r++;
  }
  if (r == 30) {
    Serial.println("Connection failed.");
  } else {
    Serial.println("Connected to web.");
  }

if (tokenExpired) {
  String LoginLink = "/phoebus/login/loginNew";
  httpsClient.print(String("POST ") + LoginLink + "?username=" + SECRET_USERNAME + "&" + "userpwd=" SECRET_PASSWORD + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Content-Type: application/x-www-form-urlencoded;charset=UTF-8" + "\r\n" + "\r\n");

  Serial.print("Token request sent. ");

  while (httpsClient.connected()) {
    String line = httpsClient.readStringUntil('\n');
    if (line == "\r") {
      Serial.print("Token received: ");
      break;
    }
  }

  String lineLogin;
  while (httpsClient.available()) {
    lineLogin = httpsClient.readStringUntil('\n');  // Read Line by Line

    if (lineLogin.startsWith("{")) {

      DynamicJsonDocument res(4096);
      deserializeJson(res, lineLogin);
      JsonObject obj = res.as<JsonObject>();
      String tokenTemp = obj["token"];
      token = tokenTemp;
      Serial.println(token);
    }
  }
}

  String LinkGetPower = "/phoebus/userIndex/getPower";
  httpsClient.print(String("POST ") + LinkGetPower + "?token=" + token + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Content-Type: application/x-www-form-urlencoded;charset=UTF-8" + "\r\n" + "\r\n");

  Serial.print("getPower request sent. ");

  while (httpsClient.connected()) {
    String line = httpsClient.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("Headers received.");
      break;
    }
  }

  String linePower;
  while (httpsClient.available()) {
    linePower = httpsClient.readStringUntil('\n');  //Read Line by Line
    if (linePower.startsWith("{")) {

      DynamicJsonDocument res(1024);
      deserializeJson(res, linePower);
      JsonObject obj = res.as<JsonObject>();

      if (!obj.containsKey("gridPower")) {
        Serial.println("Token Expired.");
        tokenExpired = true;
        continue;
      } else {
        tokenExpired = false;
      }

      String gridPower = obj["gridPower"];
      String feedInPower = obj["feedInPower"];


      // Print values.
      Serial.println("__________");

      Serial.print("Grid Power: ");
      Serial.println(gridPower);

      Serial.print("Feed-In Power: ");
      Serial.println(feedInPower);

      Serial.println("__________");

      Light(feedInPower.toInt(), gridPower.toInt());
      delay(300000);
    }
  }
  
}

void Light(int FeedIn, int Grid) {

  int feedInMapped = map(FeedIn, -SYSTEM_SIZE, SYSTEM_SIZE, -NUM_LEDS, NUM_LEDS);
  int gridMapped = map(Grid, 0, SYSTEM_SIZE, 0, NUM_LEDS);

  Serial.print("Grid Mapped: ");
  Serial.println(gridMapped);

  Serial.print("FeedIn Mapped: ");
  Serial.println(feedInMapped);

  Serial.println("__________");

  for (int i = 0; i < NUM_LEDS; i++) {

    if (FeedIn < 0 && gridMapped <= i && i < -feedInMapped + gridMapped) {
      leds[i] = CRGB(255, 10, 0);
      delay(100);
      FastLED.show();  // These seperate .show()s are because of some sort of bug with ESP8266.
      continue;
    }

    if (FeedIn > 0 && gridMapped <= i && i < feedInMapped + gridMapped) {
      leds[i] = CRGB(255, 70, 0);
      delay(100);
      FastLED.show();
      continue;
    }

    if (i < gridMapped) {
      leds[i] = CRGB(0, 255, 200);
      delay(100);
      FastLED.show();
      continue;
    }

    leds[i] = CRGB(0, 0, 0);
    FastLED.show();
  }
}
