#if SKYBOARD_HUB_MODE

#include <hub_node_config.h>
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_idf_version.h>
#include <nvs.h>

namespace HubNode {
constexpr uint8_t kVersion = 2, kHeartbeat = 0x01, kConfig = 0x02, kPairRequest = 0x10, kPairResponse = 0x11, kPairConfirm = 0x12;
constexpr uint8_t kBroadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
struct __attribute__((packed)) PairRequest { uint8_t type, version, mac[6], channel; };
struct __attribute__((packed)) PairResponse { uint8_t type, version, mac[6], channel; };
struct __attribute__((packed)) PairConfirm { uint8_t type, version; };
struct __attribute__((packed)) Heartbeat { uint8_t type, version; uint16_t firmware; };
struct __attribute__((packed)) GatewayConfig { uint8_t type, version; bool light; bool misting[3]; bool occupied[3]; };

uint8_t gatewayMac[6] = {}; bool paired = false, desiredLight = false, desiredMisting[3] = {};
uint8_t gatewayChannel = 1;
uint8_t pairingChannel = 1;
unsigned long lastPair = 0, lastHeartbeat = 0, lastToggle[3] = {}; bool atomizerOn[3] = {};

void savePeer(uint8_t channel) { nvs_handle_t h; if (nvs_open("hubpair", NVS_READWRITE, &h) == ESP_OK) { nvs_set_blob(h, "mac", gatewayMac, 6); nvs_set_u8(h, "ch", channel); nvs_commit(h); nvs_close(h); } }
bool loadPeer() {
  nvs_handle_t h;
  size_t macLength = sizeof(gatewayMac);
  uint8_t channel = 0;
  if (nvs_open("hubpair", NVS_READONLY, &h) != ESP_OK) return false;
  esp_err_t macResult = nvs_get_blob(h, "mac", gatewayMac, &macLength);
  esp_err_t channelResult = nvs_get_u8(h, "ch", &channel);
  nvs_close(h);
  if (macResult != ESP_OK || macLength != sizeof(gatewayMac) ||
      channelResult != ESP_OK || channel < 1 || channel > 13) return false;
  gatewayChannel = channel;
  return true;
}
void addPeer(const uint8_t *mac, uint8_t channel) { esp_now_peer_info_t peer = {}; memcpy(peer.peer_addr, mac, 6); peer.channel = channel; peer.encrypt = false; if (esp_now_is_peer_exist(mac)) esp_now_mod_peer(&peer); else esp_now_add_peer(&peer); }
void selectChannel(uint8_t channel) { esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); }
void sendPair() { PairRequest r = {kPairRequest, kVersion, {}, pairingChannel}; esp_read_mac(r.mac, ESP_MAC_WIFI_STA); addPeer(kBroadcast, r.channel); esp_now_send(kBroadcast, reinterpret_cast<uint8_t *>(&r), sizeof(r)); }
void applyOutputs() { digitalWrite(SYSTEM_LIGHTS_PIN, desiredLight ? HIGH : LOW); for (int i=0;i<3;i++) { if (!desiredMisting[i]) { ledcWrite(atomizerPins[i], PWM_ATOMIZER_OFF); atomizerOn[i]=false; continue; } unsigned long interval=atomizerOn[i]?200000UL:3000UL; if (millis()-lastToggle[i]>=interval) { atomizerOn[i]=!atomizerOn[i]; lastToggle[i]=millis(); ledcWrite(atomizerPins[i], atomizerOn[i]?PWM_ATOMIZER_ON:PWM_ATOMIZER_OFF); } } }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void received(const esp_now_recv_info_t *, const uint8_t *data, int length) {
#else
void received(const uint8_t *, const uint8_t *data, int length) {
#endif
  if (length < 2 || data[1] != kVersion) return;
  if (data[0] == kPairResponse && length >= static_cast<int>(sizeof(PairResponse))) { PairResponse r; memcpy(&r,data,sizeof(r)); gatewayChannel=r.channel; selectChannel(gatewayChannel); memcpy(gatewayMac,r.mac,6); addPeer(gatewayMac,gatewayChannel); PairConfirm c={kPairConfirm,kVersion}; esp_now_send(gatewayMac,reinterpret_cast<uint8_t*>(&c),sizeof(c)); savePeer(gatewayChannel); paired=true; Serial.println("[HUB] Paired with Gateway"); }
  if (data[0] == kConfig && length >= static_cast<int>(sizeof(GatewayConfig))) { GatewayConfig c; memcpy(&c,data,sizeof(c)); desiredLight=c.light; for(int i=0;i<3;i++) desiredMisting[i]=c.misting[i]; Serial.println("[HUB] Gateway control update applied"); }
}
void setup() { Serial.begin(115200); WiFi.mode(WIFI_STA); WiFi.disconnect(); pinMode(SYSTEM_12V_POWER_PIN,OUTPUT); digitalWrite(SYSTEM_12V_POWER_PIN,HIGH); pinMode(SYSTEM_LIGHTS_PIN,OUTPUT); digitalWrite(SYSTEM_LIGHTS_PIN,LOW); for(int i=0;i<3;i++){ledcAttachChannel(atomizerPins[i],PWM_FREQUENCY_ATOMIZER,PWM_RESOLUTION_ATOMIZER,i);ledcWrite(atomizerPins[i],PWM_ATOMIZER_OFF);lastToggle[i]=millis();} paired=loadPeer(); pairingChannel=paired?gatewayChannel:1; selectChannel(pairingChannel); if(esp_now_init()!=ESP_OK){Serial.println("[HUB] ESP-NOW init failed");ESP.restart();} if(paired){addPeer(gatewayMac,gatewayChannel); Serial.println("[HUB] Restored Gateway pairing");} esp_now_register_recv_cb(received); esp_wifi_set_ps(WIFI_PS_NONE); Serial.println("[HUB] Node ready; pair it from the Gateway."); }
void loop() { unsigned long now=millis(); if(!paired && now-lastPair>=500){lastPair=now; selectChannel(pairingChannel); sendPair(); pairingChannel=pairingChannel==13?1:pairingChannel+1;} if(paired && now-lastHeartbeat>=15000){lastHeartbeat=now;Heartbeat h={kHeartbeat,kVersion,HUB_FIRMWARE_VERSION_CENTIS};esp_now_send(gatewayMac,reinterpret_cast<uint8_t*>(&h),sizeof(h));} applyOutputs(); delay(50); }
} // namespace HubNode
void setup(){HubNode::setup();}
void loop(){HubNode::loop();}
#endif
