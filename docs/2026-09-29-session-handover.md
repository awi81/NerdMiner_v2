# Session-Handover 2026-09-29

> ↪ Fortgeschrieben bis 2026-09-30 06:30; aktueller Stand und Übergabe: `2026-09-30-session-handover.md`.

Stichworte: Session 2026-09-27 bis 29. Gheops Padding-Schritt ohne Gewinn, sieben Fixes aus Gheops all-fixes übernommen,
SHA1-/SHA512-Engine-Sperren, Ursache der Prüffehler eingegrenzt (TLS der HTTPS-Abrufe + Grundrauschen, harmlos),
zwei Antworten auf #727, Aufräumen nach globaler CLAUDE.md. Messprotokolle in `2026-09-27-session-handover.md`.
Diese Datei dient als Übergabe an die nächste Session.

---

## Ausgangslage

- Branch `awiEdition` auf `5b2c5b1` + Doku-Commit dieses Handovers, gepusht nach `origin`. `upstream/main` unverändert `e3a04b7`.
- Alle vier CYDs (nerd3CF0 links oben, nerd46BC, nerd7990, nerdFBD4) auf `31c4e26` (`-1.6.3-372-g31c4e26`), seit 2026-09-30 ~05:05 (nerd3CF0/nerd46BC nach A/B-Test seit 06:28).
  5-min-Kontrolle: 789–791 KH/s je Gerät, Summe 3160,1 KH/s, Selbsttest ok. Vorher lief `cf81a51` 13,4 h ohne Neustart.
  `src/`-Änderungen seitdem: `checkError()` per Referenz, TCP-Keepalive, WLAN wählt stärksten Zugangspunkt, `/info` mit `bssid`/`chan`.
- WLAN: Im Netz hängen zwei Repeater mit derselben SSID „awi2“. Mit dem alten Fast-Scan landeten die Miner beim zuerst gefundenen
  Zugangspunkt, nach dem OTA am 2026-09-30 00:18 zwei davon bei -80…-84 dBm. Seit `1e7d0a0` alle vier direkt an der Fritz!Box
  (BSSID <BSSID Fritz!Box>, Kanal 6) mit -41…-54 dBm. Erkennbar auch in der ARP-Tabelle des PCs: Hinter einem Repeater
  stehen die Miner mit dessen MAC (32-16-9D-…, EE-B9-31-…), direkt an der Fritz!Box mit der eigenen (AC-15-18-…, CC-7B-5C-…).
- Strom: drei Geräte am gemeinsamen Netzteil (1-auf-4-USB-C-Kabel), nerdFBD4 an eigenem Netzteil (egal, kein Unterschied gemessen).
- Logger läuft seit 2026-09-30 05:23 (PID 50960, losgelöst): `D:\workspace_temp\NerdMiner_v2\poollog\poollog.py`, fragt alle 20 s `/info` ab
  und schreibt Pool-Neuverbindungen, WLAN-Abbrüche, Wechsel des Zugangspunkts, Neustarts, Nichterreichbarkeit und stündlich
  den Stand nach `poollog.txt`. Beenden, wenn nicht mehr gebraucht (`Stop-Process -Id 50960`).
  Bei „NICHT ERREICHBAR“ prüft er die Fritz!Box mit. Ping-Logger, PC-WLAN-Logger und ARP-Diagnose sind beendet (06:30).
- **PC-Netz: IP-Konflikt (Ursache der „Aussetzer“).** Der PC hat die feste IP 192.168.178.116 (von Hand gesetzt, auf
  `vEthernet (OpenClaw-External-Switch)`). Die Fritz!Box vergibt dieselbe IP per DHCP an ein Gerät mit zufälliger MAC
  <MAC Fremdgerät> (Handy/Tablet mit privater Adresse?). Windows meldet das seit August (System, Tcpip 4199, auch mit
  <Zufalls-MAC 2> und <Zufalls-MAC 3>). Meldet sich das Gerät (etwa alle 10 min), stellen Miner, Fritz!Box und andere
  Geräte ihren ARP-Eintrag für .116 auf dieses Gerät um und antworten dorthin, bis der PC den Eintrag korrigiert.
  Bei den Minern dauert das bis zu ~5 min. Belegt mit einer ARP-Diagnose-Firmware auf nerd3CF0 (nicht committet):
  `163:<Fremdgerät> 172:<PC> 173:<Fremdgerät> 189:<PC>` (Laufzeit s: MAC für .116), genau beim Aussetzer um 06:26.
  Mining ist nicht betroffen (Pool läuft über die Fritz!Box als Gateway, `pool_conn` blieb 1). Betroffen: Zugriff vom PC
  (`/info`, Messungen, OTA-Abbrüche 00:16/05:05 → wiederholen). Das schwache 5-GHz-Signal des PCs ist nicht die Ursache.
  **Behebung entscheidet der User** (außerhalb des Repos): PC auf DHCP und in der Fritz!Box .116 fest für MAC
  <MAC PC> reservieren, oder PC-IP außerhalb des DHCP-Bereichs legen. Gerät mit <MAC Fremdgerät> in der
  Fritz!Box-Netzwerkliste nachsehen.
- A/B Modem-Sleep aus (06:01–06:28, nerd3CF0/nerd46BC): Ping 6–7 ms statt 113 ms, beseitigt die Ausfälle aber nicht
  (nerd3CF0 fiel auch ohne Sleep aus). Zurückgenommen, alle vier wieder auf `31c4e26`. In einem 5-min-Fenster lagen die
  beiden ohne Sleep 0,4 % vorn (HW-Leerlauf 0,1 statt 0,3 %); früher ohne Effekt gemessen → nicht weiterverfolgt.
- Sonst kein Test und kein Zeitplan. PR-Worktree liegt unter `D:\workspace_temp\NerdMiner_v2\pr-worktree`.

---

## Erledigt in dieser Session

| Commit / Link | Bereich | Was |
|---|---|---|
| `949d72b`…`d7b17e0` | `src/stratum.cpp`, `mining.cpp`, `utils.cpp`, `wManager.cpp`, `stratum.h` | Sieben Fixes aus Gheops all-fixes (Cherry-Pick, Autor bleibt Gheop): Nonce 8 Hex-Zeichen (sonst ~1/16 Shares abgelehnt), DNS-Fehler nicht cachen, Coinbase-Puffer, Keepalive-Difficulty, `to_byte_array`-UB, Reconnect-Backoff, wPass-Überlauf |
| `f06f90c`, `ef8ce77` | `src/otaUpdate.cpp`, `monitor.cpp` | `/info`: `wifi_disc`, `wifi_reason`, `api_calls`, `api_last_s` |
| `d62209e` | `src/mining.cpp` | HW-Miner sperrt SHA1- und SHA384/512-Engine dauerhaft (PR #826 erweitert); Korrektheit, kein messbarer Effekt |
| `e58749f` → `cf81a51` | `src/` | HW-Pause während HTTPS getestet und wieder entfernt |
| `7d2ee8a` | `src/stratum.cpp`, `stratum.h` | `checkError()` nimmt das JSON-Dokument per Referenz (vorher 4-KB-Kopie auf den Stack) |
| `5c44b2a` | `src/mining.cpp`, `otaUpdate.cpp` | TCP-Keepalive auf den Pool-Socket (10 s ruhig, dann alle 5 s, nach 3 ohne Antwort weg → tote Verbindung nach ≤ 25 s erkannt); `/info`: `pool_conn` (Verbindungen seit Start), `pool_gap_s` (beim letzten Abbruch: Sekunden seit den letzten Pool-Daten). Per Test-Build geprüft: Socket meldet `idle 10 intvl 5 cnt 3` |
| `1e7d0a0` | `src/wManager.cpp` | WLAN: Scan aller Kanäle, Wahl nach Signalstärke (vorher erster gefundener Zugangspunkt); gilt auch für `WiFi.reconnect()` und automatisches Wiederverbinden, weil in der gespeicherten Konfiguration gesetzt |
| `31c4e26` | `src/otaUpdate.cpp` | `/info`: `bssid`, `chan` |
| `70e5ded` | `.gitignore` | `firmware/` (Build-Ausgabe) ignoriert |
| [#727 (1)](https://github.com/BitMaker-hub/NerdMiner_v2/pull/727#issuecomment-5862577511), [#727 (2)](https://github.com/BitMaker-hub/NerdMiner_v2/pull/727#issuecomment-5884180667) | Upstream | Padding-Nachtest, Stromversorgung, Prüffehler-Zeitpunkte; Ergebnis Pausentest |
| – | `D:\workspace` | Aufräumen: PR-Worktree und alte `firmware/`-Builds nach `D:\workspace_temp\NerdMiner_v2`, Eintrag in `REPOS.md` |

**Ergebnisse (Details und Tabellen: `2026-09-27-session-handover.md`):**

- Padding TEXT[8]/[15] während Block 2 (Gheop +2,2 %): bei uns +0,4 % / ±0 → nicht übernommen. Gheop: sein Loop hatte `memw` nach START.
- Prüffehler ~2–3/h je Gerät: ~1/h durch TLS der Pool-API-Abrufe (Pausentest: mit Pause verschwindet die Häufung),
  Rest ~1,8/h Grundrauschen. Kein Effekt: eigenes Netzteil, SHA-Sperren, Modem-Sleep aus. Harmlos (~0,01 % Hashes).
- HW-Pause kostet 2,7 s je Abruf = 0,3 % → nicht übernommen. Modem-Sleep aus: kein Effekt → nicht übernommen.
- WLAN: 0 Abbrüche in allen Läufen; ~40 OTA-Updates seit dem WLAN-Fix ohne Ausfall.

---

## Offene Punkte für die nächste Session

### 0. User fragen

- IP-Konflikt des PCs (192.168.178.116, siehe Ausgangslage): User am 2026-09-30 empfohlen, .116 in der Fritz!Box fest für
  <MAC PC> zu reservieren (IP bleibt, Hermes/moneyBot nutzt ComfyUI/TTS auf dem PC), ggf. Eintrag <MAC Fremdgerät> löschen.
  Danach prüfen: `/info` aller Miner über 30 min ohne „NICHT ERREICHBAR“ in `poollog.txt`.

### 1. Upstream beobachten

- #727: Gheops Reaktion auf den Pausentest-Kommentar (2026-09-29 05:21 UTC). Stand 2026-09-30 05:44: keine Antwort.
- #831–833: ohne Review und ohne Kommentar (Stand 2026-09-30 05:44), konfliktfrei. Gheop: BitMaker-hub antwortet seit Juli auf keine PRs.

### 2. Optional, klein

- Keepalive: Nachtlauf 2026-09-30 00:18–05:03 (4,7 h, zwei Geräte bei -80…-84 dBm) ohne einen einzigen Neuaufbau (`pool_conn` = 1),
  kein WLAN-Abbruch → keine Fehlalarme. Echter Test einer toten Verbindung bräuchte eine Firewall-Regel mit Admin-Rechten
  (Proxy auf dem PC, Verbindung sperren); nicht nötig, Werte sind per Test-Build am Socket belegt.
- Prüffehler im Nachtlauf: 13/15/9/21 in 4,7 h (2,8/3,2/1,9/4,5 je h), im bekannten harmlosen Bereich.

### 3. Bewusst nicht weiterverfolgt

- SW-Hashing in HW-Wartezeiten (User 2026-09-27), Padding-Schritt, Modem-Sleep aus, HW-Pause während HTTPS.
- Auto-Helligkeit (PR #725): User will sie nicht (2026-09-30).
- Versionsanzeige: Der CYD-Bildschirm zeigt `CURRENT_VERSION` („V1.8.3“); `AUTO_VERSION` (`git describe` ohne `--tags`,
  „-1.6.3-…“) steht nur in `/info`, wo der Commit-Hash zählt. `README.md:198` („huge app“) ist Upstream-Text. Beides bleibt so.

---

## Wichtige Konventionen aus dieser Session (Memory-relevant)

- Miner-Entscheidungen (Tests, Fixes, OTA) trifft Claude selbst. **Why:** User 2026-09-28 „den Rest zu den Minern entscheidest du“.
  **How to apply:** nur bei Physischem (Umstecken, BOOT), Destruktivem oder vorab gewünschter Textprüfung fragen.
- Allgemeines Wissen in die globale `~/.claude/CLAUDE.md`. **Why:** User 2026-09-28. **How to apply:** Rechner/Netz/Werkzeuge dort, Miner-Spezifisches in die Projekt-Memory.
- Prüffehler streuen stark zwischen Boards. **Why:** Gheop misst bis 15-fach, unsere Raten schwanken auch je Nacht (nerd7990 3,1 → 1,9/h).
  **How to apply:** gegen eigene Vorher-Rate und gleichzeitige Kontrollgeräte vergleichen, ≥ 8 h, Zeitpunkte mitloggen.
- Schalter im Binary prüfen: `xtensa-esp32-elf-objdump -d`, `callx8` in der Funktion zählen (Flash→IRAM-Aufrufe sind indirekt).

---

## Aktionen in der neuen Session — Empfehlung

1. `gh pr view 727 831 832 833 -R BitMaker-hub/NerdMiner_v2` (bzw. einzeln) auf Antworten/Reviews prüfen.
2. `~/.platformio/penv/Scripts/python.exe tools/ota_upload.py --list` (in `D:\workspace\NerdMiner_v2`): alle vier auf `-372-g31c4e26`, ohne Neustart, `bssid` <BSSID Fritz!Box>? `poollog.txt` auf Ereignisse prüfen.
3. Danach nach Bedarf Punkt 2 der offenen Punkte.
