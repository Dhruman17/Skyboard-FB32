#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <esp_now.h>
#include <esp_mac.h>
#include <time.h>
#include <credentials.h>

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
constexpr size_t kMaxNodes = 16;

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

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig firebaseConfig;
WiFiManager wifiManager;
Peer peers[kMaxNodes] = {};
GatewayConfig control = {kConfig, kProtocolVersion, false, {false, false, false}, {false, false, false}};
unsigned long lastControlSyncMs = 0;

String systemPath() {
  return "Systems/" + serialNumber;
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
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, sizeof(peerInfo.peer_addr));
  peerInfo.channel = 0;  // Use the C6 station's current Wi-Fi channel.
  peerInfo.encrypt = false;

  if (esp_now_is_peer_exist(mac)) {
    return esp_now_mod_peer(&peerInfo) == ESP_OK;
  }
  return esp_now_add_peer(&peerInfo) == ESP_OK;
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
  for (const Peer &peer : peers) {
    if (peer.used) {
      esp_now_send(peer.mac, reinterpret_cast<const uint8_t *>(&control), sizeof(control));
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
    rememberPeer(sender);
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

  publishControl();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[Gateway] Starting ESP32-C6 gateway.");

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

  firebaseConfig.api_key = API_KEY;
  firebaseConfig.token_status_callback = tokenStatusCallback;
  firebaseConfig.signer.reqTO = kAuthCooldownMs;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  Firebase.begin(&firebaseConfig, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (Firebase.ready()) {
    const unsigned long now = millis();
    if (now - lastControlSyncMs >= kControlSyncIntervalMs) {
      lastControlSyncMs = now;
      syncControlFromFirebase();
    }
  }
  delay(50);
}

}  // namespace Gateway

void setup() { Gateway::setup(); }
void loop() { Gateway::loop(); }
