#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <Servo.h>

Servo s1;  

const char* AP_SSID = "ESP8266_AP";
const char* AP_PASS = "12345678";

bool feeding = false;
bool waitingForWiFi = false;

const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer server(80);

struct TimeEntry {
  String time;
  String status;
};

TimeEntry timeEntries[50];
int timeCount = 0;
unsigned long lastCheck = 0;
const unsigned long CHECK_INTERVAL = 60000;

String wifiSSID = "";
String wifiPASS = "";
bool wifiConnected = false;

int getHourFromTimeString(String timeStr) {
  int tIndex = timeStr.indexOf('T');
  if (tIndex != -1) {
    String hourStr = timeStr.substring(tIndex + 1, tIndex + 3);
    return hourStr.toInt();
  }
  return -1;
}

bool isTimeMatching(String savedTime, struct tm* currentTime) {
  int year, month, day, hour, minute;
  sscanf(savedTime.c_str(), "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute);
  return (currentTime->tm_hour == hour && currentTime->tm_min == minute);
}

void checkTimeMatch() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  Serial.printf("Current time: %02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min);
  for (int i = 0; i < timeCount; i++) {
    Serial.printf("Checking time entry %d: %s\n", i, timeEntries[i].time.c_str());
    if (isTimeMatching(timeEntries[i].time, &timeinfo)) {
      timeEntries[i].status = "succeed";
      doSomething();
    } else {
      timeEntries[i].status = "waiting";
    }
  }
}

void saveTimeEntry(String entry) {
  if (timeCount < 50) {
    if (entry.indexOf('T') != -1 && entry.length() >= 16) {
      timeEntries[timeCount++] = {entry, "waiting"};
      Serial.printf("Saved new time entry: %s\n", entry.c_str());
    } else {
      Serial.println("Invalid time format");
    }
  }
}

void deleteTimeEntries(const bool* deleteFlags) {
  int newCount = 0;
  for (int i = 0; i < timeCount; i++) {
    if (!deleteFlags[i]) {
      timeEntries[newCount++] = timeEntries[i];
    }
  }
  timeCount = newCount;
}

void saveWiFiConfig() {
  File file = LittleFS.open("/wifi.json", "w");
  if (file) {
    StaticJsonDocument<256> doc;
    doc["ssid"] = wifiSSID;
    doc["pass"] = wifiPASS;
    serializeJson(doc, file);
    file.close();
    Serial.println("WiFi config saved");
  } else {
    Serial.println("Failed to save WiFi config");
  }
}

void loadWiFiConfig() {
  if (LittleFS.exists("/wifi.json")) {
    File file = LittleFS.open("/wifi.json", "r");
    if (file) {
      StaticJsonDocument<256> doc;
      deserializeJson(doc, file);
      wifiSSID = doc["ssid"].as<String>();
      wifiPASS = doc["pass"].as<String>();
      file.close();
      connectToWiFi(wifiSSID.c_str(), wifiPASS.c_str());
    }
  }
}

void doSomething() {
  feeding = true;
  Serial.println("=============================");
  Serial.println("TIME MATCH DETECTED!");
  Serial.println("EXECUTING ACTION!");
  Serial.println("=============================");
  s1.write(0);
  delay(1000);
  s1.write(90);
  delay(1000);
  s1.write(180);
  delay(1000);
  s1.write(90);
  delay(1000);
  s1.write(0);
  feeding = false;
}

void setupTime() {
  configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for time sync...");
  int retries = 0;
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo) && retries < 20) {
    Serial.print(".");
    delay(1500);
    retries++;
  }
  if (retries >= 20) {
    Serial.println("\nFailed to obtain time");
  } else {
    Serial.println("\nTime synchronized");
  }
}

void connectToWiFi(const char* ssid, const char* password) {
  WiFi.begin(ssid, password);
  waitingForWiFi = true;
  Serial.print("Connecting to WiFi");
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    waitingForWiFi = false;
    Serial.println("\nConnected to WiFi");
    setupTime();
  } else {
    wifiConnected = false;
    waitingForWiFi = false;
    Serial.println("\nWiFi Connection Failed");
  }
}

void setup() {
  s1.attach(0);
  Serial.begin(115200);
  LittleFS.begin();

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getHTML());
  });

  server.on("/addTime", HTTP_POST, []() {
    if (server.hasArg("time")) {
      saveTimeEntry(server.arg("time"));
      server.send(200, "text/plain", "Time entry added");
    }
  });

  server.on("/deleteTimes", HTTP_POST, []() {
    bool deleteFlags[50] = {false};
    for (int i = 0; i < timeCount; i++) {
      if (server.hasArg("delete" + String(i))) {
        deleteFlags[i] = true;
      }
    }
    deleteTimeEntries(deleteFlags);
    server.send(200, "text/plain", "Time entries deleted");
  });

  server.on("/getTimes", HTTP_GET, []() {
    String json = "[";
    for (int i = 0; i < timeCount; i++) {
      if (i > 0) json += ",";
      json += "{\"time\":\"" + timeEntries[i].time + "\",\"status\":\"" + timeEntries[i].status + "\"}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/wifiConfig", HTTP_POST, []() {
    if (server.hasArg("ssid") && server.hasArg("pass")) {
      wifiSSID = server.arg("ssid");
      wifiPASS = server.arg("pass");
      saveWiFiConfig();
      connectToWiFi(wifiSSID.c_str(), wifiPASS.c_str());
      server.send(200, "text/plain", wifiConnected ? "WiFi Connected" : "WiFi Connection Failed");
    }
  });

  server.begin();
  loadWiFiConfig();
  setupTime();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long currentMillis = millis();
  if (currentMillis - lastCheck >= CHECK_INTERVAL) {
    lastCheck = currentMillis;
    checkTimeMatch();
  }
}

String getHTML() {
  String html = "<!DOCTYPE html><html><head><title>ESP8266 Captive Portal</title>";
  html += "<style>body { font-family: Arial; text-align: center; padding: 20px; background-color: #f0f0f0; }";
  html += "table { width: 80%; margin: auto; border-collapse: collapse; }";
  html += "th, td { border: 1px solid #ddd; padding: 8px; }";
  html += "th { background-color: #4CAF50; color: white; }";
  html += "input, button { padding: 10px; margin: 5px; border-radius: 5px; border: 1px solid #ccc; }";
  html += "button { cursor: pointer; background-color: #4CAF50; color: white; border: none; }";
  html += "button:hover { background-color: #45a049; }";
  html += "h1 { color: #333; }";
  html += ".status { margin: 20px; padding: 10px; border-radius: 5px; color: white; }";
  html += ".feeding { background-color: #e74c3c; }";
  html += ".waiting { background-color: #f39c12; }";
  html += "</style>";

  html += "<script>function addTime() {";
  html += "  const timeInput = document.getElementById('time');";
  html += "  if (!timeInput.value) { alert('Please select a date and time'); return; }";
  html += "  fetch('/addTime', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'time=' + encodeURIComponent(timeInput.value) })";
  html += "    .then(response => response.ok ? loadTimes() : alert('Failed to add time'))";
  html += "    .catch(error => console.error('Error:', error));";
  html += "}";

  html += "function loadTimes() {";
  html += "  fetch('/getTimes')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      const table = document.getElementById('timeTable');";
  html += "      table.innerHTML = '<tr><th>Select</th><th>#</th><th>Date & Time</th><th>Status</th></tr>';";
  html += "      data.forEach((entry, index) => {";
  html += "        table.innerHTML += `<tr><td><input type='checkbox' id='delete${index}' name='delete${index}'></td><td>${index + 1}</td><td>${new Date(entry.time).toLocaleString()}</td><td>${entry.status}</td></tr>`;";
  html += "      });";
  html += "    })";
  html += "    .catch(error => console.error('Error:', error));";
  html += "}";

  html += "function deleteTimes() {";
  html += "  const checkboxes = document.querySelectorAll('input[type=checkbox]');";
  html += "  checkboxes.forEach((checkbox, index) => {";
  html += "    if (checkbox.checked) {";
  html += "      fetch('/deleteTimes', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'delete' + index + '=true' });";
  html += "    }";
  html += "  });";
  html += "  loadTimes();"; // Reload the times after deletion
  html += "}";

  html += "function configureWiFi() {";
  html += "  const ssid = document.getElementById('ssid').value;";
  html += "  const pass = document.getElementById('pass').value;";
  html += "  fetch('/wifiConfig', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass) })";
  html += "    .then(response => response.text())";
  html += "    .then(data => alert(data))";
  html += "    .catch(error => console.error('Error:', error));";
  html += "}";

  html += "window.onload = loadTimes;";
  html += "</script>";

  html += "</head><body>";
  html += "<h1>ESP8266 Captive Portal</h1>";

  if (feeding) {
    html += "<div class='status feeding'>Feeding in process</div>";
  }

  if (waitingForWiFi) {
    html += "<div class='status waiting'>Waiting to connect...</div>";
  }

  html += "<div><input type='datetime-local' id='time'><button onclick='addTime()'>Add Time</button></div>";
  html += "<h2>Time Entries</h2><table id='timeTable'></table>";
  html += "<h2>Feeding time depends to hour and minute</h2>";
  html += "<button onclick='deleteTimes()'>Delete Selected Times</button>";
  html += "<h2>WiFi Configuration</h2>";
  html += "<div><input type='text' id='ssid' placeholder='SSID'><input type='password' id='pass' placeholder='Password'><button onclick='configureWiFi()'>Connect WiFi</button> </div>";
  html += "</body></html>";

  return html;
}
