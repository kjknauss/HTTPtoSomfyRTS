#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <EEPROMRollingCodeStorage.h>
#include <SomfyRemote.h>
#include <WiFiManager.h>  // WiFiManager library

// ============================================
// CC1101 PINS - MATCHES YOUR VERIFIED PINOUT
// ============================================
#define EMITTER_GPIO D2      // Pin 3 (GDO0) - Purple wire
#define CC1101_CSN D8        // Pin 4 (CSN) - Orange wire
#define CC1101_SCK D5        // Pin 5 (SCK) - Yellow wire
#define CC1101_MOSI D7       // Pin 6 (MOSI) - Blue wire
#define CC1101_MISO D6       // Pin 7 (MISO) - Green wire
// Pin 1 (GND) → GND (Black wire)
// Pin 2 (VCC) → 3V3 (Red wire)
// Pin 8 (GDO2) → Not connected

// ============================================
// SOMFY CONFIGURATION
// ============================================
#define EEPROM_ADDRESS 0
#define REMOTE 0x5184c8           // Your remote ID
#define CC1101_FREQUENCY 433.42   // Somfy RTS frequency

// ============================================
// OBJECTS
// ============================================
EEPROMRollingCodeStorage rollingCodeStorage(EEPROM_ADDRESS);
SomfyRemote somfyRemote(EMITTER_GPIO, REMOTE, &rollingCodeStorage);
ESP8266WebServer server(80);
WiFiManager wifiManager;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n╔════════════════════════════════════════╗");
  Serial.println("║     SOMFY RTS CONTROLLER v2.1         ║");
  Serial.println("║   HTTP to RTS Bridge for OpenHAB      ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Initialize Somfy remote
  Serial.print("[1/5] Initializing Somfy Remote... ");
  somfyRemote.setup();
  Serial.println("✓ Done");
  
  // Configure SPI pins
  Serial.print("[2/5] Configuring SPI pins... ");
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CSN);
  Serial.println("✓ Done");
  
  // Initialize CC1101
  Serial.print("[3/5] Initializing CC1101 Radio... ");
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(CC1101_FREQUENCY);
  
  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("✓ Done");
    Serial.println("      Frequency: 433.42 MHz");
    Serial.print("      CC1101 Version: 0x");
    Serial.println(ELECHOUSE_cc1101.SpiReadStatus(CC1101_VERSION), HEX);
  } else {
    Serial.println("✗ FAILED!");
    Serial.println("      CC1101 not responding - check wiring");
    while(1) { delay(1000); }  // Halt
  }
  
  // Initialize EEPROM
  Serial.print("[4/5] Initializing EEPROM... ");
  EEPROM.begin(4);
  Serial.println("✓ Done");
  
  // WiFi Configuration with WiFiManager
  Serial.println("[5/5] Connecting to WiFi...");
  Serial.println("      If not configured, will start config portal");
  
  // Customize WiFiManager
  wifiManager.setConfigPortalTimeout(180);  // 3 minute timeout
  wifiManager.setAPCallback(configModeCallback);
  
  // Try to connect, if fails, start config portal
  if (!wifiManager.autoConnect("Somfy-RTS-Setup")) {
    Serial.println("✗ Failed to connect and timeout occurred");
    Serial.println("ℹ Restarting device...");
    delay(3000);
    ESP.restart();
  }
  
  // Connected!
  Serial.println("✓ Connected!");
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║           SYSTEM READY!                ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("\n🌐 Web Interface: http://");
  Serial.println(WiFi.localIP());
  Serial.print("📶 WiFi Network: ");
  Serial.println(WiFi.SSID());
  Serial.print("📡 Signal Strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("🆔 Remote ID: 0x");
  Serial.println(REMOTE, HEX);
  Serial.println("\n📡 Available Commands:");
  Serial.println("   • /up    - Open awning");
  Serial.println("   • /down  - Close awning");
  Serial.println("   • /stop  - Stop awning");
  Serial.println("   • /prog  - Pair with awning");
  Serial.println("\n🔗 For OpenHAB integration, use:");
  Serial.print("   http://");
  Serial.print(WiFi.localIP());
  Serial.println("/[command]");
  Serial.println("\n🔄 To reconfigure WiFi:");
  Serial.println("   Visit http://");
  Serial.print(WiFi.localIP());
  Serial.println("/reset");
  Serial.println("\n" + String('─', 42) + "\n");
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/up", handleUp);
  server.on("/down", handleDown);
  server.on("/stop", handleStop);
  server.on("/prog", handleProg);
  server.on("/status", handleStatus);
  server.on("/reset", handleReset);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("✓ HTTP server started and listening...\n");
}

void loop() {
  server.handleClient();
  
  // WiFi connection monitor - auto-reconnect if lost
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000) {  // Check every 30 seconds
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠ WiFi connection lost! Attempting to reconnect...");
      WiFi.reconnect();
    }
  }
}

// Callback when entering config mode
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║      CONFIGURATION MODE ACTIVE         ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("\n📱 Connect to WiFi network:");
  Serial.println("   SSID: Somfy-RTS-Setup");
  Serial.println("   Password: (none - open network)");
  Serial.println("\n🌐 Configuration page will open automatically");
  Serial.print("   Or visit: http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("\n⏱ Timeout: 3 minutes");
  Serial.println("\nℹ Device will restart if no configuration is made\n");
}

// ============================================
// SEND COMMAND
// ============================================
void sendCC1101Command(Command command) {
  const char* commandName;
  switch(command) {
    case Command::Up: commandName = "UP (OPEN)"; break;
    case Command::Down: commandName = "DOWN (CLOSE)"; break;
    case Command::My: commandName = "MY (STOP)"; break;
    case Command::Prog: commandName = "PROG (PAIR)"; break;
    default: commandName = "UNKNOWN";
  }
  
  Serial.println("╔════════════════════════════════════════╗");
  Serial.print("║ Sending: ");
  Serial.print(commandName);
  for(int i = 0; i < 26 - strlen(commandName); i++) Serial.print(" ");
  Serial.println("║");
  Serial.println("╚════════════════════════════════════════╝");
  
  // Transmit via CC1101
  ELECHOUSE_cc1101.SetTx();
  somfyRemote.sendCommand(command);
  ELECHOUSE_cc1101.setSidle();
  
  Serial.println("✓ Command transmitted successfully\n");
}

// ============================================
// WEB HANDLERS
// ============================================

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Somfy RTS Controller</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { 
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .container {
      background: white;
      border-radius: 20px;
      padding: 40px;
      max-width: 500px;
      width: 100%;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
    }
    h1 {
      color: #333;
      text-align: center;
      margin-bottom: 10px;
      font-size: 28px;
      font-weight: 700;
    }
    .subtitle {
      text-align: center;
      color: #666;
      margin-bottom: 30px;
      font-size: 14px;
    }
    .info-box {
      background: #f8f9fa;
      border-left: 4px solid #667eea;
      padding: 15px;
      margin-bottom: 30px;
      border-radius: 5px;
      font-size: 13px;
    }
    .info-box p {
      margin: 5px 0;
      color: #555;
    }
    .info-box strong {
      color: #333;
    }
    .control-section {
      margin-bottom: 30px;
    }
    .section-title {
      color: #333;
      font-size: 18px;
      font-weight: 600;
      margin-bottom: 15px;
      padding-bottom: 10px;
      border-bottom: 2px solid #f0f0f0;
    }
    button {
      width: 100%;
      padding: 18px;
      margin: 10px 0;
      font-size: 18px;
      font-weight: 600;
      border: none;
      border-radius: 12px;
      cursor: pointer;
      transition: all 0.3s ease;
      box-shadow: 0 4px 15px rgba(0,0,0,0.1);
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    button:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(0,0,0,0.15);
    }
    button:active {
      transform: translateY(0);
    }
    .btn-up {
      background: linear-gradient(135deg, #4CAF50 0%, #45a049 100%);
      color: white;
    }
    .btn-down {
      background: linear-gradient(135deg, #f44336 0%, #da190b 100%);
      color: white;
    }
    .btn-stop {
      background: linear-gradient(135deg, #ff9800 0%, #e68900 100%);
      color: white;
    }
    .btn-prog {
      background: linear-gradient(135deg, #2196F3 0%, #0b7dda 100%);
      color: white;
    }
    .btn-reset {
      background: linear-gradient(135deg, #9c27b0 0%, #7b1fa2 100%);
      color: white;
      font-size: 14px;
      padding: 12px;
    }
    .pairing-box {
      background: #fff3cd;
      border: 2px solid #ffc107;
      border-radius: 10px;
      padding: 20px;
      margin-top: 20px;
    }
    .pairing-box h3 {
      color: #856404;
      margin-bottom: 15px;
      font-size: 16px;
    }
    .pairing-box ol {
      margin-left: 20px;
      color: #856404;
      font-size: 14px;
      line-height: 1.8;
    }
    .pairing-box li {
      margin: 8px 0;
    }
    .reset-box {
      background: #f8d7da;
      border: 2px solid #f5c6cb;
      border-radius: 10px;
      padding: 15px;
      margin-top: 20px;
    }
    .reset-box h3 {
      color: #721c24;
      margin-bottom: 10px;
      font-size: 14px;
    }
    .reset-box p {
      color: #721c24;
      font-size: 13px;
      margin-bottom: 10px;
    }
    .status {
      text-align: center;
      margin-top: 20px;
      padding: 10px;
      border-radius: 5px;
      font-size: 14px;
      display: none;
    }
    .status.show {
      display: block;
    }
    .status.success {
      background: #d4edda;
      color: #155724;
      border: 1px solid #c3e6cb;
    }
    .status.error {
      background: #f8d7da;
      color: #721c24;
      border: 1px solid #f5c6cb;
    }
    @media (max-width: 600px) {
      .container {
        padding: 25px;
      }
      h1 {
        font-size: 24px;
      }
      button {
        padding: 15px;
        font-size: 16px;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Somfy RTS Controller</h1>
    <div class="subtitle">HTTP to RTS Bridge v2.1</div>
    
    <div class="info-box">
      <p><strong>Device IP:</strong> )rawliteral" + WiFi.localIP().toString() + R"rawliteral(</p>
      <p><strong>WiFi Network:</strong> )rawliteral" + WiFi.SSID() + R"rawliteral(</p>
      <p><strong>Signal:</strong> )rawliteral" + String(WiFi.RSSI()) + R"rawliteral( dBm</p>
      <p><strong>Remote ID:</strong> 0x)rawliteral" + String(REMOTE, HEX) + R"rawliteral(</p>
      <p><strong>Status:</strong> <span style="color:#4CAF50;">Online</span></p>
    </div>
    
    <div class="control-section">
      <div class="section-title">Awning Controls</div>
      <button class="btn-up" onclick="sendCommand('up')">Open</button>
      <button class="btn-down" onclick="sendCommand('down')">Close</button>
      <button class="btn-stop" onclick="sendCommand('stop')">Stop</button>
    </div>
    
    <div class="pairing-box">
      <h3>Pairing Instructions</h3>
      <ol>
        <li>Press and hold <strong>PROG</strong> on existing remote (3 sec)</li>
        <li>Wait for awning to beep/jog</li>
        <li>Within 10 seconds, click <strong>PROG</strong> below</li>
        <li>Awning beeps/jogs again = paired!</li>
      </ol>
      <button class="btn-prog" onclick="sendCommand('prog')">Prog (Pair)</button>
    </div>
    
    <div class="reset-box">
      <h3>WiFi Configuration</h3>
      <p>To change WiFi settings, click the button below. Device will restart in configuration mode.</p>
      <button class="btn-reset" onclick="resetWiFi()">Reset WiFi Settings</button>
    </div>
    
    <div id="status" class="status"></div>
  </div>
  
  <script>
    function sendCommand(cmd) {
      const statusDiv = document.getElementById('status');
      statusDiv.className = 'status';
      statusDiv.textContent = 'Sending command...';
      statusDiv.classList.add('show');
      
      fetch('/' + cmd)
        .then(response => response.text())
        .then(data => {
          statusDiv.className = 'status success show';
          statusDiv.textContent = '✓ ' + data;
          setTimeout(() => statusDiv.classList.remove('show'), 3000);
        })
        .catch(error => {
          statusDiv.className = 'status error show';
          statusDiv.textContent = '✗ Error: ' + error;
          setTimeout(() => statusDiv.classList.remove('show'), 5000);
        });
    }
    
    function resetWiFi() {
      if (confirm('Reset WiFi settings? Device will restart in configuration mode.\n\nYou will need to connect to "Somfy-RTS-Setup" WiFi network to reconfigure.')) {
        fetch('/reset')
          .then(() => {
            alert('WiFi settings cleared! Device restarting...\n\nConnect to "Somfy-RTS-Setup" WiFi network to reconfigure.');
          });
      }
    }
  </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleUp() {
  Serial.println("\n[WEB] UP command received");
  sendCC1101Command(Command::Up);
  server.send(200, "text/plain", "UP command sent");
}

void handleDown() {
  Serial.println("\n[WEB] DOWN command received");
  sendCC1101Command(Command::Down);
  server.send(200, "text/plain", "DOWN command sent");
}

void handleStop() {
  Serial.println("\n[WEB] STOP command received");
  sendCC1101Command(Command::My);
  server.send(200, "text/plain", "STOP command sent");
}

void handleProg() {
  Serial.println("\n[WEB] PROG command received");
  Serial.println("⚠ Ensure awning is in pairing mode!");
  sendCC1101Command(Command::Prog);
  server.send(200, "text/plain", "PROG command sent");
}

void handleReset() {
  Serial.println("\n[WEB] WiFi reset requested");
  server.send(200, "text/plain", "Resetting WiFi settings and restarting...");
  delay(1000);
  wifiManager.resetSettings();
  delay(1000);
  ESP.restart();
}

void handleStatus() {
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"remote\":\"0x" + String(REMOTE, HEX) + "\",";
  json += "\"status\":\"online\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  String message = "Not Found\n\n";
  message += "Available endpoints:\n";
  message += "  /        - Web interface\n";
  message += "  /up      - Open awning\n";
  message += "  /down    - Close awning\n";
  message += "  /stop    - Stop awning\n";
  message += "  /prog    - Pair mode\n";
  message += "  /status  - JSON status\n";
  message += "  /reset   - Reset WiFi\n";
  server.send(404, "text/plain", message);
}