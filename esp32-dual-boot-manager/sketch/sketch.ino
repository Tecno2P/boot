/*
 * ESP32 Dual Boot Manager
 * Full Web GUI for OTA flashing and boot partition switching
 * Supports AP mode (first boot) and STA mode (saved credentials)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"

// ─── Constants ────────────────────────────────────────────────────────────────
#define AP_SSID       "ESP32-DualBoot"
#define AP_PASS       "admin1234"
#define AP_IP_STR     "192.168.4.1"

#define APP0_MAX_SIZE 1638400UL   // 0x190000 = 1,638,400 bytes
#define APP1_MAX_SIZE 1048576UL   // 0x100000 = 1,048,576 bytes

// ─── Globals ──────────────────────────────────────────────────────────────────
WebServer   server(80);
Preferences prefs;

bool    wifiIsAP   = false;
String  savedSSID  = "";
String  savedPass  = "";
String  currentIP  = AP_IP_STR;

// OTA state
bool           otaBusy        = false;
size_t         otaTotal       = 0;
size_t         otaWritten     = 0;
esp_ota_handle_t otaHandle    = 0;
const esp_partition_t* otaPart = nullptr;

// ─── Helper: partition state string ───────────────────────────────────────────
String partStateStr(esp_ota_img_states_t s) {
  switch (s) {
    case ESP_OTA_IMG_NEW:              return "NEW";
    case ESP_OTA_IMG_PENDING_VERIFY:   return "PENDING";
    case ESP_OTA_IMG_VALID:            return "VALID";
    case ESP_OTA_IMG_INVALID:          return "INVALID";
    case ESP_OTA_IMG_ABORTED:          return "ABORTED";
    case ESP_OTA_IMG_UNDEFINED:
    default:                           return "EMPTY";
  }
}

// ─── Helper: uptime string ────────────────────────────────────────────────────
String uptimeStr() {
  unsigned long s  = millis() / 1000;
  unsigned long h  = s / 3600; s %= 3600;
  unsigned long m  = s / 60;   s %= 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "%luh %lum %lus", h, m, s);
  return String(buf);
}

// ─── WiFi init ────────────────────────────────────────────────────────────────
void initWiFi() {
  prefs.begin("wifi", true);
  savedSSID = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();

  if (savedSSID.length() > 0) {
    Serial.printf("[WiFi] Connecting to SSID: %s\n", savedSSID.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      wifiIsAP  = false;
      currentIP = WiFi.localIP().toString();
      Serial.printf("[WiFi] Connected. IP: %s\n", currentIP.c_str());
      return;
    }
    Serial.println("[WiFi] STA failed, falling back to AP");
  }

  // AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  wifiIsAP  = true;
  currentIP = AP_IP_STR;
  Serial.printf("[WiFi] AP mode. SSID=%s  IP=%s\n", AP_SSID, currentIP.c_str());
}

// ─── SPIFFS file serving ──────────────────────────────────────────────────────
String mimeType(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".ico"))  return "image/x-icon";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg"))  return "image/jpeg";
  return "text/plain";
}

void handleStaticFile(const String& path) {
  if (SPIFFS.exists(path)) {
    File f = SPIFFS.open(path, "r");
    server.streamFile(f, mimeType(path));
    f.close();
  } else {
    server.send(404, "text/plain", "Not found: " + path);
  }
}

// ─── API: GET /api/status ─────────────────────────────────────────────────────
void handleStatus() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot    = esp_ota_get_boot_partition();

  auto getState = [](const char* label) -> String {
    const esp_partition_t* p = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!p) return "MISSING";
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(p, &st) != ESP_OK) return "EMPTY";
    return partStateStr(st);
  };

  char json[512];
  snprintf(json, sizeof(json),
    "{"
    "\"running\":\"%s\","
    "\"boot\":\"%s\","
    "\"app0_state\":\"%s\","
    "\"app1_state\":\"%s\","
    "\"heap\":%u,"
    "\"uptime\":\"%s\","
    "\"wifi_mode\":\"%s\","
    "\"ip\":\"%s\","
    "\"flash_size\":%u"
    "}",
    running ? running->label : "unknown",
    boot    ? boot->label    : "unknown",
    getState("app0").c_str(),
    getState("app1").c_str(),
    (unsigned)ESP.getFreeHeap(),
    uptimeStr().c_str(),
    wifiIsAP ? "AP" : "STA",
    currentIP.c_str(),
    (unsigned)ESP.getFlashChipSize()
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ─── API: GET /api/partitions ─────────────────────────────────────────────────
void handlePartitions() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot    = esp_ota_get_boot_partition();

  String json = "[";
  bool first = true;

  esp_partition_iterator_t it = esp_partition_find(
    ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);

  while (it) {
    const esp_partition_t* p = esp_partition_get(it);
    if (!first) json += ",";
    first = false;

    String state = "";
    if (p->type == ESP_PARTITION_TYPE_APP) {
      esp_ota_img_states_t st;
      if (esp_ota_get_state_partition(p, &st) == ESP_OK)
        state = partStateStr(st);
      else
        state = "EMPTY";
    }

    char entry[256];
    snprintf(entry, sizeof(entry),
      "{\"name\":\"%s\",\"type\":%d,\"subtype\":%d,"
      "\"offset\":%u,\"size\":%u,\"state\":\"%s\","
      "\"running\":%s,\"boot\":%s}",
      p->label, p->type, p->subtype,
      (unsigned)p->address, (unsigned)p->size,
      state.c_str(),
      (running && strcmp(p->label, running->label) == 0) ? "true" : "false",
      (boot    && strcmp(p->label, boot->label)    == 0) ? "true" : "false"
    );
    json += entry;
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  json += "]";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ─── API: GET /api/switch?target=app0|app1 ────────────────────────────────────
void handleSwitch() {
  if (!server.hasArg("target")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing target\"}");
    return;
  }
  String target_label = server.arg("target");
  if (target_label != "app0" && target_label != "app1") {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid target\"}");
    return;
  }

  const esp_partition_t* part = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, target_label.c_str());

  if (!part) {
    server.send(404, "application/json", "{\"success\":false,\"message\":\"Partition not found\"}");
    return;
  }

  // Check state
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(part, &st) == ESP_OK) {
    if (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED) {
      server.send(400, "application/json",
        "{\"success\":false,\"message\":\"Partition is INVALID or ABORTED\"}");
      return;
    }
  }

  esp_err_t err = esp_ota_set_boot_partition(part);
  if (err == ESP_OK) {
    char msg[128];
    snprintf(msg, sizeof(msg),
      "{\"success\":true,\"message\":\"Boot set to %s, rebooting...\"}",
      target_label.c_str());
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", msg);
    delay(500);
    esp_restart();
  } else {
    char msg[128];
    snprintf(msg, sizeof(msg),
      "{\"success\":false,\"message\":\"esp_ota_set_boot_partition failed: %d\"}",
      (int)err);
    server.send(500, "application/json", msg);
  }
}

// ─── API: GET /api/reboot ─────────────────────────────────────────────────────
void handleReboot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
  delay(500);
  esp_restart();
}

// ─── API: POST /api/flash?slot=app0|app1 ─────────────────────────────────────
void handleFlash() {
  // This handler is called when the upload is complete
  if (otaBusy) {
    // Finalise OTA
    esp_err_t endErr = esp_ota_end(otaHandle);
    otaHandle = 0;
    otaBusy   = false;

    if (endErr != ESP_OK) {
      char msg[128];
      snprintf(msg, sizeof(msg),
        "{\"success\":false,\"message\":\"OTA end failed: %d\"}", (int)endErr);
      server.send(500, "application/json", msg);
      return;
    }

    char msg[256];
    snprintf(msg, sizeof(msg),
      "{\"success\":true,\"message\":\"Flash complete. %u bytes written to %s. Use /api/switch to boot.\","
      "\"bytes\":%u,\"slot\":\"%s\"}",
      (unsigned)otaWritten,
      otaPart ? otaPart->label : "?",
      (unsigned)otaWritten,
      otaPart ? otaPart->label : "?"
    );
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", msg);
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No OTA in progress\"}");
  }
}

void handleFlashUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Determine slot
    String slot = server.hasArg("slot") ? server.arg("slot") : "app1";
    if (slot != "app0" && slot != "app1") slot = "app1";

    size_t maxSize = (slot == "app0") ? APP0_MAX_SIZE : APP1_MAX_SIZE;

    const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, slot.c_str());

    if (!part) {
      Serial.println("[OTA] Partition not found");
      return;
    }

    otaPart    = part;
    otaTotal   = 0;
    otaWritten = 0;

    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
    if (err != ESP_OK) {
      Serial.printf("[OTA] esp_ota_begin failed: %d\n", err);
      otaHandle = 0;
      return;
    }
    otaBusy = true;
    Serial.printf("[OTA] Begin flash to %s (max %u bytes)\n", slot.c_str(), (unsigned)maxSize);

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaBusy || !otaHandle) return;

    size_t maxSize = (otaPart && strcmp(otaPart->label, "app0") == 0)
                     ? APP0_MAX_SIZE : APP1_MAX_SIZE;

    if (otaWritten + upload.currentSize > maxSize) {
      Serial.println("[OTA] Firmware too large, aborting");
      esp_ota_abort(otaHandle);
      otaHandle = 0;
      otaBusy   = false;
      return;
    }

    esp_err_t err = esp_ota_write(otaHandle, upload.buf, upload.currentSize);
    if (err != ESP_OK) {
      Serial.printf("[OTA] esp_ota_write failed: %d\n", err);
      esp_ota_abort(otaHandle);
      otaHandle = 0;
      otaBusy   = false;
      return;
    }
    otaWritten += upload.currentSize;
    Serial.printf("[OTA] Written %u / ? bytes\n", (unsigned)otaWritten);

  } else if (upload.status == UPLOAD_FILE_END) {
    otaTotal = upload.totalSize;
    Serial.printf("[OTA] Upload end. Total: %u bytes\n", (unsigned)otaTotal);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (otaBusy && otaHandle) {
      esp_ota_abort(otaHandle);
      otaHandle = 0;
    }
    otaBusy = false;
    Serial.println("[OTA] Upload aborted");
  }
}

// ─── API: POST /api/wifi ──────────────────────────────────────────────────────
void handleWifiSave() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"success\":false,\"message\":\"POST required\"}");
    return;
  }

  String body = server.arg("plain");
  // Simple JSON parse for ssid and pass
  auto extractField = [&](const String& key) -> String {
    String search = "\"" + key + "\"";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    idx = body.indexOf("\"", idx + search.length() + 1);
    if (idx < 0) return "";
    int end = body.indexOf("\"", idx + 1);
    if (end < 0) return "";
    return body.substring(idx + 1, end);
  };

  String ssid = extractField("ssid");
  String pass = extractField("pass");

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"ssid required\"}");
    return;
  }

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  savedSSID = ssid;
  savedPass = pass;

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json",
    "{\"success\":true,\"message\":\"Credentials saved. Reboot to connect.\"}");
}

// ─── API: GET /api/wifi/scan ──────────────────────────────────────────────────
void handleWifiScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    char entry[128];
    snprintf(entry, sizeof(entry),
      "{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
      WiFi.SSID(i).c_str(),
      WiFi.RSSI(i),
      (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false"
    );
    json += entry;
  }
  json += "]";
  WiFi.scanDelete();

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ─── API: GET /api/files ──────────────────────────────────────────────────────
void handleFileList() {
  String json = "{\"files\":[";
  bool first = true;

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    if (!first) json += ",";
    first = false;
    char entry[128];
    snprintf(entry, sizeof(entry),
      "{\"name\":\"%s\",\"size\":%u}",
      file.name(), (unsigned)file.size());
    json += entry;
    file = root.openNextFile();
  }

  // SPIFFS total/used
  size_t total = SPIFFS.totalBytes();
  size_t used  = SPIFFS.usedBytes();

  char tail[128];
  snprintf(tail, sizeof(tail),
    "],\"total\":%u,\"used\":%u}",
    (unsigned)total, (unsigned)used);
  json += tail;

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ─── API: POST /api/upload ────────────────────────────────────────────────────
static File uploadFile;

void handleUpload() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"File uploaded\"}");
}

void handleUploadFile() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    Serial.printf("[SPIFFS] Uploading: %s\n", filename.c_str());
    uploadFile = SPIFFS.open(filename, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("[SPIFFS] Failed to open file for writing");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("[SPIFFS] Upload done: %u bytes\n", (unsigned)upload.totalSize);
    }
  }
}

// ─── API: DELETE /api/file?name= ─────────────────────────────────────────────
void handleDeleteFile() {
  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing name\"}");
    return;
  }
  String name = server.arg("name");
  if (!name.startsWith("/")) name = "/" + name;

  if (SPIFFS.exists(name)) {
    SPIFFS.remove(name);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"success\":true,\"message\":\"File deleted\"}");
  } else {
    server.send(404, "application/json", "{\"success\":false,\"message\":\"File not found\"}");
  }
}

// ─── Route setup ──────────────────────────────────────────────────────────────
void setupRoutes() {
  // Static pages
  server.on("/",             HTTP_GET,  []() { handleStaticFile("/index.html"); });
  server.on("/index.html",   HTTP_GET,  []() { handleStaticFile("/index.html"); });
  server.on("/switch.html",  HTTP_GET,  []() { handleStaticFile("/switch.html"); });
  server.on("/flash.html",   HTTP_GET,  []() { handleStaticFile("/flash.html"); });
  server.on("/wifi.html",    HTTP_GET,  []() { handleStaticFile("/wifi.html"); });
  server.on("/files.html",   HTTP_GET,  []() { handleStaticFile("/files.html"); });
  server.on("/style.css",    HTTP_GET,  []() { handleStaticFile("/style.css"); });

  // API endpoints
  server.on("/api/status",     HTTP_GET,    handleStatus);
  server.on("/api/partitions", HTTP_GET,    handlePartitions);
  server.on("/api/switch",     HTTP_GET,    handleSwitch);
  server.on("/api/reboot",     HTTP_GET,    handleReboot);
  server.on("/api/wifi",       HTTP_POST,   handleWifiSave);
  server.on("/api/wifi/scan",  HTTP_GET,    handleWifiScan);
  server.on("/api/files",      HTTP_GET,    handleFileList);
  server.on("/api/file",       HTTP_DELETE, handleDeleteFile);

  // OTA flash upload
  server.on("/api/flash", HTTP_POST,
    handleFlash,
    handleFlashUpload
  );

  // SPIFFS file upload
  server.on("/api/upload", HTTP_POST,
    handleUpload,
    handleUploadFile
  );

  // CORS preflight
  server.on("/api/flash",   HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin",  "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });

  // 404 fallback
  server.onNotFound([]() {
    String path = server.uri();
    if (SPIFFS.exists(path)) {
      handleStaticFile(path);
    } else {
      server.send(404, "text/plain", "Not Found: " + path);
    }
  });
}

// ─── setup() ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] ESP32 Dual Boot Manager starting...");

  // Print current running partition
  const esp_partition_t* running = esp_ota_get_running_partition();
  Serial.printf("[BOOT] Running partition: %s\n",
    running ? running->label : "unknown");

  // Mark current partition valid (if in pending state)
  esp_ota_img_states_t st;
  if (running && esp_ota_get_state_partition(running, &st) == ESP_OK) {
    if (st == ESP_OTA_IMG_PENDING_VERIFY) {
      Serial.println("[BOOT] Marking partition as VALID");
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Mount failed! Formatting...");
    SPIFFS.format();
    if (!SPIFFS.begin(true)) {
      Serial.println("[SPIFFS] Fatal: cannot mount SPIFFS");
    }
  } else {
    Serial.printf("[SPIFFS] Mounted. Total=%u Used=%u\n",
      (unsigned)SPIFFS.totalBytes(), (unsigned)SPIFFS.usedBytes());
  }

  // WiFi
  initWiFi();

  // HTTP server
  setupRoutes();
  server.begin();
  Serial.printf("[HTTP] Server started at http://%s\n", currentIP.c_str());
}

// ─── loop() ───────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();
}
