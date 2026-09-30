# Session-Handover 2026-09-27 bis 29

> Abgelöst durch `2026-09-29-session-handover.md` (Endstand der Session). Diese Datei bleibt als Messprotokoll
> (Padding-Nachtest, Nachttest Prüffehler, Pausentest, Fork-Suche).

Stichworte: Gheops Padding-Schritt nachgetestet (kein Gewinn), sieben Fixes aus Gheops all-fixes übernommen,
Fork-Suche, zwei Nachttests zu den Prüffehlern (Netzteil, SHA-Sperren, HTTPS, Modem-Sleep, HW-Pause), zwei Antworten auf #727.
Vorgänger: `2026-09-26-session-handover-abend.md` (Hashrate 467 → 792 KH/s, Messdetails).

---

## Ausgangslage für die nächste Session

- Branch `awiEdition` auf `cf81a51` (+ dieser Doku-Commit), gepusht nach `origin`. `upstream/main` unverändert `e3a04b7`.
  `src/` von `cf81a51` ist identisch mit `d62209e`.
- Alle vier CYDs auf `cf81a51` (Version `-1.6.3-364-gcf81a51`, MD5 `0c61c0c3…`), seit 2026-09-29 ~07:20.
  Kontrollmessung 5 min: 791–793 KH/s je Gerät (HW 753–754), Summe 3165,5 KH/s, Selbsttest ok.
  Davor lief `d62209e` 26 h ohne Neustart und ohne WLAN-Abbruch.
- Kein Logger und kein Test läuft mehr.
- Stromversorgung: alle vier an einem Netzteil über ein 1-auf-4-USB-C-Kabel, nur **nerdFBD4** hängt seit
  2026-09-27 23:10 an einem eigenen Netzteil (vom User umgesteckt; kann so bleiben oder zurück, macht keinen Unterschied).
- Position im Rahmen: nerd3CF0 = links oben (übrige unbekannt).
- PR-Worktree für Upstream-PRs jetzt unter `D:\workspace_temp\NerdMiner_v2\pr-worktree` (Aufräumen nach globaler CLAUDE.md).

---

## Erledigt in dieser Session

| Commit / Link | Was |
|---|---|
| `949d72b`…`d7b17e0` | Sieben Fixes von Gheop (all-fixes) per Cherry-Pick: Nonce auf 8 Hex-Zeichen, DNS-Fehler nicht cachen, Coinbase-Puffer 1024, Keepalive-Difficulty, `to_byte_array`-UB, Reconnect-Backoff 1→15 s, wPass-Überlauf (Konflikt mit Auto-Worker-Name gelöst) |
| `f06f90c`, `ef8ce77` | `/info` neu: `wifi_disc`, `wifi_reason` (WLAN-Abbrüche), `api_calls`, `api_last_s` (HTTPS-Abrufe) |
| `d62209e` | HW-Miner sperrt SHA1- und SHA384/512-Engine dauerhaft (mbedTLS rechnet diese dann in Software); Testschalter entfernt |
| `70e5ded` | `firmware/` (Build-Ausgabe von `post_build_merge.py`) in `.gitignore` |
| [#727 Kommentar](https://github.com/BitMaker-hub/NerdMiner_v2/pull/727#issuecomment-5862577511) | Padding-Nachtest, Stromversorgung, Zeitpunkte der Prüffehler |
| `e58749f`, `cf81a51` | HW-Pause während HTTPS als Test (Vorschlag Gheop) und wieder entfernt, Ergebnis siehe unten |
| [#727 Kommentar](https://github.com/BitMaker-hub/NerdMiner_v2/pull/727#issuecomment-5884180667) | Ergebnis des Pausentests |

### Padding-Nachtest (Gheop `84a54f7`, TEXT[8]/[15] während Block 2 nach 48 nop) — nicht übernommen

10 min Baseline aller vier, dann 20 min A/B, HW-Anteil:

| Gerät | Rolle | vorher | nachher | Δ |
|---|---|---|---|---|
| nerd3CF0 | Padding 48 nop | 752,5 | 756,7 | +0,56 % |
| nerd46BC | Padding 48 nop | 753,8 | 754,6 | +0,11 % |
| nerd7990 | unverändert | 754,2 | 755,2 | +0,13 % |
| nerdFBD4 | unverändert | 753,3 | 754,5 | +0,16 % |

Um die Kontrolldrift bereinigt +0,4 % bzw. ±0 → kein Gewinn auf beiden Geräten (Gheop: +2,2 %). Passt zum Engine-Limit.

### Nachttest Prüffehler (2026-09-28 00:29–05:00, Logger mit 15-s-Auflösung)

| Gerät | Variante | Laufzeit | Fehler/h | davon ±30 s um HTTPS-Abruf | WLAN-Abbrüche |
|---|---|---|---|---|---|
| nerd3CF0 | ohne HTTPS-Abrufe (Pool-API aus) | 4,0 h | 1,8 | – | 0 |
| nerd46BC | Modem-Sleep aus | 3,5 h | 2,9 | 6 von 10 | 0 |
| nerd7990 | unverändert, Verteiler | 4,5 h | 2,6 | 5 von 12 | 0 |
| nerdFBD4 | unverändert, eigenes Netzteil | 4,5 h | 3,1 | 10 von 14 | 0 |

- **Netzteil:** kein Unterschied (3,1 gegen 2,6/h). Gheops PC-USB-Beobachtung trifft hier nicht zu.
- **HTTPS:** 21 von 36 Fehlern fallen in die ~8 % der Zeit um einen Pool-API-Abruf (alle 15 min) → während TLS
  ~15-fach erhöhte Rate. Ohne Abrufe 1,8/h statt ~2,9/h; der Rest ist Grundrauschen, u. a. gehäuft 80–110 s nach dem Start (Ursache offen).
- **SHA-Sperren:** Vorher (23:55–01:33) liefen SHA1-Sperre (B) und SHA1+SHA512-Sperre (C) mit weiter ~3–5 Fehlern/h,
  auch an HTTPS-Abrufen → nicht die Ursache. Trotzdem fest eingebaut (siehe oben), weil die Schleife SHA_TEXT ohne
  Speicherblock-Sperre beschreibt und PR #826 (Hasenpriester, gleiches Board) WLAN-Abbrüche dadurch belegt.
  Laut ESP-IDF 4.4.6 (`sha/parallel_engine/sha.c`): drei Engine-Semaphoren SHA1 / SHA256 / SHA384+512, alle über SHA_TEXT.
- **Modem-Sleep aus:** ohne Effekt, und kein einziger WLAN-Abbruch in ~16 Geräte-Stunden → nicht übernommen.
- **Mechanismus (Vermutung):** Errata CPU-3.16 — TLS nutzt auf dem anderen Kern die AES/RSA-Hardware (0x3FF01000/0x3FF02000)
  direkt neben SHA_TEXT (0x3FF03000). Gheop: MEMW vor START und SW-Miner aus helfen nicht; seine Boards ohne Display
  und ohne HTTPS liegen bei 0,3–1,0/h → Rate folgt der Buslast des anderen Kerns (TLS am stärksten, Display-SPI u. a.).
- **Bewertung:** harmlos. Falsche Kandidaten werden per Software verworfen; der Verlust liegt bei ~0,01 % der Hashes.

### Pausentest (Vorschlag Gheop), 2026-09-28 20:47 bis 29 07:13, je 10,4 h — nicht übernommen

HW-Miner hält von `http.GET()` bis zum Lesen der Antwort an (Schalter `NERD_HW_PAUSE_API`, `e58749f`, wieder entfernt in `cf81a51`).

| Gerät | Pause | Fehler/h | vorher | um HTTPS-Abruf |
|---|---|---|---|---|
| nerd3CF0 | ja | 1,7 | 2,5 | 2 von 18 |
| nerd46BC | ja | 2,0 | 2,4 | 1 von 21 |
| nerd7990 | nein | 1,9 | 3,1 | 7 von 20 |
| nerdFBD4 | nein | 3,5 | 2,9 | 12 von 37 |

- Mit Pause nur noch Zufallsniveau um die Abrufe (3 von 39 ≈ 8 %), ohne 19 von 57 → **TLS als Ursache dieses Anteils bestätigt**.
  Rest ~1,8/h Grundrauschen.
- Kosten: 2,7 s je Abruf, 0,30 % HW-Zeit (`hw_pause_ms` 114 s bzw. 111 s in 10,4 h) → mehr als die harmlosen Prüffehler kosten.

### Fork-Suche (628 Forks, 57 seit März aktiv)

- Außer Gheop keine Hashrate-Arbeit für den klassischen ESP32. Relevanter Fund war PR #826 (übernommen, erweitert).
- Nicht übernommen, ggf. später: `checkError()` per Referenz statt 4-KB-Kopie (`stratum.h:52`); TCP-Keepalive auf den
  Pool-Socket (shaftfx, erkennt tote Verbindungen in ~14 s); PR #725 Auto-Helligkeit über den Lichtsensor der CYD.
  ↪ 2026-09-30: `checkError()` per Referenz (`7d2ee8a`) und TCP-Keepalive (`5c44b2a`) übernommen; Auto-Helligkeit vom User abgelehnt.
- Irrelevant für uns: große mining.notify-Puffer (nur public-pool PPLNS-Port 13333), Board-Ports, Branding.

---

## Offene Punkte

1. **Upstream-PRs #831–833:** weiter ohne Review (Stand 2026-09-29 07:20), konfliktfrei.
2. **#727:** auf Gheops Reaktion zum Pausentest-Kommentar vom 2026-09-29 achten. Nichts mehr zugesagt.
3. **WLAN-Fix beobachten:** seit dem Fix ~25 Updates ohne Ausfall; Ausfall meldet `tools/ota_upload.py` als
   `FEHLER - nach dem Neustart nicht die neue Firmware`, dann `reset_reason` in `/info` prüfen.
4. Optional: `checkError()` per Referenz; Prüffehler-Grundrauschen 80–110 s nach dem Start (Ursache offen, harmlos).
   ↪ `checkError()` ✅ ERLEDIGT (2026-09-30, `7d2ee8a`).
5. Bewusst nicht weiterverfolgt: SW-Hashing in HW-Wartezeiten (User 2026-09-27), Modem-Sleep aus, HW-Pause während HTTPS.
   Kleinkram: Versionsanzeige (`git describe` ohne `--tags`), `README.md:198`.
   ↪ 2026-09-30: bleibt bewusst so (Bildschirm zeigt `CURRENT_VERSION`, siehe Handover 2026-09-30).

---

## Konventionen (Memory-relevant)

- Miner-Entscheidungen (Tests, Fixes, OTA) trifft Claude selbst (User 2026-09-28); nachfragen nur bei Physischem/Destruktivem.
- Allgemeines, projektübergreifendes Wissen in die globale `~/.claude/CLAUDE.md` (User 2026-09-28).
- Boards streuen bei Prüffehlern stark (Gheop: bis 15-fach) → Prüffehler je Gerät gegen die eigene Vorher-Rate vergleichen;
  bei ~3/h braucht ein Vergleich mehrere Stunden.
- Varianten per `PLATFORMIO_BUILD_FLAGS` + eigenem `PLATFORMIO_BUILD_DIR`; Sperren/Aufrufe im Binary per
  `xtensa-esp32-elf-objdump` prüfen (`callx8` in `minerWorkerHw` zählen).

---

## Aktionen in der neuen Session — Empfehlung

1. `gh pr view 727|831|832|833 -R BitMaker-hub/NerdMiner_v2` auf Antworten/Reviews prüfen.
2. Kurz `tools/ota_upload.py --list`: alle vier auf `-364-gcf81a51`, Laufzeit ohne Neustart?
