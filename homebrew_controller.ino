#include <DNSServer.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <WebServer.h>
#include <WiFi.h>
#include "SPIFFS.h"

const byte DNS_PORT = 53;
const char *ssid = "HOMEBREW";
const char *password = "12345678";

IPAddress apIP(192, 168, 88, 1);
DNSServer dnsServer;
WebServer webServer(80);

#define ONE_WIRE_BUS 14 // GPIO where DS18B20 data line is connected
#define RELAY_PIN 12    // GPIO where relay is connected

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensor1; // Variables to hold sensor addresses


long prevLoop = 0;
int freezerMode = 1; // 0=off, 1=on

float temp = 0.0;
float tempTarget = 2.0;
float offset = 1.0;
float minTemp = tempTarget - offset;
float maxTemp = tempTarget + offset;
int relayState = LOW;

long prevOffTime = 0;
long now = 0;
long waitTime = 5 * 60 * 1000; // 5 minutes in milis
int waintingMinutes = 0;

String htmlContent = "";

void handleRoot() {
  webServer.send( 200, "text/html", htmlContent );
}

void readTemp() {

  sensors.requestTemperatures(); // Request temp readings from all sensors

  float tempSensor = sensors.getTempC(sensor1);

  if (tempSensor != DEVICE_DISCONNECTED_C) {
    temp = tempSensor;
    //   Serial.print("Sensor 1: ");
    //   Serial.print(tempSensor, 2); // Print with 2 decimals
    //   Serial.println(" °C");
  }
}

void freezer() {
  // when millis() overflows it restarts from 0, prevOffTime is in the future
  if (prevOffTime > now) {
    prevOffTime = 0;
  }

  long ellapsed = now - prevOffTime;
  long wait = waitTime - ellapsed;

   waitingMinutes = 0;

  if (temp < minTemp && relayState == HIGH) {
    relayState = LOW;
    prevOffTime = now;
  } else if (temp > maxTemp && relayState == LOW) {
    if (ellapsed > waitTime) {
      relayState = HIGH;
    } else {
      waitingMinutes = ceil(float(wait) / 1000.0 / 60.0);
    }
  }

  Serial.print("Target: ");
  Serial.print(tempTarget, 2);
  Serial.print("°C | ");
  Serial.print("Offset: ");
  Serial.print(offset, 2);
  Serial.print("°C | ");
  Serial.print("Current: ");
  Serial.print(temp, 2);
  Serial.print("°C | ");
  // Serial.print("min: ");
  // Serial.print(minTemp, 2);
  // Serial.print("°C, max: ");
  // Serial.print(maxTemp, 2);
  // Serial.print("°C | ");
  Serial.print("Relay: ");
  Serial.print(relayState == HIGH ? "ON" : "OFF");
  if (waitingMinutes > 0) {
    Serial.print(" (turning ON in: ");
    Serial.print(waitingMinutes);
    Serial.print(" min)");
  }
  Serial.println();

  digitalWrite(RELAY_PIN, relayState);
}

void boiler() {
  if (temp > maxTemp && relayState == HIGH) {
    relayState = LOW;
  } else if (temp < minTemp && relayState == LOW) {
    relayState = HIGH;
  }
  Serial.print("Target: ");
  Serial.print(tempTarget, 2);
  Serial.print("°C | ");
  Serial.print("Offset: ");
  Serial.print(offset, 2);
  Serial.print("°C | ");
  Serial.print("Current: ");
  Serial.print(temp, 2);
  Serial.print("°C | ");
  // Serial.print("min: ");
  // Serial.print(minTemp, 2);
  // Serial.print("°C, max: ");
  // Serial.print(maxTemp, 2);
  // Serial.print("°C | ");
  Serial.print("Relay: ");
  Serial.print(relayState == HIGH ? "ON" : "OFF");
  Serial.println();
  digitalWrite(RELAY_PIN, relayState);
}

String generateJSON() {
  String json = "{";
  json += "\"mode\":" + String(freezerMode) + ",";
  json += "\"target\":" + String(tempTarget, 2) + ",";
  json += "\"temp\":" + String(temp, 2) + ",";
  json += "\"offset\":" + String(offset, 2) + ",";
  json += "\"relay\":" + String(relayState);
  json += "\"wait\":" + String(waitingMinutes);
  json += "}";

  return json;
}

void setup() {
  Serial.begin(19200);

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }

  // Read HTML file into a string
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }

  htmlContent = file.readString();
  file.close();

  sensors.begin();

  if (!sensors.getAddress(sensor1, 0)) {
    Serial.println("Sensor 1 not found!");
  }

  // Optionally set resolution for accuracy (9 to 12 bits)
  // this sets how precise the temperature readings are
  // 9 bits = 0.5°C, 10 bits = 0.25°C, 11 bits = 0.125°C, 12 bits = 0.0625°C
  sensors.setResolution(sensor1, 9);

  // Set ESP32 as Access Point
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  Serial.println("Access Point started");
  Serial.print("IP address: ");
  Serial.println(apIP);

  // Start DNS server to redirect all queries to ESP32 IP (captive portal)
  dnsServer.start(DNS_PORT, "*", apIP);

  // Start web server routes
  webServer.onNotFound([]() {
    // Redirect all requests to root page
    webServer.sendHeader("Location", String("http://") + apIP.toString(), true);
    webServer.send(302, "text/plain", "");
  });

  webServer.on("/", handleRoot);

  webServer.on("/data", HTTP_GET, []() {
    String json = generateJSON();
    webServer.send(200, "application/json", json);
  });

  webServer.on("/offset/inc", HTTP_GET, []() {
    offset += 0.5;
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.on("/offset/dec", HTTP_GET, []() {
    if (offset > 0.5) {
      offset -= 0.5;
    }
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.on("/target/inc", HTTP_GET, []() {
    tempTarget += 0.5;
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.on("/target/dec", HTTP_GET, []() {
    tempTarget -= 0.5;
    webServer.send(200, "application/json", generateJSON());
  });

  webserver.on("/mode/toggle", HTTP_GET, []() {
    freezerMode = 1 - freezerMode;
    relayState = LOW; // turn off relay when mode changes
    prevOffTime = millis();
    digitalWrite(RELAY_PIN, relayState);
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.begin();
}

void loop() {
  now = millis();

  if(now - prevLoop > 1000) {
    if(now < prevLoop) {
      prevLoop = 0;
    }

    readTemp();

    if(freezerMode == 1) {
      freezer();
    } else {
      boiler();
    }

    prevLoop = now;
  }

  dnsServer.processNextRequest();
  webServer.handleClient();
}
