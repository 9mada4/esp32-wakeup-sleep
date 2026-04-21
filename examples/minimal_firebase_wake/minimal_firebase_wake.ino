struct WakeConfig;
struct FirebaseSession;

#include <wake_core.h>
#include <stdlib.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <USB.h>
#include <USBHID.h>
#include <USBHIDMouse.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#define WAKE_HAS_ARDUINO_USB_HID 1
#else
#define WAKE_HAS_ARDUINO_USB_HID 0
#endif

#ifndef WAKE_USB_REMOTE_WAKE_ATTR
#define WAKE_USB_REMOTE_WAKE_ATTR 0x20u
#endif

#if defined(ARDUINO_ARCH_ESP32)
#define WAKE_LOG_SERIAL Serial0
#else
#define WAKE_LOG_SERIAL Serial
#endif

static const int kBootButtonPin = 0;  // ESP32-S3 BOOT button (active low)
static bool s_lastBootLevel = true;

#if WAKE_HAS_ARDUINO_USB_HID
static USBHID sUsbHid(HID_ITF_PROTOCOL_MOUSE);
static USBHIDMouse sUsbMouse;

static void onArduinoUsbEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  (void)arg;
  if (event_base != ARDUINO_USB_EVENTS) {
    return;
  }
  if (event_id == ARDUINO_USB_SUSPEND_EVENT) {
    bool remoteWakeEnabled = false;
    if (event_data != nullptr) {
      remoteWakeEnabled = ((arduino_usb_event_data_t*)event_data)->suspend.remote_wakeup_en;
    }
    wake_usb_on_suspend(remoteWakeEnabled);
    return;
  }
  if (event_id == ARDUINO_USB_RESUME_EVENT) {
    wake_usb_on_resume();
  }
}

static void configureUsbForWake() {
  USB.onEvent(onArduinoUsbEvent);
  USB.usbClass(TUSB_CLASS_HID);
  USB.usbSubClass(HID_SUBCLASS_BOOT);
  USB.usbProtocol(HID_ITF_PROTOCOL_MOUSE);
  USB.usbAttributes((uint8_t)WAKE_USB_REMOTE_WAKE_ATTR);
  USB.usbPower(100);
  USB.productName("ESP32WakeCore");

  (void)sUsbHid;
  sUsbMouse.begin();
  USB.begin();
}
#endif

// ---------------------------------------------------------------------------
// Firebase wake monitor app (minimal additions on top of minimal template).
// ---------------------------------------------------------------------------

#if WAKE_HAS_ARDUINO_USB_HID
static const char* kConfigNamespace = "wake_cfg";
static const char* kSetupApSsid = "ESP32-Wake-Setup";
static const char* kSetupApPass = "esp32setup";
static const uint32_t kWifiConnectTimeoutMs = 20000;
static const uint32_t kFirebasePollIntervalMs = 1000;

struct WakeConfig {
  char wifi_ssid[33];
  char wifi_password[65];
  char api_key[129];
  char database_url[193];
  char user_email[129];
  char user_password[129];
};

struct FirebaseSession {
  String id_token;
  uint32_t refresh_at_ms;
};

static WakeConfig s_config = {};
static FirebaseSession s_firebase = {};
static WebServer s_setupServer(80);
static bool s_configMode = false;
static bool s_restartQueued = false;
static uint32_t s_restartAtMs = 0;
static uint32_t s_lastPollMs = 0;

static bool timeReached(uint32_t now, uint32_t due_ms) {
  return (int32_t)(now - due_ms) >= 0;
}

static void copyToBuf(char* dst, size_t dst_len, const String& value) {
  if (dst_len == 0) {
    return;
  }
  snprintf(dst, dst_len, "%s", value.c_str());
}

static void trimStringField(char* value, size_t len) {
  String tmp = String(value);
  tmp.trim();
  copyToBuf(value, len, tmp);
}

static void normalizeDatabaseUrl(char* url, size_t len) {
  String s = String(url);
  s.trim();
  while (s.endsWith("/")) {
    s.remove(s.length() - 1);
  }
  copyToBuf(url, len, s);
}

static bool configComplete(const WakeConfig& cfg) {
  return cfg.wifi_ssid[0] != '\0' &&
         cfg.wifi_password[0] != '\0' &&
         cfg.api_key[0] != '\0' &&
         cfg.database_url[0] != '\0' &&
         cfg.user_email[0] != '\0' &&
         cfg.user_password[0] != '\0';
}

static bool loadConfigNvs(WakeConfig* out) {
  if (!out) {
    return false;
  }
  *out = {};

  Preferences prefs;
  if (!prefs.begin(kConfigNamespace, true)) {
    WAKE_LOG_SERIAL.println("NVS open failed (read)");
    return false;
  }

  copyToBuf(out->wifi_ssid, sizeof(out->wifi_ssid), prefs.getString("wifi_ssid", ""));
  copyToBuf(out->wifi_password, sizeof(out->wifi_password), prefs.getString("wifi_password", ""));
  copyToBuf(out->api_key, sizeof(out->api_key), prefs.getString("api_key", ""));
  copyToBuf(out->database_url, sizeof(out->database_url), prefs.getString("database_url", ""));
  copyToBuf(out->user_email, sizeof(out->user_email), prefs.getString("user_email", ""));
  copyToBuf(out->user_password, sizeof(out->user_password), prefs.getString("user_password", ""));
  prefs.end();

  normalizeDatabaseUrl(out->database_url, sizeof(out->database_url));
  return configComplete(*out);
}

static bool saveConfigNvs(const WakeConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(kConfigNamespace, false)) {
    WAKE_LOG_SERIAL.println("NVS open failed (write)");
    return false;
  }

  bool ok = true;
  ok &= (prefs.putString("wifi_ssid", cfg.wifi_ssid) > 0);
  ok &= (prefs.putString("wifi_password", cfg.wifi_password) > 0);
  ok &= (prefs.putString("api_key", cfg.api_key) > 0);
  ok &= (prefs.putString("database_url", cfg.database_url) > 0);
  ok &= (prefs.putString("user_email", cfg.user_email) > 0);
  ok &= (prefs.putString("user_password", cfg.user_password) > 0);
  prefs.end();

  return ok;
}

static String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

static void sendSetupPage(int code, const String& notice = "") {
  String ipText = WiFi.softAPIP().toString();
  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>ESP32 Wake Setup</title></head><body>"
                "<h1>ESP32 Wake Setup</h1>";
  if (notice.length() > 0) {
    html += "<p><b>" + htmlEscape(notice) + "</b></p>";
  }
  html += "<p>AP SSID: <code>";
  html += htmlEscape(kSetupApSsid);
  html += "</code> / IP: <code>";
  html += htmlEscape(ipText);
  html += "</code></p>"
          "<form method='POST' action='/save'>"
          "<label>wifi_ssid</label><br><input name='wifi_ssid' maxlength='32' value='" + htmlEscape(String(s_config.wifi_ssid)) + "' style='width:95%'><br><br>"
          "<label>wifi_password</label><br><input name='wifi_password' type='password' maxlength='64' value='" + htmlEscape(String(s_config.wifi_password)) + "' style='width:95%'><br><br>"
          "<label>api_key</label><br><input name='api_key' maxlength='128' value='" + htmlEscape(String(s_config.api_key)) + "' style='width:95%'><br><br>"
          "<label>database_url</label><br><input name='database_url' maxlength='192' value='" + htmlEscape(String(s_config.database_url)) + "' placeholder='https://YOUR_DB.firebaseio.com' style='width:95%'><br><br>"
          "<label>user_email</label><br><input name='user_email' maxlength='128' value='" + htmlEscape(String(s_config.user_email)) + "' style='width:95%'><br><br>"
          "<label>user_password</label><br><input name='user_password' type='password' maxlength='128' value='" + htmlEscape(String(s_config.user_password)) + "' style='width:95%'><br><br>"
          "<button type='submit'>Save to NVS and restart</button>"
          "</form>"
          "<p><a href='/status'>status</a></p>"
          "</body></html>";

  s_setupServer.send(code, "text/html; charset=utf-8", html);
}

static bool parseConfigFromRequest(WakeConfig* out, String* error) {
  if (!out) {
    return false;
  }

  WakeConfig next = {};
  copyToBuf(next.wifi_ssid, sizeof(next.wifi_ssid), s_setupServer.arg("wifi_ssid"));
  copyToBuf(next.wifi_password, sizeof(next.wifi_password), s_setupServer.arg("wifi_password"));
  copyToBuf(next.api_key, sizeof(next.api_key), s_setupServer.arg("api_key"));
  copyToBuf(next.database_url, sizeof(next.database_url), s_setupServer.arg("database_url"));
  copyToBuf(next.user_email, sizeof(next.user_email), s_setupServer.arg("user_email"));
  copyToBuf(next.user_password, sizeof(next.user_password), s_setupServer.arg("user_password"));

  trimStringField(next.wifi_ssid, sizeof(next.wifi_ssid));
  trimStringField(next.wifi_password, sizeof(next.wifi_password));
  trimStringField(next.api_key, sizeof(next.api_key));
  trimStringField(next.database_url, sizeof(next.database_url));
  trimStringField(next.user_email, sizeof(next.user_email));
  trimStringField(next.user_password, sizeof(next.user_password));
  normalizeDatabaseUrl(next.database_url, sizeof(next.database_url));

  if (!configComplete(next)) {
    if (error) {
      *error = "all fields are required";
    }
    return false;
  }

  *out = next;
  return true;
}

static void handleSetupRoot() {
  sendSetupPage(200);
}

static void handleSetupStatus() {
  String body;
  body.reserve(320);
  body += "mode=config\n";
  body += "ap_ssid=";
  body += kSetupApSsid;
  body += "\n";
  body += "ap_ip=";
  body += WiFi.softAPIP().toString();
  body += "\n";
  body += "stored_wifi_ssid=";
  body += String(s_config.wifi_ssid);
  body += "\n";
  body += "firebase_db=";
  body += String(s_config.database_url);
  body += "\n";
  s_setupServer.send(200, "text/plain; charset=utf-8", body);
}

static void handleSetupSave() {
  WakeConfig next = {};
  String err;
  if (!parseConfigFromRequest(&next, &err)) {
    sendSetupPage(400, "Save failed: " + err);
    return;
  }

  if (!saveConfigNvs(next)) {
    sendSetupPage(500, "Save failed: NVS write error");
    return;
  }

  s_config = next;
  s_restartQueued = true;
  s_restartAtMs = millis() + 1500;
  sendSetupPage(200, "Saved. Rebooting...");
  WAKE_LOG_SERIAL.println("Config saved to NVS. Reboot scheduled.");
}

static void startConfigMode() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(kSetupApSsid, kSetupApPass)) {
    WAKE_LOG_SERIAL.println("Failed to start setup AP");
    return;
  }

  s_setupServer.on("/", HTTP_GET, handleSetupRoot);
  s_setupServer.on("/save", HTTP_POST, handleSetupSave);
  s_setupServer.on("/status", HTTP_GET, handleSetupStatus);
  s_setupServer.begin();

  s_configMode = true;
  WAKE_LOG_SERIAL.printf("Config mode: connect to AP '%s', open http://%s/\n",
                         kSetupApSsid,
                         WiFi.softAPIP().toString().c_str());
}

static bool connectWifiSta(const WakeConfig& cfg) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

  uint32_t start_ms = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start_ms) < kWifiConnectTimeoutMs) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    WAKE_LOG_SERIAL.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  WAKE_LOG_SERIAL.println("Wi-Fi connect timeout");
  return false;
}

static String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

static bool jsonExtractStringValue(const String& json, const char* key, String* out) {
  if (!out) {
    return false;
  }

  String pattern = "\"";
  pattern += key;
  pattern += "\"";
  int key_pos = json.indexOf(pattern);
  if (key_pos < 0) {
    return false;
  }

  int colon_pos = json.indexOf(':', key_pos + (int)pattern.length());
  if (colon_pos < 0) {
    return false;
  }

  int first_quote = json.indexOf('"', colon_pos + 1);
  if (first_quote < 0) {
    return false;
  }

  String value;
  bool escaped = false;
  for (int i = first_quote + 1; i < (int)json.length(); ++i) {
    char c = json[i];
    if (escaped) {
      value += c;
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      *out = value;
      return true;
    }
    value += c;
  }
  return false;
}

static bool firebaseSignIn(FirebaseSession* session) {
  if (!session) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + String(s_config.api_key);
  if (!https.begin(client, url)) {
    WAKE_LOG_SERIAL.println("Firebase sign-in: begin failed");
    return false;
  }

  https.addHeader("Content-Type", "application/json");
  String payload = "{\"email\":\"" + jsonEscape(String(s_config.user_email)) +
                   "\",\"password\":\"" + jsonEscape(String(s_config.user_password)) +
                   "\",\"returnSecureToken\":true}";
  int code = https.POST(payload);
  String body = https.getString();
  https.end();

  if (code != 200) {
    WAKE_LOG_SERIAL.printf("Firebase sign-in failed: HTTP %d body=%s\n", code, body.c_str());
    return false;
  }

  String token;
  if (!jsonExtractStringValue(body, "idToken", &token) || token.length() == 0) {
    WAKE_LOG_SERIAL.println("Firebase sign-in failed: idToken not found");
    return false;
  }

  String expires_s;
  uint32_t expires_sec = 3600;
  if (jsonExtractStringValue(body, "expiresIn", &expires_s)) {
    uint32_t parsed = (uint32_t)expires_s.toInt();
    if (parsed >= 120) {
      expires_sec = parsed;
    }
  }

  session->id_token = token;
  session->refresh_at_ms = millis() + (expires_sec - 60) * 1000UL;
  WAKE_LOG_SERIAL.println("Firebase sign-in success");
  return true;
}

static bool firebaseEnsureSession(FirebaseSession* session) {
  if (!session) {
    return false;
  }

  uint32_t now = millis();
  if (session->id_token.length() == 0 || timeReached(now, session->refresh_at_ms)) {
    return firebaseSignIn(session);
  }
  return true;
}

static bool parseWakeRequestRaw(const String& raw, bool* recognized, bool* is_one) {
  if (recognized) *recognized = false;
  if (is_one) *is_one = false;

  String token = raw;
  token.trim();

  if (token.length() >= 2 && token[0] == '"' && token[token.length() - 1] == '"') {
    token = token.substring(1, token.length() - 1);
    token.trim();
  }

  String lower = token;
  lower.toLowerCase();

  if (lower == "1" || lower == "true" || lower == "on") {
    if (recognized) *recognized = true;
    if (is_one) *is_one = true;
    return true;
  }

  if (lower == "0" || lower == "false" || lower == "off" || lower == "null" || lower.length() == 0) {
    if (recognized) *recognized = true;
    if (is_one) *is_one = false;
    return true;
  }

  char* endp = nullptr;
  long numeric = strtol(lower.c_str(), &endp, 10);
  if (endp && *endp == '\0') {
    if (recognized) *recognized = true;
    if (is_one) *is_one = (numeric != 0);
    return true;
  }

  return false;
}

static bool firebaseGetWakeRequest(bool* recognized, bool* is_one) {
  if (recognized) *recognized = false;
  if (is_one) *is_one = false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = String(s_config.database_url) + "/wake/request.json?auth=" + s_firebase.id_token;
  if (!https.begin(client, url)) {
    WAKE_LOG_SERIAL.println("Firebase GET begin failed");
    return false;
  }

  int code = https.GET();
  String body = https.getString();
  https.end();

  if (code == 401 || code == 403) {
    s_firebase.id_token = "";
    WAKE_LOG_SERIAL.println("Firebase GET auth expired, session reset");
    return false;
  }
  if (code != 200) {
    WAKE_LOG_SERIAL.printf("Firebase GET failed: HTTP %d body=%s\n", code, body.c_str());
    return false;
  }

  return parseWakeRequestRaw(body, recognized, is_one);
}

static bool firebaseResetWakeRequestFalse() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = String(s_config.database_url) + "/wake/request.json?auth=" + s_firebase.id_token;
  if (!https.begin(client, url)) {
    WAKE_LOG_SERIAL.println("Firebase PUT begin failed");
    return false;
  }

  https.addHeader("Content-Type", "application/json");
  int code = https.PUT("false");
  String body = https.getString();
  https.end();

  if (code == 401 || code == 403) {
    s_firebase.id_token = "";
    WAKE_LOG_SERIAL.println("Firebase PUT auth expired, session reset");
    return false;
  }
  if (code < 200 || code >= 300) {
    WAKE_LOG_SERIAL.printf("Firebase PUT failed: HTTP %d body=%s\n", code, body.c_str());
    return false;
  }
  return true;
}

static bool wake_trigger_usb() {
  // Reuse wake_core USB wake implementation as-is.
  return wake_trigger();
}
#endif

static void appSetup() {
#if WAKE_HAS_ARDUINO_USB_HID
  if (digitalRead(kBootButtonPin) == LOW) {
    WAKE_LOG_SERIAL.println("BOOT held at startup. Entering config mode.");
    startConfigMode();
    return;
  }

  if (!loadConfigNvs(&s_config)) {
    WAKE_LOG_SERIAL.println("No complete config in NVS. Starting config mode.");
    startConfigMode();
    return;
  }

  if (!connectWifiSta(s_config)) {
    WAKE_LOG_SERIAL.println("Wi-Fi unavailable. Entering config mode for safe standby.");
    startConfigMode();
    return;
  }

  s_lastPollMs = millis();
#endif
}

static void appLoop() {
#if WAKE_HAS_ARDUINO_USB_HID
  if (s_configMode) {
    s_setupServer.handleClient();
    if (s_restartQueued && timeReached(millis(), s_restartAtMs)) {
      delay(50);
      ESP.restart();
    }
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  uint32_t now = millis();
  if (!timeReached(now, s_lastPollMs + kFirebasePollIntervalMs)) {
    return;
  }
  s_lastPollMs = now;

  if (!firebaseEnsureSession(&s_firebase)) {
    return;
  }

  bool recognized = false;
  bool requested = false;
  if (!firebaseGetWakeRequest(&recognized, &requested)) {
    return;
  }
  if (!recognized) {
    WAKE_LOG_SERIAL.println("Firebase value parse error at /wake/request");
    return;
  }
  if (!requested) {
    return;
  }

  bool wake_sent = wake_trigger_usb();
  WAKE_LOG_SERIAL.printf("wake_trigger_usb -> %d\n", wake_sent ? 1 : 0);
  if (wake_sent) {
    if (!firebaseResetWakeRequestFalse()) {
      WAKE_LOG_SERIAL.println("Wake sent but reset-to-false failed");
    }
  } else {
    WAKE_LOG_SERIAL.println("Wake failed; keep /wake/request unchanged");
  }
#endif
}

static void appOnWakeButton(bool wakeSent) {
  (void)wakeSent;
  // Optional: add your own signal/report hook here.
}

void setup() {
  WAKE_LOG_SERIAL.begin(115200);
  delay(200);
  WAKE_LOG_SERIAL.println("wake minimal firebase build tag: 2026-04-21-min-firebase");
  WAKE_LOG_SERIAL.printf("WAKE_HAS_ARDUINO_USB_HID=%d\n", WAKE_HAS_ARDUINO_USB_HID ? 1 : 0);

#if WAKE_HAS_ARDUINO_USB_HID
  configureUsbForWake();
#endif

  esp_err_t err = wake_init();
  if (err != ESP_OK) {
    WAKE_LOG_SERIAL.printf("wake_init failed: %d\n", (int)err);
    return;
  }

  pinMode(kBootButtonPin, INPUT_PULLUP);
  s_lastBootLevel = (digitalRead(kBootButtonPin) == HIGH);

  WAKE_LOG_SERIAL.println("wake core ready");
  WAKE_LOG_SERIAL.println("press BOOT to request remote wake");

  appSetup();
}

void loop() {
  bool bootPressed = (digitalRead(kBootButtonPin) == LOW);
  if (bootPressed && s_lastBootLevel) {
    delay(20);  // debounce
    if (digitalRead(kBootButtonPin) == LOW) {
      bool wakeSent = wake_trigger();
      WAKE_LOG_SERIAL.printf("wake_trigger -> %d\n", wakeSent ? 1 : 0);
      appOnWakeButton(wakeSent);
    }
  }
  s_lastBootLevel = !bootPressed;

  appLoop();
  delay(10);
}
