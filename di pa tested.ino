#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ====== CONFIG ======
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Supabase edge function URL
const char* API_URL = "https://<project-ref>.supabase.co/functions/v1/esp32-api";

// from devices.api_key in Supabase
const char* DEVICE_API_KEY = "<device_api_key>";

// Test cadence
unsigned long lastRun = 0;
const unsigned long LOOP_INTERVAL_MS = 15000;

// Store command IDs that need ack
String pendingAckIds[10];
int pendingAckCount = 0;

// TLS client (for HTTPS)
WiFiClientSecure secureClient;

// ====== HELPERS ======
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi connect failed.");
  }
}

bool httpRequest(
  const char* method,
  const String& body,
  int& statusCode,
  String& responseBody
) {
  HTTPClient http;
  http.begin(secureClient, API_URL);
  http.addHeader("x-api-key", DEVICE_API_KEY);
  if (String(method) != "GET") {
    http.addHeader("Content-Type", "application/json");
  }

  if (String(method) == "POST") {
    statusCode = http.POST(body);
  } else if (String(method) == "PATCH") {
    statusCode = http.sendRequest("PATCH", body);
  } else if (String(method) == "GET") {
    statusCode = http.GET();
  } else {
    Serial.println("Unsupported HTTP method");
    http.end();
    return false;
  }

  responseBody = http.getString();
  http.end();
  return true;
}

void sendTelemetry() {
  // Build POST payload expected by backend
  // { magnitude, timestamp?, metadata? }
  StaticJsonDocument<256> doc;
  doc["magnitude"] = 4.2; // replace with sensor reading
  JsonObject metadata = doc.createNestedObject("metadata");
  metadata["battery"] = 3.88;
  metadata["firmware"] = "esp32-test-v1";
  metadata["heap"] = ESP.getFreeHeap();

  String body;
  serializeJson(doc, body);

  int code = 0;
  String resp;
  if (!httpRequest("POST", body, code, resp)) return;

  Serial.printf("[POST] code=%d\n", code);
  Serial.printf("[POST] resp=%s\n", resp.c_str());
}

void pollDownlinks() {
  int code = 0;
  String resp;
  if (!httpRequest("GET", "", code, resp)) return;

  Serial.printf("[GET] code=%d\n", code);
  Serial.printf("[GET] resp=%s\n", resp.c_str());

  if (code != 200) return;

  DynamicJsonDocument doc(4096);
  auto err = deserializeJson(doc, resp);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return;
  }

  // Fetch phone number (if present)
  JsonArray numbers = doc["phone_numbers"].as<JsonArray>();
  if (!numbers.isNull() && numbers.size() > 0) {
    const char* number = numbers[0] | "";
    Serial.printf("Registered number: %s\n", number);
  } else {
    Serial.println("No registered phone number returned.");
  }

  // Fetch pending commands
  JsonArray commands = doc["pending_commands"].as<JsonArray>();
  pendingAckCount = 0;

  if (!commands.isNull() && commands.size() > 0) {
    Serial.printf("Pending commands: %d\n", commands.size());

    for (JsonObject cmd : commands) {
      const char* id = cmd["id"] | "";
      const char* type = cmd["command_type"] | "";
      JsonVariant payload = cmd["payload"];

      Serial.printf("Command id=%s type=%s\n", id, type);

      // TODO: execute command here based on type/payload
      // For now, mark every command to acknowledge.
      if (strlen(id) > 0 && pendingAckCount < 10) {
        pendingAckIds[pendingAckCount++] = String(id);
      }
    }
  } else {
    Serial.println("No pending commands.");
  }
}

void ackCommands() {
  if (pendingAckCount == 0) {
    Serial.println("Nothing to acknowledge.");
    return;
  }

  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.createNestedArray("acknowledged_command_ids");
  for (int i = 0; i < pendingAckCount; i++) {
    arr.add(pendingAckIds[i]);
  }

  String body;
  serializeJson(doc, body);

  int code = 0;
  String resp;
  if (!httpRequest("PATCH", body, code, resp)) return;

  Serial.printf("[PATCH] code=%d\n", code);
  Serial.printf("[PATCH] resp=%s\n", resp.c_str());

  // Clear local ack queue on success
  if (code == 200) pendingAckCount = 0;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // For quick testing only. In production, use CA cert pinning.
  secureClient.setInsecure();

  connectWiFi();
}

void loop() {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    return;
  }

  unsigned long now = millis();
  if (now - lastRun >= LOOP_INTERVAL_MS) {
    lastRun = now;

    sendTelemetry();   // POST
    delay(500);
    pollDownlinks();   // GET
    delay(500);
    ackCommands();     // PATCH
  }

  delay(50);
}
