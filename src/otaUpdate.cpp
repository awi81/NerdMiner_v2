#include <Arduino.h>
#include "otaUpdate.h"

#ifdef OTA_HTTP_ENABLE

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <ESPmDNS.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include "version.h"
#include "utils.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif
#define OTA_USER "nerdminer"

#ifdef AUTO_VERSION
#define OTA_VERSION AUTO_VERSION
#else
#define OTA_VERSION CURRENT_VERSION
#endif

// Nach so vielen Abstürzen in Folge (Panic/Watchdog) auf die Firmware im anderen Slot zurückschalten
#define OTA_CRASH_LIMIT 3
// Läuft die Firmware so lange, gilt sie als stabil und der Absturzzähler wird zurückgesetzt
#define OTA_STABLE_ms (60 * 1000)
#define OTA_GUARD_MAGIC 0x4E4D4F54

extern uint32_t elapsedKHs;
extern uint32_t hashesHw;
extern uint32_t hashesSw;
extern uint32_t lastDrawDurationMs;
extern uint32_t hwChecked;
extern uint32_t hwErrors;
extern uint32_t hwIdle;
extern int hwKat;
extern char hwBench[];

static WebServer s_server(80);
static HTTPUpdateServer s_updater;
static bool s_running = false;
static char s_host[16];
static String s_md5;
static int s_resetReason = 0;  // esp_reset_reason() beim Start (/info)
static volatile uint32_t s_wifiDisc = 0;  // WLAN-Abbrüche seit dem Start (/info)
static volatile int s_wifiReason = 0;     // Grund des letzten Abbruchs (wifi_err_reason_t, 16 = Gruppenschlüssel)

// RTC-Speicher überlebt Neustarts durch Absturz/Watchdog (nicht aber einen Stromausfall)
RTC_NOINIT_ATTR static uint32_t s_guardMagic;
RTC_NOINIT_ATTR static uint32_t s_crashCount;

static bool isCrashReset(esp_reset_reason_t reason)
{
  return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

void otaBootGuard()
{
  esp_reset_reason_t reason = esp_reset_reason();
  s_resetReason = (int)reason;
  bool crash = isCrashReset(reason);
  if (s_guardMagic != OTA_GUARD_MAGIC || !crash)
  {
    s_guardMagic = OTA_GUARD_MAGIC;
    s_crashCount = 0;
  }
  if (crash)
    s_crashCount++;

  const esp_partition_t* running = esp_ota_get_running_partition();
  Serial.printf("[OTA] Firmware aus %s, Reset-Grund %d, Abstürze in Folge: %u\n",
                running ? running->label : "?", (int)reason, s_crashCount);

  if (s_crashCount >= OTA_CRASH_LIMIT)
  {
    s_crashCount = 0;
    const esp_partition_t* other = esp_ota_get_next_update_partition(running);
    // esp_ota_set_boot_partition prüft das Image und lehnt einen leeren/defekten Slot ab
    if (other && esp_ota_set_boot_partition(other) == ESP_OK)
    {
      Serial.printf("[OTA] %d Abstürze in Folge -> zurück auf die Firmware in %s\n", OTA_CRASH_LIMIT, other->label);
      delay(100);
      esp_restart();
    }
    Serial.println("[OTA] Keine gültige Firmware im anderen Slot, bleibe bei der aktuellen");
  }

#ifdef OTA_CRASH_TEST
  // Nur zum Testen des Rückfalls (Build mit -DOTA_CRASH_TEST): stürzt bei jedem Start ab
  Serial.println("[OTA] Absturztest");
  delay(100);
  abort();
#endif
}

static void handleInfo()
{
  const esp_partition_t* running = esp_ota_get_running_partition();
  char json[640];
  snprintf(json, sizeof(json),
           "{\"host\":\"%s\",\"version\":\"%s\",\"partition\":\"%s\",\"md5\":\"%s\",\"uptime_s\":%lu,\"khs\":%u,\"heap\":%u,"
           "\"hw_hashes\":%u,\"sw_hashes\":%u,\"rssi\":%d,\"draw_ms\":%u,\"hw_checked\":%u,\"hw_errors\":%u,\"hw_kat\":%d,\"hw_idle\":%u,\"hw_bench\":\"%s\",\"reset_reason\":%d,"
           "\"wifi_disc\":%u,\"wifi_reason\":%d}",
           s_host, OTA_VERSION, running ? running->label : "?", s_md5.c_str(),
           (unsigned long)(millis() / 1000), elapsedKHs, ESP.getFreeHeap(), hashesHw, hashesSw, (int)WiFi.RSSI(), lastDrawDurationMs, hwChecked, hwErrors, hwKat, hwIdle, hwBench, s_resetReason,
           s_wifiDisc, s_wifiReason);
  s_server.send(200, "application/json", json);
}

void otaSetup()
{
  if (strlen(OTA_PASSWORD) == 0)
  {
    Serial.println("[OTA] Kein OTA_PASSWORD beim Build gesetzt -> Update per WLAN deaktiviert");
    return;
  }
  // Gleicher Name wie der automatische Worker-Name beim Pool (mining.cpp)
  getDeviceName(s_host, sizeof(s_host));
  s_md5 = ESP.getSketchMD5();  // MD5 der laufenden Firmware, zum Abgleich mit der .bin-Datei
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    s_wifiDisc++;
    s_wifiReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

#ifndef OTA_NO_MDNS
  if (MDNS.begin(s_host))
    MDNS.addService("http", "tcp", 80);
#endif

  s_updater.setup(&s_server, "/update", OTA_USER, OTA_PASSWORD);
  s_server.on("/info", HTTP_GET, handleInfo);
  s_server.begin();
  s_running = true;
  Serial.printf("[OTA] Update per WLAN bereit: http://%s.local/update bzw. http://%s/update\n",
                s_host, WiFi.localIP().toString().c_str());
}

// Wartung über die USB-Konsole (115200 Baud), geht ohne BOOT-Taste:
//   NM-WIFI {"ssid":"...","pass":"..."}  WLAN-Zugang speichern und neu starten
//   NM-INFO                               Name, IP und Firmware-Slot ausgeben
static void handleSerialCommand(const String& line)
{
  if (line == "NM-INFO")
  {
    const esp_partition_t* running = esp_ota_get_running_partition();
    Serial.printf("[USB] %s IP %s SSID '%s' Slot %s Version %s\n", s_host, WiFi.localIP().toString().c_str(),
                  WiFi.SSID().c_str(), running ? running->label : "?", OTA_VERSION);
  }
  else if (line.startsWith("NM-WIFI "))
  {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, line.substring(8)) || !doc["ssid"].is<const char*>())
    {
      Serial.println("[USB] NM-WIFI: erwartet {\"ssid\":\"...\",\"pass\":\"...\"}");
      return;
    }
    const char* ssid = doc["ssid"];
    const char* pass = doc["pass"] | "";
    String oldSsid = WiFi.SSID();
    String oldPass = WiFi.psk();

    // Erst testen und nur bei Erfolg speichern: mit falschem Passwort bliebe das Gerät sonst nach dem
    // Neustart im Einrichtungsportal hängen, wo es keine USB-Befehle mehr annimmt
    Serial.printf("[USB] Teste WLAN '%s' ...\n", ssid);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    WiFi.disconnect();
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000)
      delay(250);

    if (WiFi.status() == WL_CONNECTED)
    {
      wifi_config_t conf;
      esp_wifi_get_config(WIFI_IF_STA, &conf);
      esp_wifi_set_storage(WIFI_STORAGE_FLASH);
      esp_wifi_set_config(WIFI_IF_STA, &conf);  // jetzt dauerhaft im NVS, dort liest WiFiManager beim Start
      Serial.printf("[USB] WLAN '%s' verbunden (IP %s), gespeichert, starte neu\n", ssid, WiFi.localIP().toString().c_str());
      delay(500);
      ESP.restart();
    }
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    Serial.printf("[USB] WLAN '%s' nicht erreichbar oder Passwort falsch, bleibe bei '%s'\n", ssid, oldSsid.c_str());
    WiFi.disconnect();
    WiFi.begin(oldSsid.c_str(), oldPass.c_str());
  }
}

static void pollSerial()
{
  static String buf;
  while (Serial.available())
  {
    char c = Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (buf.length())
        handleSerialCommand(buf);
      buf = "";
    }
    else if (buf.length() < 240)
      buf += c;
  }
}

void otaLoop()
{
  if (s_crashCount != 0 && millis() > OTA_STABLE_ms)
    s_crashCount = 0;
  pollSerial();
  if (s_running)
    s_server.handleClient();
}

#else

void otaBootGuard() {}
void otaSetup() {}
void otaLoop() {}

#endif
