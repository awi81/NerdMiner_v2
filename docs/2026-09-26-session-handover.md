# Session-Handover 2026-09-26

Stichworte: Update per WLAN für die vier CYD-NerdMiner, Hashrate 370 → 467 KH/s je Gerät,
drei Upstream-PRs. Diese Datei dient als Übergabe an die nächste Session.

---

## Ausgangslage

- Branch `awiEdition` auf `9d93ebd` (Stand 2026-06-19), Upstream `upstream/main` unverändert auf `e3a04b7`.
- Vier ESP32-2432S028 (CYD) in einem Bilderrahmen, je ~370 KH/s, im Gäste-WLAN der Fritz!Box
  (vom PC nicht erreichbar). Flashen nur per USB mit BOOT-Taste (GPIO0 ist nicht mit dem USB-Chip verbunden).

---

## Erledigt in dieser Session

| Commit / PR | Bereich | Was |
|---|---|---|
| `a82fef0` | OTA | `partitions_cyd_ota.csv` (2 × 1,5 MB, nvs/spiffs/coredump wie `huge_app.csv`), `src/otaUpdate.cpp`: `/update` (Passwort aus `ota_secret.ini`, gitignored), `/info`, Absturzschutz (3 Abstürze → anderer Slot), USB-Befehl `NM-WIFI` zum WLAN-Wechsel |
| `a82fef0` | Bilder | Hintergrundbilder zlib-komprimiert (805 → 88 KB, `tools/compress_images.py`, Decoder `src/drivers/displays/zImage.cpp` mit ROM-tinfl) → Firmware 2,15 → 1,46 MB |
| `a82fef0` | Hashrate | Miner-Schleifen nicht im IRAM (344 → 413), SHA-Register mit eingebettetem APB-Vorlesen statt IDF-Funktionsaufruf (413 → 467); jeder HW-Treffer wird in Software nachgerechnet (`hw_errors` in `/info`, bisher 0) |
| `a82fef0` | Fixes | `checkValid` + Target-Umkehrung, API-Retry-Bremse, Screen-Wechsel (`^=`), Auth/Reject-Auswertung, eindeutige Gerätenamen aus den letzten MAC-Bytes (`getDeviceName`), 3 WLAN-Versuche beim Start, Display alle 5 s, Loop-Task Prio 6 (Update-Server vor Display) |
| `a82fef0` | Werkzeuge | `tools/usb_flash_ota.py` (einmaliger USB-Schritt), `tools/ota_upload.py` (Update per WLAN, sucht alle Miner, prüft MD5), `tools/hashrate.py` (HW/SW-Hashrate + Prüffehler) |
| Geräte | alle 4 | auf OTA umgestellt, im WLAN `awi2` (2,4 GHz), Namen `nerd3CF0`, `nerd46BC`, `nerdFBD4`, `nerd7990`; zusammen 1867 KH/s |
| [#831](https://github.com/BitMaker-hub/NerdMiner_v2/pull/831), [#832](https://github.com/BitMaker-hub/NerdMiner_v2/pull/832), [#833](https://github.com/BitMaker-hub/NerdMiner_v2/pull/833) | Upstream | API-Retry, `checkValid`, CYD-Screenwechsel; Branches im Worktree `D:\workspace\NerdMiner_v2_upstream` |
| [#727 Kommentare](https://github.com/BitMaker-hub/NerdMiner_v2/pull/727) | Upstream | Messdaten zu IRAM (−17 % mit Schleife im IRAM) und DPORT (direkt = 100 % falsche Hashes, APB-inline = +13 %) |

---

## Offene Punkte für die nächste Session

### 1. Weitere SHA-Optimierung nach Gheop (PR #727)

✅ ERLEDIGT (2026-09-26, Commit `a193daa`): 467 → 792 KH/s je Gerät, siehe `2026-09-26-session-handover-abend.md`.

- Gheop meldet auf einem DevKit ohne Display: 12 redundante APB-Writes pro Nonce weglassen (+6,9 %),
  Text-Fills und Engine-Befehle in Assembler mit `SHA_TEXT_BASE` in einem Register (573 KH/s gesamt).
- Code: `src/mining.cpp`, Abschnitt `#if defined(CONFIG_IDF_TARGET_ESP32)` (Helfer `nerd_sha_ll_*`, `minerWorkerHw`).
- Vorgehen wie bisher: Variante per Build-Flag in eigenem `PLATFORMIO_BUILD_DIR`, auf ein Gerät per
  `tools/ota_upload.py --firmware …`, messen mit `tools/hashrate.py`; nur übernehmen bei 0 Prüffehlern.
- Option: Gheops angekündigten PR abwarten und testen statt selbst zu schreiben.

### 2. `awiEdition` pushen

- `a82fef0` und der Doc-Commit liegen nur lokal. Push nach `origin` nicht ohne Rückfrage.
- ✅ ERLEDIGT (2026-09-26, gepusht bis `e54495b`; Commit und Push seitdem ohne Rückfrage erlaubt).

### 3. Upstream-PRs #831–833 beobachten

- Auf Reviews antworten; Branches `fix/api-retry-backoff`, `fix/check-valid`, `fix/cyd-screen-change` im Worktree.

### 4. Ungeklärt

- 4-Minuten-Ausfall von nerdFBD4 nach einem Update (Zeitfenster passt zum Einrichtungs-Hotspot, 180 s).
  `/info` meldet jetzt `reset_reason`; beim nächsten Vorfall prüfen. Option: Hotspot-Zeitlimit verkürzen.
  ✅ Ursache gefunden und behoben (2026-09-26, Commit `a193daa`): WiFiManager beendet den blockierenden Hotspot nicht bei Hintergrundverbindung.
- Upload dauerte 5–6 min, als das Display jede Sekunde zeichnete; Ursache nicht belegt (Zeichnen dauert nur 136 ms).
- Versionsanzeige `-1.6.3-339-g…`: `auto_firmware_version.py` nutzt `git describe` ohne `--tags`, findet daher das alte annotierte Tag.
- `README.md:198` („huge app“) gilt nicht mehr für das CYD-Env; Upstream-Datei, bewusst nicht geändert.

---

## Wichtige Konventionen aus dieser Session (Memory-relevant)

- Updates nur mit `tools/ota_upload.py` bzw. `*_firmware.bin` an 0x10000, nie `*_factory.bin` (löscht NVS/WLAN).
- Messen per A/B: ein Testgerät, drei Kontrollen, Zählerdifferenzen aus `/info` (`hw_hashes`/`sw_hashes`) — Anzeige-Hashrate allein kann Schein sein.
- Registertricks an der SHA-Engine nur mit Software-Prüfung jedes Treffers (`hw_errors` muss 0 bleiben).
  (Überholt 2026-09-26: mit rohen Reads ist ~1 von 15.000 Kandidaten falsch und wird verworfen; maßgeblich sind Selbsttest `hw_kat` und Kandidatenquote.)
- Testbuilds in eigenem `PLATFORMIO_BUILD_DIR`; `post_build_merge.py` kopiert trotzdem nach `firmware/`, dort kann danach ein Testbuild liegen.
- Build nur über `python -m platformio` (`pio.exe` ist per Richtlinie blockiert).

---

## Aktionen in der neuen Session — Empfehlung

1. SHA-Optimierung nach Gheop testen (siehe Punkt 1), auf `nerdFBD4` mit `tools/ota_upload.py --firmware <testbuild> <IP>` und `tools/hashrate.py`.
2. Nach Rückfrage `git push origin awiEdition` in `D:\workspace\NerdMiner_v2`.

(1. und 2. ✅ ERLEDIGT 2026-09-26, siehe `2026-09-26-session-handover-abend.md`.)
3. Status von PR #831–833 und #727 prüfen (`gh pr view <nr> -R BitMaker-hub/NerdMiner_v2`).
