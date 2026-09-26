#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

// Firmware-Update per WLAN (nur aktiv mit -DOTA_HTTP_ENABLE und gesetztem OTA_PASSWORD, siehe
// platformio.ini und tools/ota_upload.py). Ohne OTA_HTTP_ENABLE sind alle Funktionen leer.

// Ganz am Anfang von setup(): schaltet nach mehreren Abstürzen in Folge auf die vorige Firmware zurück
void otaBootGuard();

// Nach der WLAN-Verbindung: HTTP-Update-Server (/update, /info) und mDNS-Name nerdXXXX starten
void otaSetup();

// Aus loop() aufrufen
void otaLoop();

#endif
