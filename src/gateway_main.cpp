#include <Arduino.h>
#include <SD.h>  // C6-safe project stub; Firebase includes this unconditionally.
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <esp_now.h>
#include <esp_mac.h>
#include <time.h>
#include <credentials.h>
#include "gateway_sensors.h"
#include "gateway_display.h"

namespace Gateway {

constexpr uint8_t kProtocolVersion = 2;
constexpr uint8_t kHeartbeat = 0x01;
constexpr uint8_t kConfig = 0x02;
constexpr uint8_t kPairRequest = 0x10;
constexpr uint8_t kPairResponse = 0x11;
constexpr uint8_t kPairConfirm = 0x12;
constexpr unsigned long kControlSyncIntervalMs = 45000UL;
constexpr unsigned long kAuthCooldownMs = 15UL * 60UL * 1000UL;
constexpr unsigned long kNormalAuthRequestTimeoutMs = 2000UL;
constexpr unsigned long kSensorReadIntervalMs = 5000UL;
constexpr unsigned long kPilotPollIntervalMs = 35000UL;
constexpr unsigned long kPilotHeartbeatIntervalMs = 10000UL;
constexpr unsigned long kPilotTelemetryIntervalMs = 30000UL;
constexpr unsigned long kPilotRetryMinMs = 5000UL;
constexpr unsigned long kPilotRetryMaxMs = 300000UL;
constexpr size_t kMaxNodes = 100;
constexpr char kPilotConfigUrl[] = "https://us-central1-skyacres-marketplace.cloudfunctions.net/hubPilotConfig";
constexpr char kPilotHeartbeatUrl[] = "https://us-central1-skyacres-marketplace.cloudfunctions.net/hubPilotHeartbeat";
constexpr char kPilotTelemetryUrl[] = "https://us-central1-skyacres-marketplace.cloudfunctions.net/hubPilotTelemetry";

// Google Trust Services root used by the Google-hosted Functions endpoint.
// This is a trust anchor; TLS hostname and chain verification remain enabled.
constexpr char kGoogleGtsRootR1[] = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICjTCCAjOgAwIBAgIQf/NXf/Y8fKN+BkL4yLhikDAKBggqhkjOPQQDAjBQMSQwIgYDVQQLExtH
bG9iYWxTaWduIEVDQyBSb290IENBIC0gUjQxEzARBgNVBAoTCkdsb2JhbFNpZ24xEzARBgNVBAMT
Ckdsb2JhbFNpZ24wHhcNMjMxMjEzMDkwMDAwWhcNMjkwMjIwMTQwMDAwWjA7MQswCQYDVQQGEwJV
UzEeMBwGA1UEChMVR29vZ2xlIFRydXN0IFNlcnZpY2VzMQwwCgYDVQQDEwNXRTIwWTATBgcqhkjO
PQIBBggqhkjOPQMBBwNCAAQ1fh/yFO2QfeGeKjRDhsHVlugncN+eBMupyoZ5CwhNRorCdKS72b/u
/SPXOPNL71QX4b7nylUlqAwwrC1dTqFRo4IBAjCB/zAOBgNVHQ8BAf8EBAMCAYYwHQYDVR0lBBYw
FAYIKwYBBQUHAwEGCCsGAQUFBwMCMBIGA1UdEwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFHW+xHeu
ifZEN33PsWgfHRrr3DRZMB8GA1UdIwQYMBaAFFSwe61FuOJAf/sKbvu+M8k8o4TVMDYGCCsGAQUF
BwEBBCowKDAmBggrBgEFBQcwAoYaaHR0cDovL2kucGtpLmdvb2cvZ3NyNC5jcnQwLQYDVR0fBCYw
JDAioCCgHoYcaHR0cDovL2MucGtpLmdvb2cvci9nc3I0LmNybDATBgNVHSAEDDAKMAgGBmeBDAEC
ATAKBggqhkjOPQQDAgNIADBFAiBbsQQLhG0VbSTee24xTExpfZjtvxSr0QUeF5ynbsLfsAIhALzH
r/nsu7YViuT13ZNVPKTQ4UWHvxnqxJZRGRgNHE4w
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIB3DCCAYOgAwIBAgINAgPlfvU/k/2lCSGypjAKBggqhkjOPQQDAjBQMSQwIgYDVQQLExtHbG9i
YWxTaWduIEVDQyBSb290IENBIC0gUjQxEzARBgNVBAoTCkdsb2JhbFNpZ24xEzARBgNVBAMTCkds
b2JhbFNpZ24wHhcNMTIxMTEzMDAwMDAwWhcNMzgwMTE5MDMxNDA3WjBQMSQwIgYDVQQLExtHbG9i
YWxTaWduIEVDQyBSb290IENBIC0gUjQxEzARBgNVBAoTCkdsb2JhbFNpZ24xEzARBgNVBAMTCkds
b2JhbFNpZ24wWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAS4xnnTj2wlDp8uORkcA6SumuU5BwkW
ymOxuYb4ilfBV85C+nOh92VC/x7BALJucw7/xyHlGKSq2XE/qNS5zowdo0IwQDAOBgNVHQ8BAf8E
BAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUVLB7rUW44kB/+wpu+74zyTyjhNUwCgYI
KoZIzj0EAwIDRwAwRAIgIk90crlgr/HmnKAWBVBfw147bmF0774BxL4YSFlhgjICICadVGNA3jdg
UM/I2O2dgq43mLyjj0xMqTQrbO/7lZsm
-----END CERTIFICATE-----
)CERT";

struct __attribute__((packed)) PairRequest {
  uint8_t type;
  uint8_t version;
  uint8_t mac[6];
  uint8_t channel;
};

struct __attribute__((packed)) PairResponse {
  uint8_t type;
  uint8_t version;
  uint8_t mac[6];
  uint8_t channel;
};

struct __attribute__((packed)) GatewayConfig {
  uint8_t type;
  uint8_t version;
  bool light;
  bool misting[3];
  bool occupied[3];
};

struct Peer {
  uint8_t mac[6];
  bool used;
};

struct PilotAssignment {
  bool valid;
  uint8_t mac[6];
  String systemId;
  GatewayConfig control;
  unsigned long lastHeartbeatMs;
  unsigned long nextHeartbeatMs;
  unsigned long heartbeatBackoffMs;
};

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig firebaseConfig;
WiFiManager wifiManager;
Preferences pilotPreferences;
Peer peers[kMaxNodes] = {};
PilotAssignment pilotAssignments[kMaxNodes] = {};
GatewayConfig control = {kConfig, kProtocolVersion, false, {false, false, false}, {false, false, false}};
unsigned long lastControlSyncMs = 0;
unsigned long lastSensorReadMs = 0;
unsigned long nextPilotPollMs = 0;
unsigned long pilotRetryMs = kPilotRetryMinMs;
unsigned long nextPilotTelemetryMs = 0;
unsigned long pilotTelemetryRetryMs = kPilotTelemetryIntervalMs;
String hubId;
bool pilotApiEnabled = true;
bool pilotNtpValid = false;
String onboardingState = "ready";
volatile int pendingPilotHeartbeatIndex = -1;
uint8_t activeEspNowPeer[6] = {};
bool hasActiveEspNowPeer = false;

String systemPath() {
  return "Systems/" + serialNumber;
}

String normalizeMac(const uint8_t *mac) {
  char value[13] = {};
  snprintf(value, sizeof(value), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(value);
}

bool normalizeMacText(const char *input, String &normalized) {
  if (input == nullptr) return false;
  char value[13] = {};
  size_t count = 0;
  for (const char *cursor = input; *cursor != '\0'; ++cursor) {
    if (*cursor == ':' || *cursor == '-' || *cursor == ' ') continue;
    if (!isxdigit(static_cast<unsigned char>(*cursor)) || count >= 12) return false;
    value[count++] = static_cast<char>(tolower(static_cast<unsigned char>(*cursor)));
  }
  if (count != 12) return false;
  value[count] = '\0';
  normalized = String(value);
  return true;
}

bool macTextMatches(const JsonVariantConst &value, const uint8_t *mac) {
  if (value.isNull()) return false;
  String normalized;
  if (value.is<const char *>()) {
    if (!normalizeMacText(value.as<const char *>(), normalized)) return false;
  } else if (value.is<int>()) {
    return false;
  } else {
    return false;
  }
  return normalized == normalizeMac(mac);
}

String unitIdText(const JsonVariantConst &value) {
  if (value.is<const char *>()) return String(value.as<const char *>());
  if (value.is<int>()) return String(value.as<int>());
  return String();
}

bool currentTimeValid() {
  return time(nullptr) >= 1700000000;
}

String iso8601Now() {
  time_t now = time(nullptr);
  struct tm utc = {};
  gmtime_r(&now, &utc);
  char value[25] = {};
  strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(value);
}

int peerIndex(const uint8_t *mac) {
  for (size_t index = 0; index < kMaxNodes; ++index) {
    if (peers[index].used && memcmp(peers[index].mac, mac, 6) == 0) return static_cast<int>(index);
  }
  return -1;
}

void persistPilotConfig() {
  pilotPreferences.begin("hub-pilot", false);
  pilotPreferences.putString("onboarding", onboardingState);
  pilotPreferences.end();
}

void loadPilotConfig() {
  pilotPreferences.begin("hub-pilot", true);
  pilotApiEnabled = true;
  onboardingState = pilotPreferences.getString("onboarding", "ready");
  pilotPreferences.end();
}

void setOnboardingState(const char *next) {
  onboardingState = next;
  persistPilotConfig();
  Serial.printf("[Gateway] Hub onboarding state: %s\n", onboardingState.c_str());
}

void clearPilotAssignments() {
  for (PilotAssignment &assignment : pilotAssignments) {
    assignment.valid = false;
    assignment.systemId = String();
    assignment.lastHeartbeatMs = 0;
    assignment.nextHeartbeatMs = 0;
    assignment.heartbeatBackoffMs = kPilotRetryMinMs;
  }
}

bool addPeer(const uint8_t *mac);

void sendControlToPeer(const Peer &peer, const GatewayConfig &configuration) {
  if (!addPeer(peer.mac)) {
    Serial.println("[Gateway] Unable to select ESP-NOW peer for control.");
    return;
  }
  Serial.printf("[Gateway] Sending config to %02X:%02X:%02X:%02X:%02X:%02X channel=%d light=%d unit1=%d unit2=%d unit3=%d\n",
                peer.mac[0], peer.mac[1], peer.mac[2], peer.mac[3], peer.mac[4], peer.mac[5],
                WiFi.channel(), configuration.light, configuration.misting[0], configuration.misting[1], configuration.misting[2]);
  const esp_err_t sendResult = esp_now_send(peer.mac, reinterpret_cast<const uint8_t *>(&configuration), sizeof(configuration));
  Serial.printf("[Gateway] ESP-NOW send result to %02X:%02X:%02X:%02X:%02X:%02X: %d\n",
                peer.mac[0], peer.mac[1], peer.mac[2], peer.mac[3], peer.mac[4], peer.mac[5], sendResult);
}

bool parsePilotNode(JsonObjectConst node) {
  const JsonVariantConst nodeMac = node["nodeMac"];
  int matched = -1;
  for (size_t index = 0; index < kMaxNodes; ++index) {
    if (peers[index].used && macTextMatches(nodeMac, peers[index].mac)) {
      matched = static_cast<int>(index);
      break;
    }
  }
  if (matched < 0) return false;

  const char *systemIdValue = node["systemId"] | "";
  JsonObjectConst controls = node["controls"].as<JsonObjectConst>();
  if (systemIdValue[0] == '\0' || controls.isNull()) return false;

  PilotAssignment &assignment = pilotAssignments[matched];
  assignment.valid = false;
  memcpy(assignment.mac, peers[matched].mac, sizeof(assignment.mac));
  assignment.systemId = String(systemIdValue);
  assignment.control = {kConfig, kProtocolVersion, false, {false, false, false}, {false, false, false}};

  JsonObjectConst systemControls = controls["system"].as<JsonObjectConst>();
  if (!systemControls.isNull() && !systemControls["Light_Master_Switch"].isNull()) {
    assignment.control.light = systemControls["Light_Master_Switch"].as<bool>();
  }

  JsonArrayConst units = controls["units"].as<JsonArrayConst>();
  if (!units.isNull()) {
    for (JsonObjectConst unit : units) {
      String unitId = unitIdText(unit["unitId"]);
      if (unitId == "1" || unitId == "2" || unitId == "3") {
        const int slot = unitId.toInt() - 1;
        assignment.control.misting[slot] = unit["unitState"] | false;
      }
    }
  }

  assignment.valid = true;
  return true;
}

void applyPilotAssignments(JsonDocument &document) {
  clearPilotAssignments();
  JsonArrayConst nodes = document["nodes"].as<JsonArrayConst>();
  if (nodes.isNull()) {
    Serial.println("[Gateway] Hub Pilot config response missing nodes; no controls applied.");
    return;
  }

  int applied = 0;
  for (JsonObjectConst node : nodes) {
    if (parsePilotNode(node)) ++applied;
  }

  for (size_t index = 0; index < kMaxNodes; ++index) {
    if (pilotAssignments[index].valid) {
      sendControlToPeer(peers[index], pilotAssignments[index].control);
    }
  }
  Serial.printf("[Gateway] Hub Pilot config applied to %d paired node(s); manual light/unit controls active, schedules/timers unsupported.\n", applied);
}

void pilotHttpHeaders(HTTPClient &http) {
  http.addHeader("X-Hub-Id", hubId);
  http.addHeader("X-Hub-Mode", "manual");
  http.addHeader("Accept", "application/json");
}

void schedulePilotRetry() {
  nextPilotPollMs = millis() + pilotRetryMs;
  pilotRetryMs = min(kPilotRetryMaxMs, pilotRetryMs * 2UL);
}

bool fetchPilotConfig() {
  if (!pilotApiEnabled || !pilotNtpValid || WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setCACert(kGoogleGtsRootR1);
  client.setTimeout(10);
  HTTPClient http;
  if (!http.begin(client, kPilotConfigUrl)) {
    Serial.println("[Gateway] Hub Pilot config connection setup failed; retrying with backoff.");
    schedulePilotRetry();
    return false;
  }
  pilotHttpHeaders(http);
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[Gateway] Hub Pilot config HTTP %d; retrying with backoff.\n", status);
    http.end();
    schedulePilotRetry();
    return false;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, http.getStream());
  http.end();
  if (error) {
    Serial.println("[Gateway] Hub Pilot config JSON invalid; retrying with backoff.");
    schedulePilotRetry();
    return false;
  }

  applyPilotAssignments(document);
  pilotRetryMs = kPilotRetryMinMs;
  nextPilotPollMs = millis() + kPilotPollIntervalMs;
  return true;
}

bool sendPilotHeartbeat(int index) {
  if (index < 0 || index >= static_cast<int>(kMaxNodes) || !pilotAssignments[index].valid || !pilotNtpValid || !currentTimeValid()) return false;
  const unsigned long nowMs = millis();
  if (nowMs - pilotAssignments[index].lastHeartbeatMs < kPilotHeartbeatIntervalMs || nowMs < pilotAssignments[index].nextHeartbeatMs) return false;

  WiFiClientSecure client;
  client.setCACert(kGoogleGtsRootR1);
  client.setTimeout(10);
  HTTPClient http;
  if (!http.begin(client, kPilotHeartbeatUrl)) return false;
  pilotHttpHeaders(http);

  JsonDocument document;
  document["hubId"] = hubId;
  document["nodeMac"] = normalizeMac(pilotAssignments[index].mac);
  document["systemId"] = pilotAssignments[index].systemId;
  document["reportedAt"] = iso8601Now();
  document["connected"] = true;
  document["status"] = "online";
  String body;
  serializeJson(document, body);
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(body);
  http.end();
  if (status < 200 || status >= 300) {
    Serial.printf("[Gateway] Hub Pilot heartbeat HTTP %d; retry deferred.\n", status);
    pilotAssignments[index].nextHeartbeatMs = nowMs + pilotAssignments[index].heartbeatBackoffMs;
    pilotAssignments[index].heartbeatBackoffMs = min(kPilotRetryMaxMs, pilotAssignments[index].heartbeatBackoffMs * 2UL);
    return false;
  }
  pilotAssignments[index].lastHeartbeatMs = nowMs;
  pilotAssignments[index].nextHeartbeatMs = nowMs + kPilotHeartbeatIntervalMs;
  pilotAssignments[index].heartbeatBackoffMs = kPilotRetryMinMs;
  return true;
}

int activePilotAssignmentIndex() {
  int activeIndex = -1;
  int activeCount = 0;
  for (size_t index = 0; index < kMaxNodes; ++index) {
    if (!pilotAssignments[index].valid) continue;
    activeIndex = static_cast<int>(index);
    ++activeCount;
  }
  if (activeCount == 1) return activeIndex;
  return activeCount == 0 ? -1 : -2;
}

bool pilotSensorReadingsValid() {
  return GatewaySensors::available &&
         isfinite(GatewaySensors::temperatureC) &&
         isfinite(GatewaySensors::humidityPct) &&
         GatewaySensors::humidityPct >= 0.0f &&
         GatewaySensors::humidityPct <= 100.0f &&
         GatewaySensors::co2Ppm > 0;
}

void schedulePilotTelemetryRetry(unsigned long nowMs) {
  nextPilotTelemetryMs = nowMs + pilotTelemetryRetryMs;
  pilotTelemetryRetryMs = min(kPilotRetryMaxMs, pilotTelemetryRetryMs * 2UL);
}

void scheduleNextPilotTelemetry(unsigned long nowMs) {
  nextPilotTelemetryMs = nowMs + kPilotTelemetryIntervalMs;
  pilotTelemetryRetryMs = kPilotTelemetryIntervalMs;
}

void sendPilotTelemetry() {
  const unsigned long nowMs = millis();
  if (nowMs < nextPilotTelemetryMs || !pilotApiEnabled ||
      WiFi.status() != WL_CONNECTED || !pilotNtpValid || !currentTimeValid()) {
    return;
  }

  const int assignmentIndex = activePilotAssignmentIndex();
  if (assignmentIndex == -1) {
    Serial.println("[Gateway] Pilot telemetry skipped: no active assignment.");
    scheduleNextPilotTelemetry(nowMs);
    return;
  }
  if (assignmentIndex == -2) {
    Serial.println("[Gateway] Pilot telemetry skipped: multiple active assignments.");
    scheduleNextPilotTelemetry(nowMs);
    return;
  }
  if (!pilotSensorReadingsValid()) {
    Serial.println("[Gateway] Pilot telemetry skipped: invalid sensor readings.");
    scheduleNextPilotTelemetry(nowMs);
    return;
  }

  const PilotAssignment &assignment = pilotAssignments[assignmentIndex];
  const String nodeMac = normalizeMac(assignment.mac);
  WiFiClientSecure client;
  client.setCACert(kGoogleGtsRootR1);
  client.setTimeout(10);
  HTTPClient http;
  if (!http.begin(client, kPilotTelemetryUrl)) {
    Serial.printf("[Gateway] Pilot telemetry connection failed node=%s system=%s; retry deferred.\n",
                  nodeMac.c_str(), assignment.systemId.c_str());
    schedulePilotTelemetryRetry(nowMs);
    return;
  }
  pilotHttpHeaders(http);

  JsonDocument document;
  document["hubId"] = hubId;
  document["nodeMac"] = nodeMac;
  document["systemId"] = assignment.systemId;
  document["reportedAt"] = iso8601Now();
  document["temperatureC"] = GatewaySensors::temperatureC;
  document["humidityPercent"] = GatewaySensors::humidityPct;
  document["co2Ppm"] = GatewaySensors::co2Ppm;
  String body;
  serializeJson(document, body);
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(body);
  http.end();

  if (status < 200 || status >= 300) {
    Serial.printf("[Gateway] Pilot telemetry HTTP %d node=%s system=%s temp=%.1f humidity=%.1f co2=%u; retry deferred.\n",
                  status, nodeMac.c_str(), assignment.systemId.c_str(),
                  GatewaySensors::temperatureC, GatewaySensors::humidityPct, GatewaySensors::co2Ppm);
    schedulePilotTelemetryRetry(nowMs);
    return;
  }

  Serial.printf("[Gateway] Pilot telemetry accepted node=%s system=%s temp=%.1f humidity=%.1f co2=%u.\n",
                nodeMac.c_str(), assignment.systemId.c_str(),
                GatewaySensors::temperatureC, GatewaySensors::humidityPct, GatewaySensors::co2Ppm);
  scheduleNextPilotTelemetry(nowMs);
}

void tokenStatusCallback(TokenInfo info) {
  ::tokenStatusCallback(info);

  if (info.status == token_status_ready) {
    firebaseConfig.signer.reqTO = kNormalAuthRequestTimeoutMs;
    return;
  }

  if (info.status == token_status_error) {
    const String message = info.error.message.c_str();
    if (message == "TOO_MANY_ATTEMPTS_TRY_LATER" || message == "bad request") {
      firebaseConfig.signer.reqTO = kAuthCooldownMs;
      Serial.println("[Gateway] Firebase authentication throttled; retrying in 15 minutes.");
    }
  }
}

bool addPeer(const uint8_t *mac) {
  if (hasActiveEspNowPeer && memcmp(activeEspNowPeer, mac, sizeof(activeEspNowPeer)) == 0) return true;
  if (hasActiveEspNowPeer) {
    esp_now_del_peer(activeEspNowPeer);
    hasActiveEspNowPeer = false;
  }
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, sizeof(peerInfo.peer_addr));
  peerInfo.channel = 0;  // Use the C6 station's current Wi-Fi channel.
  peerInfo.encrypt = false;

  const bool added = esp_now_is_peer_exist(mac) ? esp_now_mod_peer(&peerInfo) == ESP_OK : esp_now_add_peer(&peerInfo) == ESP_OK;
  if (added) {
    memcpy(activeEspNowPeer, mac, sizeof(activeEspNowPeer));
    hasActiveEspNowPeer = true;
  }
  return added;
}

void rememberPeer(const uint8_t *mac) {
  for (Peer &peer : peers) {
    if (peer.used && memcmp(peer.mac, mac, sizeof(peer.mac)) == 0) {
      return;
    }
  }

  for (Peer &peer : peers) {
    if (!peer.used) {
      memcpy(peer.mac, mac, sizeof(peer.mac));
      peer.used = true;
      Serial.printf("[Gateway] Node paired: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      return;
    }
  }

  Serial.println("[Gateway] Node table full; ignoring additional node.");
}

void publishControl() {
  if (pilotApiEnabled) return;
  for (const Peer &peer : peers) {
    if (peer.used) {
      sendControlToPeer(peer, control);
    }
  }
}

void receive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (length < 2 || data[1] != kProtocolVersion) {
    return;
  }

  const uint8_t *sender = info->src_addr;
  if (data[0] == kPairRequest && length >= static_cast<int>(sizeof(PairRequest))) {
    if (!addPeer(sender)) {
      Serial.println("[Gateway] Unable to add ESP-NOW peer.");
      return;
    }

    rememberPeer(sender);
    PairResponse response = {kPairResponse, kProtocolVersion, {}, static_cast<uint8_t>(WiFi.channel())};
    esp_read_mac(response.mac, ESP_MAC_WIFI_STA);
    esp_now_send(sender, reinterpret_cast<const uint8_t *>(&response), sizeof(response));
    publishControl();
  } else if (data[0] == kHeartbeat) {
    addPeer(sender);
    rememberPeer(sender);
    if (pilotApiEnabled) {
      pendingPilotHeartbeatIndex = peerIndex(sender);
    }
  } else if (data[0] == kPairConfirm) {
    rememberPeer(sender);
  }
}

bool readBooleanField(FirebaseJson &json, const char *path, bool &value) {
  FirebaseJsonData data;
  if (!json.get(data, path)) {
    return false;
  }
  value = data.boolValue;
  return true;
}

void syncControlFromFirebase() {
  const String path = systemPath();
  if (!Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", path.c_str())) {
    Serial.printf("[Gateway] System control fetch failed: %s\n", fbdo.errorReason().c_str());
    return;
  }

  FirebaseJson systemJson;
  systemJson.setJsonData(fbdo.payload());
  readBooleanField(systemJson, "fields/Light_Master_Switch/booleanValue", control.light);

  for (int index = 0; index < 3; ++index) {
    const String unitPath = path + "/units/" + String(index + 1);
    if (!Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", unitPath.c_str())) {
      Serial.printf("[Gateway] Unit %d fetch failed: %s\n", index + 1, fbdo.errorReason().c_str());
      continue;
    }

    FirebaseJson unitJson;
    unitJson.setJsonData(fbdo.payload());
    readBooleanField(unitJson, "fields/unitState/booleanValue", control.misting[index]);
  }

  Serial.printf("[Gateway] Firebase controls path=%s light=%d unit1=%d unit2=%d unit3=%d\n",
                path.c_str(), control.light, control.misting[0], control.misting[1], control.misting[2]);
  publishControl();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[Gateway] Starting ESP32-C6 gateway.");

  loadPilotConfig();
  uint8_t localMac[6] = {};
  esp_read_mac(localMac, ESP_MAC_WIFI_STA);
  hubId = normalizeMac(localMac);
  setOnboardingState("ready");
  Serial.printf("[Gateway] Hub %s onboarding=%s. Captive portal configures Wi-Fi only.\n", hubId.c_str(), onboardingState.c_str());

  GatewaySensors::begin();
  GatewayDisplay::begin();

  wifiManager.setConnectTimeout(20);
  wifiManager.setConfigPortalTimeout(60);
  const String portalName = "SkyAcres Gateway " + serialNumber;
  if (!wifiManager.autoConnect(portalName.c_str())) {
    Serial.println("[Gateway] Wi-Fi unavailable; gateway will retry after restart.");
    delay(1000);
    ESP.restart();
  }

  Serial.printf("[Gateway] Wi-Fi connected on channel %d.\n", WiFi.channel());
  if (esp_now_init() != ESP_OK) {
    Serial.println("[Gateway] ESP-NOW initialization failed; restarting.");
    delay(1000);
    ESP.restart();
  }
  esp_now_register_recv_cb(receive);

  // TLS token requests require a valid clock, just as the legacy direct
  // controller does before starting Firebase.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const unsigned long ntpStart = millis();
  while (time(nullptr) < 1700000000 && millis() - ntpStart < 10000UL) {
    delay(100);
  }
  pilotNtpValid = currentTimeValid();
  Serial.printf("[Gateway] Hub Pilot NTP epoch=%lld valid=%d.\n",
                static_cast<long long>(time(nullptr)), pilotNtpValid ? 1 : 0);

  if (!pilotNtpValid) {
    Serial.println("[Gateway] Hub Pilot API waiting for valid NTP; no API requests will be sent.");
  }
}

void loop() {
  const unsigned long now = millis();
  if (now - lastSensorReadMs >= kSensorReadIntervalMs) {
    lastSensorReadMs = now;
    GatewaySensors::read();
    int pairedCount = 0;
    for (const Peer &peer : peers) {
      if (peer.used) ++pairedCount;
    }
    GatewayDisplay::update(GatewaySensors::temperatureC,
                           GatewaySensors::humidityPct,
                           GatewaySensors::co2Ppm,
                           pairedCount,
                           false);
  }

  if (pilotApiEnabled) {
    if (!pilotNtpValid && currentTimeValid()) {
      pilotNtpValid = true;
      nextPilotPollMs = 0;
    }
    if (pilotNtpValid && now >= nextPilotPollMs) {
      fetchPilotConfig();
    }
    sendPilotTelemetry();
    const int heartbeatIndex = pendingPilotHeartbeatIndex;
    if (heartbeatIndex >= 0) {
      pendingPilotHeartbeatIndex = -1;
      sendPilotHeartbeat(heartbeatIndex);
    }
  }
  delay(50);
}

}  // namespace Gateway

void setup() { Gateway::setup(); }
void loop() { Gateway::loop(); }
