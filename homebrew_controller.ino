#include <ArduinoJson.h>
#include <DNSServer.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>

Preferences preferences;

const byte DNS_PORT = 53;
const char *ssid = "HOMEBREW";
const char *password = "12345678";

IPAddress apIP(192, 168, 88, 1);
DNSServer dnsServer;
WebServer webServer(80);

#define ONE_WIRE_BUS 33 // GPIO where DS18B20 data line is connected
#define RELAY_PIN 23    // GPIO where relay is connected

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensor1; // Variables to hold sensor addresses

// setting variables
int freezerMode = 0; // 0=off, 1=on
float tempTarget = 60.0;
float offset = 1.0;

// runtime variables
long now = 0;
long waitTime = 5 * 60 * 1000; // 5 minutes in milis
int waitingMinutes = 0;
long prevOffTime = 0;
long prevLoop = 0;
float temp = 0.0;
float minTemp = tempTarget - offset;
float maxTemp = tempTarget + offset;
int relayState = LOW;


void handleRoot() {
  webServer.send(
      200, "text/html",
      "<html>"
      "<head>"
      "  <title>HOMBREW</title>"
      "</head>"
      "<body>"
      "  <h1>HOMEBREW</h1>"
      "  <p><span id=\"mode\">--</span> <button "
      "id=\"modeBtn\">mode</button></p>"
      "  <p>"
      "    Target: <span id=\"target\">--</span> &deg;C"
      "    <button id=\"incTarget\">+</button>"
      "    <button id=\"decTarget\">-</button>"
      "    <input style=\"margin-left=10px;\" type=\"text\" id=\"targetInput\" "
      "/>"
      "    <button id=\"updateTarget\">update</button>"
      "  </p>"
      "  <p>"
      "    Offset: <span id=\"offset\">--</span> &deg;C"
      "    <button id=\"incOffset\">+</button>"
      "    <button id=\"decOffset\">-</button>"
      "  </p>"
      "  <p>Current: <span id=\"temp\">--</span> &deg;C</p>"
      "  <p>Relay: <span id=\"relay\">--</span> &deg;C</p>"
      "  <p style=\"display : none\">Wait: <span id=\"wait\">0</span> min</p>"
      "  <script>"
      "    let mode = 'Freezer';"
      "    let target = 0;"
      "    let temp = 0;"
      "    let offset = 0;"
      "    let relay = 'OFF';"
      "    let wait = 0;"
      "    const modeElement = document.getElementById('mode');"
      "    const targetElement = document.getElementById('target');"
      "    const tempElement = document.getElementById('temp');"
      "    const offsetElement = document.getElementById('offset');"
      "    const relayElement = document.getElementById('relay');"
      "    const waitElement = document.getElementById('wait');"
      ""
      "    const postData = async (url, data) => {"
      "      const response = await fetch(url, { method: \"POST\", body: "
      "JSON.stringify(data) });"
      "      if (!response.ok) {"
      "        console.error('Failed', url, data);"
      "        return;"
      "      }"
      "    };"
      ""
      "    const fetchData = async (url) => {"
      "      const response = await fetch(url);"
      "      if (!response.ok) {"
      "        console.error('Failed', url);"
      "        return;"
      "      }"
      "      const data = await response.json();"
      "      mode = data.mode === 1 ? 'Freezer' : 'Boiler';"
      "      target = data.target ?? 0;"
      "      temp = data.temp ?? 0;"
      "      offset = data.offset ?? 0;"
      "      relay = data.relay === 1 ? 'ON' : 'OFF';"
      "      wait = data.wait ?? 0;"
      "      console.log({ mode, target, temp, offset, relay, wait });"
      "      modeElement.textContent = mode;"
      "      targetElement.textContent = target;"
      "      tempElement.textContent = temp;"
      "      offsetElement.textContent = offset;"
      "      relayElement.textContent = relay;"
      "      if (!wait || mode === 'Boiler') {"
      "        waitElement.parentElement.style.display = 'none';"
      "      } else {"
      "        waitElement.parentElement.style.display = 'block';"
      "        waitElement.textContent = wait;"
      "      }"
      "    };"
      ""
      "    const incTargetBtn = document.getElementById('incTarget');"
      "    const decTargetBtn = document.getElementById('decTarget');"
      "    const incOffsetBtn = document.getElementById('incOffset');"
      "    const decOffsetBtn = document.getElementById('decOffset');"
      "    const modeBtn = document.getElementById('modeBtn');"
      "    const updateTarget = document.getElementById('updateTarget');"
      "    const targetInput = document.getElementById('targetInput');"
      ""
      "    incOffsetBtn.onclick = async () => {"
      "      fetchData('/offset/inc');"
      "    };"
      ""
      "    decOffsetBtn.onclick = async () => {"
      "      fetchData('/offset/dec');"
      "    };"
      ""
      "    incTargetBtn.onclick = async () => {"
      "      fetchData('/target/inc');"
      "    };"
      ""
      "    decTargetBtn.onclick = async () => {"
      "      fetchData('/target/dec');"
      "    };"
      ""
      "    modeBtn.onclick = async () => {"
      "      fetchData('/mode/toggle');"
      "    };"
      ""
      "    updateTarget.onclick = async () => {"
      "      const newValue = targetInput.value;"
      "      await postData('/target/update', {target: newValue});"
      "      targetInput.value = '';"
      "    };"
      ""
      "    setInterval(() => {"
      "      fetchData('/data');"
      "    }, 1000);"
      "  </script>"
      "</body>"
      "</html>");
}

void readTemp() {
  sensors.requestTemperatures(); // Request temp readings from all sensors
  float tempSensor = sensors.getTempC(sensor1);
  if (tempSensor != DEVICE_DISCONNECTED_C) {
    temp = tempSensor;
  }
}

void updateMinMax() {
  minTemp = tempTarget - offset;
  maxTemp = tempTarget + offset;
}

void setMode(int newMode){
  freezerMode = newMode;
  relayState = LOW; // turn off relay when mode changes
  digitalWrite(RELAY_PIN, relayState);
  preferences.begin("appdata", false); // Open for write
  preferences.putInt("mode", freezerMode);    // Save new mode
  preferences.end();
  prevOffTime = millis();
}

void setTarget(float target){
  tempTarget = target;
  updateMinMax();
  preferences.begin("appdata", false); // Open for write
  preferences.putFloat("target", tempTarget);    // Save new target
  preferences.end();
}

void setOffset(float newOffset){
  offset = newOffset;
  updateMinMax();
  preferences.begin("appdata", false); // Open for write
  preferences.putFloat("offset", offset);    // Save new offset
  preferences.end();
}


void freezer() {
  // when millis() overflows it restarts from 0, prevOffTime is in the future
  if (prevOffTime > now) {
    prevOffTime = 0;
  }

  long ellapsed = now - prevOffTime;
  long wait = waitTime - ellapsed;

  waitingMinutes = 0;

  // we use the max as threshold to turn off the relay, probably the temp will sill go down for a few minutes
  if (temp <= maxTemp && relayState == HIGH) {
    relayState = LOW;
    prevOffTime = now;
  } else if (temp > maxTemp && relayState == LOW) {
    if (ellapsed > waitTime) {
      relayState = HIGH;
    } else {
      waitingMinutes = ceil(float(wait) / 1000.0 / 60.0);
    }
  }

  Serial.print("Mode: ");
  Serial.print("Freezer");
  Serial.print(" | ");
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

  Serial.print("Mode: ");
  Serial.print("Boiler");
  Serial.print(" | ");
  Serial.print("Target: ");
  Serial.print(tempTarget, 2);
  Serial.print("°C | ");
  Serial.print("Offset: ");
  Serial.print(offset, 2);
  Serial.print("°C | ");
  Serial.print("Current: ");
  Serial.print(temp, 2);
  Serial.print("°C | ");
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
  json += "\"relay\":" + String(relayState) + ",";
  json += "\"wait\":" + String(waitingMinutes);
  json += "}";

  return json;
}

void setup() {
  // serial -----------------------------------------
  Serial.begin(19200);
  // pins -------------------------------------------
  pinMode(RELAY_PIN, OUTPUT);
  // preferences ------------------------------------
  preferences.begin("appdata", false);
  freezerMode = preferences.getInt("mode", 0); // Get persisted mode (default 0)
  tempTarget = preferences.getFloat("target", 60.0); // Get persisted target (default 60.0)
  offset = preferences.getFloat("offset", 1.0); // Get persisted offset (default 1.0)
  preferences.end();

  updateMinMax();


  // sensors ----------------------------------------
  sensors.begin();
  if (!sensors.getAddress(sensor1, 0)) {
    Serial.println("Sensor 1 not found!");
  }
  // Optionally set resolution for accuracy (9 to 12 bits)
  // this sets how precise the temperature readings are
  // 9 bits = 0.5°C, 10 bits = 0.25°C, 11 bits = 0.125°C, 12 bits = 0.0625°C
  sensors.setResolution(sensor1, 9);
  // wifi -------------------------------------------
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
    setOffset(offset + 0.5);
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.on("/offset/dec", HTTP_GET, []() {
    if (offset > 0.5) {
      setOffset(offset - 0.5);
    }
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.on("/target/inc", HTTP_GET, []() {
    setTarget(tempTarget + 0.5);
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.on("/target/dec", HTTP_GET, []() {
    setTarget(tempTarget - 0.5);
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.on("/target/update", HTTP_POST, []() {
    if (webServer.hasArg("plain") == false) { // Check if body received
      webServer.send(400, "application/json", "{\"error\":\"Body missing\"}");
      return;
    }

    String body = webServer.arg("plain");
    StaticJsonDocument<200> jsonDoc; // Use ArduinoJson library

    DeserializationError error = deserializeJson(jsonDoc, body);
    if (error) {
      webServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    if (!jsonDoc.containsKey("target")) {
      webServer.send(400, "application/json",
                     "{\"error\":\"Missing target field\"}");
      return;
    }

    float newTarget = jsonDoc["target"];
    setTarget(newTarget);

    webServer.send(200, "application/json", generateJSON());
  });

  webServer.on("/mode/toggle", HTTP_GET, []() {
    setMode(1 - freezerMode);
    webServer.send(200, "application/json", generateJSON());
  });

  webServer.begin();
}


void loop() {
  now = millis();

  if (now - prevLoop > 1000) {
    if (now < prevLoop) {
      prevLoop = 0;
    }

    readTemp();

    if (freezerMode == 1) {
      freezer();
    } else {
      boiler();
    }

    prevLoop = now;
  }

  dnsServer.processNextRequest();
  webServer.handleClient();
}
