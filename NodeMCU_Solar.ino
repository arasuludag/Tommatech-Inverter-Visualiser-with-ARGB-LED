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

// Set web server port number to 80
WiFiServer server(80);

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

  server.begin();
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

      // {"ratedPower":9.1,"gridPower":249.0,"relay2Power":0.0,"feedInPower":5.0,"relay1Power":0.0,"batPower1":0.0}

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

      // POST the JSON data to the local IP.
      // Maybe we can use it later to fetch with another device.
      postLocal(linePower);
      
      delay(10000);
    }
  }
  
}

// Variable to store the HTTP request
String header;
// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

void postLocal (String linePower) {
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime) { // loop while the client's connected
      currentTime = millis();         
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:application/json");
            client.println("Connection: close");
            client.println();
            
            client.println(linePower);
            
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;

          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }

        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
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

  // {"ratedPower":9.1,"gridPower":249.0,"relay2Power":0.0,"feedInPower":5.0,"relay1Power":0.0,"batPower1":0.0}
  // Solar usage is gridPower - feedInPower when feedInPower > 0.
  int solarHomeUsage = FeedIn > 0 ? gridMapped - feedInMapped : gridMapped;

  for (int i = 0; i < NUM_LEDS; i++) {

    if (i < solarHomeUsage) {
      leds[i] = CRGB(0, 255, 200);
      delay(100);
      FastLED.show();
      continue;
    }

    else if (FeedIn < 0 && i < -feedInMapped + solarHomeUsage) {
      leds[i] = CRGB(255, 20, 0);
      delay(100);
      FastLED.show();  // These seperate .show()s are because of some sort of bug with ESP8266.
    }

    else if (FeedIn > 0 && i < feedInMapped + solarHomeUsage) {
      leds[i] = CRGB(255, 70, 0);
      delay(100);
      FastLED.show();
    }

    else {
      leds[i] = CRGB(0, 0, 0);
      FastLED.show();
    }
  }
}
