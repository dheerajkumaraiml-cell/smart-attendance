// ESP32 RFID Attendance - 2 room LEDs
//
// RC522 wiring:   SDA(SS)=GPIO5, RST=GPIO4, SCK=GPIO25, MISO=GPIO26, MOSI=GPIO23, 3.3V, GND
// LED wiring:     GREEN (+) -> GPIO27 -> 220 ohm -> LED -> GND
//                 RED   (+) -> GPIO32 -> 220 ohm -> LED -> GND
//
// GREEN blinking = scan window open (first 10 min of lecture)
// GREEN solid    = teacher pressed "Allow scanning"
// RED solid      = closed / no lecture / no connection to server

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>

// ================= RFID PINS =================
#define SS_PIN    5
#define RST_PIN   4
#define SCK_PIN   25
#define MISO_PIN  26
#define MOSI_PIN  23

// ================= LED PINS ==================
#define GREEN_LED 27
#define RED_LED   32

MFRC522 rfid(SS_PIN, RST_PIN);

// ================= WIFI ======================
const char* ssid     = "TP-Link_625C";
const char* password = "38340871";

// ================= SERVER ====================
// Laptop IPv4 (ipconfig) + server PORT (9000, as in server.js). No trailing slash.
const char* serverBase = "http://192.168.0.133:9000";
const char* roomCode   = "R101";  // must match the room in the timetable
const char* deviceKey  = "";      // same as DEVICE_KEY on server, or leave empty

// ================= STATE =====================
enum LightState { LIGHT_RED, LIGHT_GREEN_BLINK, LIGHT_GREEN_SOLID };
LightState light = LIGHT_RED;
bool blinkOn = false;
unsigned long lastBlink = 0, lastPoll = 0, lastScan = 0;
const unsigned long BLINK_MS         = 500;
const unsigned long POLL_MS          = 2000;
const unsigned long SCAN_COOLDOWN_MS = 1800;

// ================= LED =======================
void showLight() {
  if (light == LIGHT_RED) {
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
  } else if (light == LIGHT_GREEN_SOLID) {
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  } else {  // LIGHT_GREEN_BLINK
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, blinkOn ? HIGH : LOW);
  }
}

// ================= ASK SERVER FOR LIGHT ======
void pollStatus() {
  if (WiFi.status() != WL_CONNECTED) { light = LIGHT_RED; return; }

  HTTPClient http;
  http.setTimeout(3000);
  String url = String(serverBase) + "/api/device/status?room=" + roomCode;
  if (!http.begin(url)) { light = LIGHT_RED; return; }
  if (strlen(deviceKey) > 0) http.addHeader("x-device-key", deviceKey);

  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    Serial.println("Status: " + body);
    if (body.indexOf("\"light\":\"yellow\"") >= 0)      light = LIGHT_GREEN_SOLID;  // teacher allowed
    else if (body.indexOf("\"light\":\"green\"") >= 0)  light = LIGHT_GREEN_BLINK;  // scan window open
    else                                                light = LIGHT_RED;
  } else {
    light = LIGHT_RED;
    Serial.printf("Status HTTP error: %d\n", code);
  }
  http.end();
  showLight();
}

// ================= SEND SCAN =================
void sendScan(const String& uid) {
  if (WiFi.status() != WL_CONNECTED) { Serial.println("Scan failed: WiFi disconnected"); return; }

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(String(serverBase) + "/api/scan")) { Serial.println("Cannot start scan request"); return; }
  http.addHeader("Content-Type", "application/json");
  if (strlen(deviceKey) > 0) http.addHeader("x-device-key", deviceKey);

  String payload = "{\"uid\":\"" + uid + "\",\"room\":\"" + roomCode + "\"}";
  Serial.println("Payload: " + payload);

  int code = http.POST(payload);
  Serial.printf("Scan HTTP status: %d\n", code);
  if (code > 0) Serial.println("Server response: " + http.getString());
  else          Serial.println(http.errorToString(code));
  http.end();
}

// ================= SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  showLight();

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  delay(100);
  Serial.print("RFID firmware: ");
  rfid.PCD_DumpVersionToSerial();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Room: " + String(roomCode));

  pollStatus();
  Serial.println("RFID scanner ready");
}

// ================= LOOP ======================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    light = LIGHT_RED;
    showLight();
    WiFi.reconnect();
    delay(100);
    return;
  }

  // Blink timing (non-blocking)
  if (millis() - lastBlink >= BLINK_MS) {
    lastBlink = millis();
    blinkOn = !blinkOn;
  }

  // Refresh LED status from server every 2 seconds
  if (millis() - lastPoll >= POLL_MS) {
    lastPoll = millis();
    pollStatus();
  }
  showLight();

  // Card check
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())  return;

  if (millis() - lastScan < SCAN_COOLDOWN_MS) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  lastScan = millis();

  // UID without spaces, uppercase
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  Serial.println();
  Serial.println("CARD DETECTED");
  Serial.println("UID: " + uid);
  sendScan(uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  Serial.println("Ready for next scan");
}
