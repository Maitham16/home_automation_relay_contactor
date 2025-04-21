#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <nvs_flash.h>

//‑‑‑ debouncer -------------------------------------------------
const uint8_t REQ_OK = 3;    // successes needed to accept ONLINE
const uint8_t REQ_FAIL = 2;  // failures needed to accept OFFLINE
uint8_t okCnt = 0, failCnt = 0;
bool masterOnline = false;  // debounced state
//----------------------------------------------------------------


// Configuration
static const char* MAIN_HOSTNAME = "mainesp.local";
#define RELAY_PIN 3
#define GREEN_LED 5
#define RED_LED 7
#define BUZZER_PIN 9

bool buzz_active = false;

WebServer server(80);
Preferences preferences;

String mainDeviceIP = "";

bool relayState = false;
bool manualOverride = false;

unsigned long previousMillis = 0;
const long interval = 1000;

// HTML Styling
String styleHeader = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Home Automation - Internal Device</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial, sans-serif; margin: 0; padding: 0; background-color: #f4f4f4;
    }
    header {
      background: #007BFF; color: #fff; padding: 20px; text-align: center;
    }
    .container {
      max-width: 900px; margin: auto; background: #fff; padding: 20px; margin-top: 20px;
      border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1);
    }
    h1, h2 { margin-top: 0; }
    .footer { margin-top: 40px; text-align: center; color: #666; font-size: 0.9em; }
    a, button {
      display: inline-block; margin: 10px 0; padding: 10px 20px;
      background: #007BFF; color: #fff; text-decoration: none; border-radius: 5px; border: none;
      cursor: pointer; font-size: 1em;
    }
    a:hover, button:hover { background: #0056b3; }
    .info { margin: 10px 0; padding: 10px; border: 1px solid #ccc; border-radius: 5px; background: #f9f9f9; line-height: 1.5; }
    .buttons { display: flex; justify-content: space-around; flex-wrap: wrap; }
    .button.off { background-color: #DC3545; }
    .button.auto { background-color: #28A745; }
    @media (max-width: 600px) {
      .buttons { flex-direction: column; }
      .button { margin-bottom: 10px; width: 100%; }
    }
  </style>
</head>
<body>
<header>
  <h1>Home Automation - Internal Device</h1>
</header>
<div class="container">
)rawliteral";

String styleFooter = R"rawliteral(
  <div class="footer">
    <p>Designed by Eng. Maitham Alrubaye & Eng. Fadi Alkhazraji</p>
    <p>All right reserved - Fabricated in 2025</p>
  </div>
</div>
</body>
</html>
)rawliteral";

// ========================
// Setup Function
// ========================
void setup() {
  Serial.begin(115200);

  delay(8000);
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);  // Default OFF
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  relayState = false;

  WiFiManager wifiManager;

  wifiManager.setConfigPortalTimeout(30);
  if (!wifiManager.autoConnect("SmartGadget-AP")) {
    Serial.println("Failed to connect or configure WiFi. Restarting...");
    ESP.restart();
  }

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  if (!MDNS.begin("devicecontrol")) {
    Serial.println("Error starting mDNS responder for devicecontrol!");
  } else {
    Serial.println("mDNS responder started with hostname: devicecontrol.local");
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", handleRoot);
  server.on("/relay", handleRelay);
  server.on("/auto", handleAuto);
  server.on("/debug", handleDebug);
  server.on("/scan", handleScanWiFi);
  server.on("/reset", handleReset);
  server.on("/connectwifi", handleConnectWiFi);
  server.begin();
}

// ========================
// Loop Function
// ========================
void loop() {
  server.handleClient();

  /* --- keep Wi‑Fi alive ------------------------------------ */
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Attempting to reconnect...");
    WiFi.reconnect();
    delay(500);
    return;  // skip the rest until re‑connected
  }

  /* --- poll master once every second ----------------------- */
  static unsigned long previousMillis = 0;
  const unsigned long interval = 1000;  // 1 s

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis < interval) return;
  previousMillis = currentMillis;

  bool nowOnline = isMainDeviceOnline();  // one raw reading

  if (nowOnline) {  // got a good reply
    okCnt++;
    failCnt = 0;
    if (!masterOnline && okCnt >= REQ_OK) {  // OFF → ON
      masterOnline = true;
      activateRelay();
    }
  } else {  // timeout / error
    failCnt++;
    okCnt = 0;
    if (masterOnline && failCnt >= REQ_FAIL) {  // ON → OFF
      masterOnline = false;
      deactivateRelay();
    }
  }
}

// ========================
// Core Functions
// ========================
bool fetchMainDeviceIP() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  if (!client.connect("mainesp.local", 80)) return false;

  client.println("GET /settings HTTP/1.1");
  client.println("Host: mainesp.local");
  client.println("Connection: close");
  client.println();

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      client.stop();
      return false;
    }
  }

  String response = "";
  while (client.available()) {
    response += client.readStringUntil('\r');
  }
  client.stop();

  int jsonStart = response.indexOf('{');
  int jsonEnd = response.lastIndexOf('}');
  if (jsonStart == -1 || jsonEnd == -1) return false;

  String jsonString = response.substring(jsonStart, jsonEnd + 1);
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) return false;

  mainDeviceIP = doc["ip"].as<String>();
  mainDeviceIP.trim();

  preferences.putString("mainDeviceIP", mainDeviceIP);

  Serial.print("Main Device IP saved to Preferences: ");
  Serial.println(mainDeviceIP);
  return true;
}

// bool isMainDeviceOnline() {
//   if (mainDeviceIP == "") return fetchMainDeviceIP();

//   WiFiClient client;
//   if (!client.connect(mainDeviceIP.c_str(), 80)) return false;

//   client.println("GET /status HTTP/1.1");
//   client.println("Host: " + mainDeviceIP);
//   client.println("Connection: close");
//   client.println();

//   unsigned long timeout = millis();
//   while (client.available() == 0) {
//     if (millis() - timeout > 5000) {
//       client.stop();
//       return false;
//     }
//   }

//   String response = "";
//   while (client.available()) {
//     response += client.readStringUntil('\r');
//   }
//   client.stop();

//   int jsonStart = response.indexOf('{');
//   int jsonEnd = response.lastIndexOf('}');
//   if (jsonStart == -1 || jsonEnd == -1) return false;

//   String jsonString = response.substring(jsonStart, jsonEnd + 1);
//   StaticJsonDocument<200> doc;
//   DeserializationError error = deserializeJson(doc, jsonString);
//   if (error) return false;

//   String powerStatus = doc["powerStatus"];
//   return powerStatus == "ON";
// }

// ========================
// Is the master reachable?
// ========================
bool isMainDeviceOnline() {
  // 1. Ensure we know the master's IP
  if (mainDeviceIP.length() == 0) {          // first boot or flushed
    if (!fetchMainDeviceIP()) return false;  // try mDNS once
  }

  // 2. Plain TCP handshake (no HTTP/JSON)
  WiFiClient client;
  client.setTimeout(1500);                   // 1.5 s connect‑timeout
  bool ok = client.connect(mainDeviceIP.c_str(), 80);
  client.stop();                             // always tidy up
  return ok;                                 // true ⇢ master present
}


void activateRelay() {
  if (!relayState) {
    relayState = true;
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, LOW);
    beepBuzzer();
    Serial.println("Relay activated.");
  }
}

void deactivateRelay() {
  if (relayState) {
    relayState = false;
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);
    Serial.println("Relay deactivated.");
  }
}

void beepBuzzer() {
  unsigned long startTime = millis();
  while (millis() - startTime < 3000) {
    tone(BUZZER_PIN, 1000);
    delay(100);
    noTone(BUZZER_PIN);
    delay(100);
  }
}

// ========================
// HTTP Handlers
// ========================
void handleRoot() {
  String modeText = manualOverride ? "Manual" : "Automatic";
  String relayText = relayState ? "ON" : "OFF";

  String html = styleHeader;
  html += R"rawliteral(
    <h2>Dashboard</h2>
    <p>This device is fully automated.</p>
    <div class="info">
      <strong>Mode:</strong> )rawliteral"
          + modeText + R"rawliteral(<br>
      <strong>Relay State:</strong> )rawliteral"
          + relayText + R"rawliteral(
    </div>
    <div class="buttons">
      <form action="/relay" method="get">
        <input type="hidden" name="state" value="ON">
        <button class="button" type="submit">Turn ON</button>
      </form>
      <form action="/relay" method="get">
        <input type="hidden" name="state" value="OFF">
        <button class="button off" type="submit">Turn OFF</button>
      </form>
      <form action="/auto" method="get">
        <button class="button auto" type="submit">Resume Automatic</button>
      </form>
    </div>
    <a href="/debug">Debug Info</a>
  )rawliteral";
  html += styleFooter;
  server.send(200, "text/html", html);
}

void handleRelay() {
  String state = server.arg("state");
  manualOverride = true;

  if (state == "ON") {
    activateRelay();
  } else if (state == "OFF") {
    deactivateRelay();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleAuto() {
  manualOverride = false;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleDebug() {
  String relayText = relayState ? "ON" : "OFF";
  String modeText = manualOverride ? "Manual" : "Automatic";

  String html = styleHeader;
  html += "<h2>Debug Information</h2>";
  html += "<div class='info'>";
  html += "<strong>Relay State:</strong> " + relayText + "<br>";
  html += "<strong>Mode:</strong> " + modeText + "<br>";
  html += "<strong>Main Device IP:</strong> " + (mainDeviceIP.isEmpty() ? "Not Available" : mainDeviceIP) + "<br>";
  html += "</div>";
  html += "<a href='/'>Back to Home</a>";
  html += styleFooter;
  server.send(200, "text/html", html);
}

void handleReset() {
  String html = styleHeader;
  html += "<h2>Factory Reset</h2>";
  html += "<p>All settings and configurations will be erased.</p>";
  html += "<p>The device will now restart and return to its factory default settings.</p>";

  html += "<a href='/'>Back to Home</a>";
  html += styleFooter;
  server.send(200, "text/html", html);

  delay(2000);

  esp_err_t result = nvs_flash_erase();
  if (result == ESP_OK) {
    Serial.println("NVS flash erased successfully!");
  } else {
    Serial.printf("Failed to erase NVS flash: %s\n", esp_err_to_name(result));
  }

  ESP.restart();
}

// ============================================================================
// Wifi Connection
//=============================================================================
void handleConnectWiFi() {
  String newSSID = server.arg("ssid");
  String newPASS = server.arg("pass");

  String html = styleHeader;
  if (newSSID == "") {
    html += "<h2>SSID not provided!</h2>";
    html += "<a href='/scan'>Back to Scan</a>";
    html += styleFooter;
    server.send(200, "text/html", html);
    return;
  }

  html += "<h2>Connecting to " + newSSID + "...</h2>";
  html += "<p>Please wait while we attempt to connect.</p>";

  WiFi.disconnect();
  delay(1000);

  // Attempt connection
  WiFi.begin(newSSID.c_str(), newPASS.c_str());
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  // Check connection status
  if (WiFi.status() == WL_CONNECTED) {
    html += "<p>Connected successfully! IP: " + WiFi.localIP().toString() + "</p>";

    WiFiManager wm;
    wm.setSaveConfigCallback([]() {
      Serial.println("WiFi credentials saved.");
    });
    wm.autoConnect(newSSID.c_str(), newPASS.c_str());
  } else {
    html += "<p>Failed to connect. Please check the credentials and try again.</p>";
  }

  html += "<a href='/'>Back to Home</a>";
  html += styleFooter;

  server.send(200, "text/html", html);
}

void handleScanWiFi() {
  int n = WiFi.scanNetworks();
  String html = styleHeader;
  html += "<h2>Available WiFi Networks</h2>";
  if (n == 0) {
    html += "<p>No networks found.</p>";
  } else {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      html += "<div class='wifi-item'>";
      html += "<strong>" + ssid + "</strong> (RSSI: " + String(rssi) + ")";
      html += R"rawliteral(
        <form style="display:inline;" action="/connectwifi" method="get">
          <input type="hidden" name="ssid" value=")rawliteral"
              + ssid + R"rawliteral(">
          <label>Password:</label>
          <input type="password" name="pass" placeholder="Enter WiFi Password">
          <button class="btn" type="submit">Connect</button>
        </form>
      )rawliteral";
      html += "</div>";
    }
  }
  html += "<a href='/'>Back to Home</a>";
  html += styleFooter;

  server.send(200, "text/html", html);
}