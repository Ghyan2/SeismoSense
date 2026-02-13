#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ===================== CONFIG =====================
const char* WIFI_SSID = "PLDTHOMEFIBR452d0";
const char* WIFI_PASS = "PLDTWIFIm7enk";

const char* SUPABASE_FUNCTION_URL =
  "https://saybcjfzpipmrhipllqd.supabase.co/functions/v1/esp32-api";

const char* DEVICE_API_KEY = "fe1cf60d-d894-4f4e-b0cc-6e518f080294";

const unsigned long INTERVAL_MS = 15000;

// ===================== GLOBALS =====================
WiFiClientSecure secureClient;
String ackIds[10];
int ackCount = 0;
unsigned long lastTick = 0;

// ===================== HELPERS =====================
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("Connecting WiFi %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi failed.");
  }
}

bool sendHttp(const char* method, const String& body, int& code, String& resp) {
  HTTPClient http;
  http.begin(secureClient, SUPABASE_FUNCTION_URL);
  http.addHeader("x-api-key", DEVICE_API_KEY);

  if (String(method) != "GET") {
    http.addHeader("Content-Type", "application/json");
  }

  if (String(method) == "POST") {
    code = http.POST(body);
  } else if (String(method) == "GET") {
    code = http.GET();
  } else if (String(method) == "PATCH") {
    code = http.sendRequest("PATCH", body);
  } else {
    http.end();
    return false;
  }

  resp = http.getString();
  http.end();
  return true;
}

// ===================== API CALLS =====================
void postTelemetry() {
  StaticJsonDocument<256> doc;
  doc["magnitude"] = 4.1; // Replace with sensor reading
  JsonObject metadata = doc.createNestedObject("metadata");
  metadata["battery"] = 3.85;
  metadata["firmware"] = "esp32-test-v1";
  metadata["free_heap"] = ESP.getFreeHeap();

  String body;
  serializeJson(doc, body);

  int code = 0;
  String resp;
  sendHttp("POST", body, code, resp);

  Serial.printf("[POST] code=%d\n", code);
  Serial.printf("[POST] resp=%s\n", resp.c_str());
}

void pollDownlink() {
  int code = 0;
  String resp;
  sendHttp("GET", "", code, resp);

  Serial.printf("[GET] code=%d\n", code);
  Serial.printf("[GET] resp=%s\n", resp.c_str());

  if (code != 200) return;

  DynamicJsonDocument doc(4096);
  auto err = deserializeJson(doc, resp);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return;
  }

  // Registered number from backend
  JsonArray nums = doc["phone_numbers"].as<JsonArray>();
  if (!nums.isNull() && nums.size() > 0) {
    const char* registered = nums[0] | "";
    Serial.printf("Registered phone number: %s\n", registered);
  } else {
    Serial.println("No registered phone number found.");
  }

  // Pending commands
  ackCount = 0;
  JsonArray cmds = doc["pending_commands"].as<JsonArray>();
  if (!cmds.isNull()) {
    for (JsonObject cmd : cmds) {
      const char* id = cmd["id"] | "";
      const char* type = cmd["command_type"] | "";
      Serial.printf("Command: id=%s type=%s\n", id, type);

      // TODO: execute command based on type/payload here.
      // For test: mark command for acknowledgment
      if (strlen(id) > 0 && ackCount < 10) {
        ackIds[ackCount++] = String(id);
      }
    }
  }
}

void acknowledgeCommands() {
  if (ackCount == 0) {
    Serial.println("[PATCH] no commands to acknowledge");
    return;
  }

  StaticJsonDocument<512> doc;
  JsonArray arr = doc.createNestedArray("acknowledged_command_ids");
  for (int i = 0; i < ackCount; i++) {
    arr.add(ackIds[i]);
  }

  String body;
  serializeJson(doc, body);

  int code = 0;
  String resp;
  sendHttp("PATCH", body, code, resp);

  Serial.printf("[PATCH] code=%d\n", code);
  Serial.printf("[PATCH] resp=%s\n", resp.c_str());

  if (code == 200) ackCount = 0;
}

// ===================== SETUP/LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Quick test mode for HTTPS:
  // For production, replace with CA cert verification.
  secureClient.setInsecure();

  ensureWiFi();
}

void loop() {
  ensureWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    return;
  }

  unsigned long now = millis();
  if (now - lastTick >= INTERVAL_MS) {
    lastTick = now;

    postTelemetry();      // POST
    delay(500);

    pollDownlink();       // GET (includes phone_numbers)
    delay(500);

    acknowledgeCommands();// PATCH
  }

  delay(50);
}
