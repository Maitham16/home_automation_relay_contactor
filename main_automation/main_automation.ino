#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>

// Configuration
static const char* MY_MDNS_HOSTNAME = "mainesp";
WebServer server(80);

void handleRoot();
void handleStatus();
void handleSettings();
void handleDebug();
void handleReset();
void handleScanWiFi();
void handleConnectWiFi();

// HTML Styles
String styleHeader = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Home Automation - Main</title>
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
    h1, h2 {
      margin-top: 0;
    }
    .footer {
      margin-top: 40px; text-align: center; color: #666; font-size: 0.9em;
    }
    a {
      display: inline-block; margin: 10px 0; padding: 10px 20px;
      background: #007BFF; color: #fff; text-decoration: none; border-radius: 5px;
    }
    a:hover {
      background: #0056b3;
    }
    .btn {
      display: inline-block; margin: 10px 0; padding: 10px 20px; color: #fff;
      background: #007BFF; text-decoration: none; border-radius: 5px; border: none;
      cursor: pointer; font-size: 1em;
    }
    .btn:hover {
      background: #0056b3;
    }
    .info {
      margin: 10px 0; padding: 10px; border: 1px solid #ccc;
      border-radius: 5px; background: #f9f9f9; line-height: 1.5;
    }
    .wifi-item {
      margin: 5px 0; padding: 8px; border: 1px solid #ccc; border-radius: 5px;
      background: #fff;
    }
    input[type=text], input[type=password] {
      width: 80%; padding: 8px; margin: 5px 0; border: 1px solid #ccc;
      border-radius: 4px;
    }
    label {
      display: inline-block; width: 100px;
    }
  </style>
</head>
<body>
<header>
  <h1>Home Automation - Main</h1>
</header>
<div class="container">
)rawliteral";

String styleFooter = R"rawliteral(
  <div class="footer">
    <p>Designed by Eng. Maitham Alrubaye and Eng. Fadi Alkhazraji</p>
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

  WiFiManager wifiManager;
  if (!wifiManager.autoConnect("Main-SmartGadget-AP")) {
    ESP.restart();
  }

  if (!MDNS.begin(MY_MDNS_HOSTNAME)) {
    Serial.println("Error starting mDNS responder!");
  } else {
    Serial.println("mDNS responder started with hostname: mainesp.local");
  }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/settings", handleSettings);
  server.on("/debug", handleDebug);
  server.on("/reset", handleReset);
  server.on("/scan", handleScanWiFi);
  server.on("/connectwifi", handleConnectWiFi);
  server.begin();

  Serial.println("HTTP server started");
}

// ========================
// Loop Function
// ========================
void loop() {
  server.handleClient();
}

// ========================
// Handler Functions
// ========================

// Root Page
void handleRoot() {
  String html = styleHeader;
  html += R"rawliteral(
    <h2>Device Overview</h2>
    <p>The main device is online and ready.</p>
    <p><strong>mDNS Hostname:</strong> mainesp.local</p>
    <p><strong>OTA Hostname:</strong> mainesp-ota</p>
    <a href="/debug">Debug Page</a>
    <a href="/status">Check Power Status</a>
    <a href="/settings">View Network Settings</a>
    <a href="/scan">Scan WiFi</a>
    <a href="/reset">Reset WiFi Credentials</a>
  )rawliteral";
  html += styleFooter;
  server.send(200, "text/html", html);
}

void handleStatus() {
  String powerStatus = "ON";
  String json = "{";
  json += "\"powerStatus\":\"" + powerStatus + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleSettings() {
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleDebug() {
  String ipStr = WiFi.localIP().toString();
  String hostnameStr = MY_MDNS_HOSTNAME;

  String wifiStat;
  wl_status_t status = WiFi.status();
  switch(status) {
    case WL_CONNECTED: wifiStat = "CONNECTED"; break;
    case WL_NO_SSID_AVAIL: wifiStat = "NO_SSID_AVAIL"; break;
    case WL_CONNECT_FAILED: wifiStat = "CONNECT_FAILED"; break;
    case WL_IDLE_STATUS: wifiStat = "IDLE_STATUS"; break;
    case WL_DISCONNECTED: wifiStat = "DISCONNECTED"; break;
    default: wifiStat = "UNKNOWN";
  }

  String html = styleHeader;
  html += "<h2>Debug Information</h2>";
  html += "<div class='info'>";
  html += "<strong>WiFi Status:</strong> " + wifiStat + "<br>";
  html += "<strong>Local IP:</strong> " + ipStr + "<br>";
  html += "<strong>mDNS Hostname:</strong> " + hostnameStr + "<br>";
  html += "<strong>OTA Hostname:</strong> mainesp-ota<br>";
  html += "</div>";
  html += "<a href='/'>Back to Home</a>";
  html += styleFooter;

  server.send(200, "text/html", html);
}

void handleReset() {
  String html = styleHeader;
  html += R"rawliteral(
    <h2>Reset WiFi Credentials</h2>
    <p>Are you sure you want to reset WiFi credentials?</p>
    <form action="/reset" method="post">
      <button class="btn" type="submit">RESET NOW</button>
    </form>
  )rawliteral";
  html += styleFooter;

  if (server.method() == HTTP_POST) {
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    server.sendHeader("Location", "/");
    server.send(303);
    delay(500);
    ESP.restart();
    return;
  }

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
          <input type="hidden" name="ssid" value=")rawliteral" + ssid + R"rawliteral(">
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

  WiFi.begin(newSSID.c_str(), newPASS.c_str());

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    html += "<p>Connected successfully! IP: " + WiFi.localIP().toString() + "</p>";
  } else {
    html += "<p>Failed to connect.</p>";
  }

  html += "<a href='/'>Back to Home</a>";
  html += styleFooter;

  server.send(200, "text/html", html);
}