# Session-Handover 2026-10-07

Stichworte: Session 2026-09-30 abends bis 2026-10-07. Git-Historie bereinigt (MACs/BSSID), IP-Konflikt des PCs
gefunden und in der Fritz!Box behoben, WLAN-Roaming eingebaut und verteilt, Pool-Logger über mehrere Tage, mehrere
PC-Abschaltungen und Umzug der Session nach Mission Control. Details im Verlauf von `2026-09-30-session-handover.md`
(Nachträge 03.10., 04.10., 06.10.). Diese Datei dient als Übergabe an die nächste Session.

---

## Ausgangslage

- Branch `awiEdition`, gepusht nach `origin`. `upstream/main` unverändert.
- Alle vier CYDs auf `426f0e9` (`-1.6.3-379-g426f0e9`, MD5 `4750fbd1…`) seit 2026-10-06 00:40. Stand 2026-10-07 01:00:
  alle an der Fritz!Box (Kanal 1, -47…-49 dBm), kein Neustart, kein WLAN-Abbruch, `wifi_roam` 0, Selbsttest ok,
  757–828 KH/s je Gerät.
- Pool-Logger läuft (PID 44936, eigener Prozess): `D:\workspace_temp\NerdMiner_v2\poollog\poollog.py` → `poollog.txt`.
  Beenden: `Stop-Process -Id 44936`. Neustart: `python poollog.py poollog.txt` in diesem Ordner.
- Die Session läuft seit 2026-10-06 in Mission Control (Dashboard `http://127.0.0.1:9900`).

---

## Erledigt in dieser Session

| Commit | Bereich | Was |
|---|---|---|
| `5b4a1b5` | Historie | Doku-Commits mit MACs/BSSID nach `31c4e26` zu einem Commit zusammengefasst, Force-Push (mit Freigabe). Alte Commits auf GitHub per SHA noch abrufbar; Support-Antrag vom User abgelehnt. Sicherung: `D:\workspace_temp\NerdMiner_v2\backup\awiEdition-vor-bereinigung-2026-09-30.bundle` |
| – | Netz | IP-Konflikt des PCs (.116) aufgeklärt: Hermes geht über die WLAN-Brücke mit der PC-MAC ins Netz, die Fritz!Box führte die PC-MAC unter der .164 und vergab die .116 an ein Handy. User hat die .116 am 01.10. fest für den PC reserviert. Am 06.10. früh hatte das Handy die .116 noch einmal; Stand 07.10. hat es eine andere IP (inaktiv) |
| `426f0e9` | `src/wManager.cpp`, `src/otaUpdate.cpp` | WLAN-Roaming: alle 10 min bei Signal < -55 dBm Hintergrundsuche nach derselben SSID, bei ≥ 10 dB stärkerem Zugangspunkt `WiFi.reconnect()`. `/info`: `wifi_roam`. Schwellen per Build-Flag. Anlass: Kanalwechsel der Fritz!Box (6 → 1) am 04.10., zwei Miner blieben danach am Repeater |
| – | Test | Testvariante (jede Minute, immer wechseln) auf nerd3CF0: 6 Wechsel in 6 min, kein Absturz, Mining lief weiter |
| – | Betrieb | nerdFBD4 nach Umstecken (03.10.) am Repeater → per OTA neu gestartet; 06.10. Roaming-Firmware auf alle vier |
| – | Neu | Repo `D:\workspace\Sessions` angelegt (lokal, kein GitHub), in `REPOS.md` eingetragen |

---

## Offene Punkte für die nächste Session

### 1. Roaming im Ernstfall

- Nicht getestet: echter Wechsel Repeater → Fritz!Box durch die Prüfung (nach dem Update wählten alle schon beim Start richtig).
- Risiko: Sieht der Verbindungsaufbau den stärkeren Zugangspunkt nicht, verbindet der Miner alle 10 min neu. Erkennbar an
  steigendem `wifi_roam` bei gleichem `bssid`. Abhilfe dann: Wartezeit nach erfolglosem Wechsel verlängern.

### 2. Kanalwechsel der Fritz!Box (Frage an den User)

- Am 04.10. ~19:14 von Kanal 6 auf 1, danach viele WLAN-Abbrüche. WLAN-Einstellungen per TR-064 nur mit Login.
  Vermutlich Autokanal; User soll unter WLAN → Funkkanal nachsehen.

### 3. Gemeinsame Pool-Neuverbindungen

- Alle vier verbinden sich mehrmals am Tag gleichzeitig neu (z. B. 06.10. 18:43, 20:10, 21:41), Internet lief durch →
  vermutlich Pool-seitig (public-pool.io). Kein Handlungsbedarf, solange die Lücken kurz bleiben (1–76 s).

### 4. Upstream beobachten

- #727, #831–833: Stand 2026-10-06 keine Reaktion.

---

## Wichtige Konventionen aus dieser Session (Memory-relevant)

- Fritz!Box-Geräteliste und Internet-Uptime sind ohne Login abfragbar (TR-064 `Hosts:1`, UPnP `WANIPConnection:1#GetStatusInfo`
  → `NewUptime`); WLAN-Einstellungen nicht (HTTP 401). Steht in der globalen CLAUDE.md.
- Neustart eines Miners ohne Rahmen öffnen: gleiche Firmware per `tools/ota_upload.py --force <IP>` (es gibt keinen
  Neustart-Befehl in `/info`).
- Vor einem Herunterfahren des PCs den Pool-Logger beenden und im Handover vermerken; nach dem Neustart wieder starten.

---

## Aktionen in der neuen Session — Empfehlung

1. `D:\workspace_temp\NerdMiner_v2\poollog\poollog.txt` auf `POOL`/`WLAN`/`AP`/`NEUSTART`/`NICHT ERREICHBAR` seit
   2026-10-07 prüfen; `/info` aller vier: `wifi_roam`, `bssid`, `chan`.
2. User nach dem Funkkanal der Fritz!Box fragen (offener Punkt 2).
3. Läuft alles ruhig: Pool-Logger beenden (`Stop-Process -Id 44936`).
4. `gh pr view 727 831 832 833 -R BitMaker-hub/NerdMiner_v2` auf Reaktionen prüfen.

---

## Nachtrag 2026-10-08 19:51

- Pool-Logger 07.10. 01:00 bis 08.10. 19:51: einziges Ereignis nerd46BC `POOL` 08.10. 13:46 (Lücke 25 s, nur dieses
  Gerät). Kein Neustart, kein WLAN-Abbruch, kein AP-Wechsel. Logger beendet (PID 44936).
- `/info` 19:51: alle vier `wifi_roam` 0, Fritz!Box-BSSID, Kanal 1, -46…-52 dBm, Laufzeit ~67 h.
- Funkkanal: laut User „müsste fest eingestellt sein“ (nicht geprüft). Dann war der Wechsel 6 → 1 am 04.10. kein
  Autokanal. Ursache offen; klären ließe sie sich im Fritz!Box-Ereignisprotokoll (System → Ereignisse, WLAN) um
  04.10. ~19:14. Mit der Roaming-Firmware niedrige Priorität.
- Upstream #727, #831–833: weiter keine Reaktion (letzte Aktivität 29.09. bzw. 26.09.).
- Offen bleibt nur Punkt 1 (Roaming im Ernstfall), testbar erst beim nächsten Repeater-Fall.
