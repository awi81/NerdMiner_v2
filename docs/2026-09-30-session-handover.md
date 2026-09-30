# Session-Handover 2026-09-30

Stichworte: Session 2026-09-29 abends bis 2026-09-30. `checkError()` per Referenz, TCP-Keepalive auf den Pool-Socket,
WLAN wählt den stärksten Zugangspunkt (Repeater im Netz), `/info` um `pool_conn`/`pool_gap_s`/`bssid`/`chan` erweitert,
IP-Konflikt des PCs als Ursache der Erreichbarkeits-Aussetzer gefunden, Solo-Reward geklärt. Der Verlauf im Detail steht in
`2026-09-29-session-handover.md` (während der Session fortgeschrieben). Diese Datei dient als Übergabe an die nächste Session.

---

## Ausgangslage

- Branch `awiEdition`, gepusht nach `origin`. `upstream/main` unverändert `e3a04b7`.
- Alle vier CYDs auf `31c4e26` (`-1.6.3-372-g31c4e26`): nerd7990/nerdFBD4 seit 2026-09-30 05:05, nerd3CF0/nerd46BC seit 06:28.
  Stand 21:00: keine Pool-Neuverbindung (`pool_conn` 1), kein WLAN-Abbruch, alle direkt an der Fritz!Box (Kanal 6) mit
  -46…-52 dBm. 5-min-Kontrolle 05:13: 789–791 KH/s je Gerät, Summe 3160,1 KH/s, Selbsttest ok.
- Pool-Logger läuft (PID 50960, losgelöst): `D:\workspace_temp\NerdMiner_v2\poollog\poollog.py` → `poollog.txt`
  (Pool-Neuverbindungen, WLAN-Abbrüche, Zugangspunkt-Wechsel, Neustarts, Nichterreichbarkeit, stündlicher Stand).
  Beenden: `Stop-Process -Id 50960`.

---

## Erledigt in dieser Session

| Commit | Bereich | Was |
|---|---|---|
| `7d2ee8a` | `src/stratum.cpp`, `stratum.h` | `checkError()` nimmt das JSON-Dokument per Referenz statt als 4-KB-Kopie auf dem Stack |
| `5c44b2a` | `src/mining.cpp`, `otaUpdate.cpp` | TCP-Keepalive auf den Pool-Socket (10 s ruhig, dann alle 5 s, 3 Versuche → tote Verbindung nach ≤ 25 s erkannt); `/info`: `pool_conn`, `pool_gap_s`. Werte per Test-Build am Socket geprüft; Nachtlauf 4,7 h (zwei Geräte bei -80…-84 dBm) ohne Fehlalarm |
| `1e7d0a0` | `src/wManager.cpp` | WLAN: Scan aller Kanäle, Wahl nach Signal (vorher erster gefundener Zugangspunkt). Zwei Repeater senden ebenfalls „awi2“; zwei Miner waren nach einem Neustart bei -80…-84 dBm gelandet, jetzt alle an der Fritz!Box mit -41…-54 dBm |
| `31c4e26` | `src/otaUpdate.cpp` | `/info`: `bssid`, `chan` |
| – | Diagnose | Aussetzer vom PC aus (alle ~10 min, bei Minern bis ~5 min) = **IP-Konflikt**: Der PC hat die feste IP 192.168.178.116, die Fritz!Box vergibt sie zusätzlich per DHCP an ein Gerät mit zufälliger MAC. Belegt per ARP-Diagnose-Build auf nerd3CF0 (ARP-Eintrag für .116 wechselt zwischen PC und Fremdgerät) und Windows-Ereignis Tcpip 4199. Mining nicht betroffen |
| – | Test | Modem-Sleep aus (A/B, 06:01–06:28): Ping 6–7 statt 113 ms, beseitigt die Aussetzer nicht → zurückgenommen |
| – | Entscheidungen | Auto-Helligkeit (PR #725): User will sie nicht. Versionsanzeige/`README.md:198` bleiben (Bildschirm zeigt `CURRENT_VERSION`) |
| – | Frage des Users | Reward: public-pool.io ist solo, ein gefundener Block geht voll an die eigene Adresse (laut Quellcode 1,5 % Dev-Fee erst ab 50 TH/s je Gerät). Keine laufenden Einnahmen; 4 Bitaxe finden im Mittel alle ~3.800 Jahre einen Block (967 EH/s Netz) |
| – | Aufräumen | Testbuild-Ausgaben nach `D:\workspace_temp\NerdMiner_v2\firmware`, Diagnose-Patch nach `…\diag\diag_patch.py`, Ping-/PC-WLAN-Logger beendet |
| – | Vertraulichkeit | Vollständige MACs/BSSID aus `2026-09-29-session-handover.md` entfernt (öffentliches Repo). Historie am 30.09. bereinigt: alle Doku-Commits nach `31c4e26` zu einem zusammengefasst, Force-Push. Die alten Commits sind auf GitHub per SHA noch abrufbar, ganz weg nur über den GitHub-Support |

---

## Offene Punkte für die nächste Session

### 1. IP-Konflikt des PCs (Entscheidung beim User)

- Ursache geklärt (30.09. abends, per TR-064 der Fritz!Box, geht ohne Login): Die VM Hermes (.164) läuft auf dem PC und
  geht über die WLAN-Brücke mit der MAC des PCs ins Netz. Die Fritz!Box führt die PC-MAC deshalb mit der .164 (DHCP) und
  hält die .116 für frei. Sie vergibt sie an Geräte mit zufälliger MAC: zuletzt ein Android-Handy, im August ein Tablet.
- Eine Reservierung der .116 auf die PC-MAC hilft daher nicht: Hermes bekäme dann per DHCP die .116.
- Empfehlung: in der Fritz!Box ein Gerät von Hand anlegen (Heimnetz → Netzwerk → Gerät hinzufügen) mit einer erfundenen
  MAC und fester IP .116. Dann vergibt die Fritz!Box die .116 nicht mehr. Alternative: PC-IP außerhalb des DHCP-Bereichs,
  dann aber Verweise auf .116 in moneyBot/Hermes anpassen.
- Letzte Aussetzer 07:08, seitdem keine (Handy nicht im Netz).
- Nach der Behebung: `poollog.txt` über ≥ 30 min ohne „NICHT ERREICHBAR“, dann Pool-Logger beenden.

### 2. Git-Historie mit MACs/BSSID

- Erledigt 30.09. (siehe oben). Sicherung des alten Stands nur lokal:
  `D:\workspace_temp\NerdMiner_v2\backup\awiEdition-vor-bereinigung-2026-09-30.bundle`.

### 3. Upstream beobachten

- #727 (Gheops Reaktion auf den Pausentest-Kommentar), #831–833: Stand 2026-09-30 21:10 keine Reaktion, konfliktfrei.

---

## Wichtige Konventionen aus dieser Session (Memory-relevant)

- Keine vollständigen MACs/BSSIDs in Repo-Dateien. **Why:** Fork ist öffentlich; BSSID erlaubt Standortbestimmung.
  **How to apply:** Platzhalter (`<MAC PC>`) in `docs/`, echte Werte nur lokal (globale `CLAUDE.md`, Memory).
- Testbuilds tragen keine „dirty“-Kennung (`git describe` ohne `--dirty`). **Why:** Versionsstring gleicht dem Commit.
  **How to apply:** Testbuild-Geräte über die MD5 in `/info` erkennen.
- Unerreichbarkeit vom PC bei unverändertem `pool_conn`/`wifi_disc` → Netz des PCs (IP-Konflikt), nicht die Miner.

---

## Aktionen in der neuen Session — Empfehlung

1. User fragen, ob der IP-Konflikt behoben ist und ob die Git-Historie bereinigt werden soll.
2. `~/.platformio/penv/Scripts/python.exe tools/ota_upload.py --list` (in `D:\workspace\NerdMiner_v2`): alle vier auf `-372-g31c4e26`?
3. `D:\workspace_temp\NerdMiner_v2\poollog\poollog.txt` auf `POOL`/`WLAN`/`NEUSTART`/`NICHT ERREICHBAR` prüfen, danach Logger beenden.
4. `gh pr view 727 831 832 833 -R BitMaker-hub/NerdMiner_v2` auf Reaktionen prüfen.
