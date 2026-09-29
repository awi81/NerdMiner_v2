# Session-Handover 2026-09-29

Stichworte: Session 2026-09-27 bis 29. Gheops Padding-Schritt ohne Gewinn, sieben Fixes aus Gheops all-fixes übernommen,
SHA1-/SHA512-Engine-Sperren, Ursache der Prüffehler eingegrenzt (TLS der HTTPS-Abrufe + Grundrauschen, harmlos),
zwei Antworten auf #727, Aufräumen nach globaler CLAUDE.md. Messprotokolle in `2026-09-27-session-handover.md`.
Diese Datei dient als Übergabe an die nächste Session.

---

## Ausgangslage

- Branch `awiEdition` auf `5b2c5b1` + Doku-Commit dieses Handovers, gepusht nach `origin`. `upstream/main` unverändert `e3a04b7`.
- Alle vier CYDs (nerd3CF0 links oben, nerd46BC, nerd7990, nerdFBD4) auf `cf81a51` (`-1.6.3-364-gcf81a51`), seit 2026-09-29 ~07:20.
  5-min-Kontrolle: 791–793 KH/s je Gerät, Summe 3165,5 KH/s, Selbsttest ok. `src/` ist identisch mit `d62209e`,
  das vorher 26 h ohne Neustart und ohne WLAN-Abbruch lief.
- Strom: drei Geräte am gemeinsamen Netzteil (1-auf-4-USB-C-Kabel), nerdFBD4 an eigenem Netzteil (egal, kein Unterschied gemessen).
- Kein Test, kein Logger, kein Zeitplan läuft. PR-Worktree liegt jetzt unter `D:\workspace_temp\NerdMiner_v2\pr-worktree`.

---

## Erledigt in dieser Session

| Commit / Link | Bereich | Was |
|---|---|---|
| `949d72b`…`d7b17e0` | `src/stratum.cpp`, `mining.cpp`, `utils.cpp`, `wManager.cpp`, `stratum.h` | Sieben Fixes aus Gheops all-fixes (Cherry-Pick, Autor bleibt Gheop): Nonce 8 Hex-Zeichen (sonst ~1/16 Shares abgelehnt), DNS-Fehler nicht cachen, Coinbase-Puffer, Keepalive-Difficulty, `to_byte_array`-UB, Reconnect-Backoff, wPass-Überlauf |
| `f06f90c`, `ef8ce77` | `src/otaUpdate.cpp`, `monitor.cpp` | `/info`: `wifi_disc`, `wifi_reason`, `api_calls`, `api_last_s` |
| `d62209e` | `src/mining.cpp` | HW-Miner sperrt SHA1- und SHA384/512-Engine dauerhaft (PR #826 erweitert); Korrektheit, kein messbarer Effekt |
| `e58749f` → `cf81a51` | `src/` | HW-Pause während HTTPS getestet und wieder entfernt |
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

### 1. Upstream beobachten

- #727: Gheops Reaktion auf den Pausentest-Kommentar (2026-09-29). Nichts zugesagt.
- #831–833: ohne Review (Stand 2026-09-29 07:20), konfliktfrei. Gheop: BitMaker-hub antwortet seit Juli auf keine PRs.

### 2. Optional, klein

- `checkError()` per Referenz statt 4-KB-Kopie auf den Stratum-Stack (`src/stratum.h:52`).
- TCP-Keepalive auf den Pool-Socket (Fork shaftfx, erkennt tote Verbindungen in ~14 s statt ~2 min).
- PR #725 (fremd): Auto-Helligkeit über den Lichtsensor der CYD.
- Unverändert: Versionsanzeige (`git describe` ohne `--tags`), `README.md:198`.

### 3. Bewusst nicht weiterverfolgt

- SW-Hashing in HW-Wartezeiten (User 2026-09-27), Padding-Schritt, Modem-Sleep aus, HW-Pause während HTTPS.

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
2. `~/.platformio/penv/Scripts/python.exe tools/ota_upload.py --list` (in `D:\workspace\NerdMiner_v2`): alle vier auf `-364-gcf81a51`, ohne Neustart?
3. Danach nach Bedarf Punkt 2 der offenen Punkte.
