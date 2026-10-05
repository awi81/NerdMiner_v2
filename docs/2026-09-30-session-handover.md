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
- Hermes hat die .164 fest eingestellt (netplan, `dhcp4: false`, geprüft 01.10.), der .164-Eintrag in der Fritz!Box ist alt.
- Behebung: in der Fritz!Box die .116 für das Gerät „awi“ reservieren. Danach gilt die .164 dort als frei, deshalb ein
  Platzhalter-Gerät mit erfundener MAC und fester .164 anlegen.
- **Behoben 01.10. nachts:** .116 in der Fritz!Box fest für den PC reserviert, Handy-Eintrag gelöscht. Einen Platzhalter
  für die .164 lehnt die Fritz!Box ab, weil Hermes sie aktiv nutzt; solange Hermes läuft, vergibt sie sie nicht.
- Pool-Log bis 02:21 ohne „NICHT ERREICHBAR“ (letzter Eintrag 21:41 = WLAN-Ausfall des PCs, auch die Fritz!Box war weg).
  Alle vier Miner ohne Pool-Neuverbindung und WLAN-Abbruch. Pool-Logger beendet.

### 2. Git-Historie mit MACs/BSSID

- Erledigt 30.09. (siehe oben). Sicherung des alten Stands nur lokal:
  `D:\workspace_temp\NerdMiner_v2\backup\awiEdition-vor-bereinigung-2026-09-30.bundle`.

### Nachtrag 03.10. (PC aus bis 04.10. ~09:00)

- Pool-Logger lief 02.10. 19:24 – 03.10. 19:59, vor dem Herunterfahren beendet (Neustart: `python poollog.py poollog.txt`
  in `D:\workspace_temp\NerdMiner_v2\poollog`).
- 02.10. 22:12: alle vier gleichzeitig neu mit dem Pool verbunden (Lücke je 25 s), Internet lief durch → vermutlich Pool-seitig.
  Seit 01.10. 01:17 bis zu 5 weitere Neuverbindungen je Miner, Zeitpunkte davor unbekannt.
- 03.10. 05:48–08:46: Miner vom PC aus nicht erreichbar (07:38–08:46 alle, zeitweise auch die Fritz!Box), Miner selbst ohne
  Störung, kein Tcpip-4199, keine Systemereignisse. Ursache offen (PC-Seite).
- 03.10. 15:40: nerdFBD4 nach Umstecken an einem Repeater (-78 dBm) statt an der Fritz!Box. Per OTA (gleiche Firmware,
  `--force`) neu gestartet → wieder Fritz!Box, -44 dBm. Warum die Suche nach dem stärksten Zugangspunkt beim Einschalten
  den Repeater wählte, ist offen; tritt es wieder auf, `src/wManager.cpp` prüfen.

### Nachtrag 04.10. (PC aus bis Mo 05.10. ~09:00)

- Pool-Logger lief 04.10. 09:27 – 20:03, vor dem Herunterfahren beendet.
- 04.10. 19:08: alle vier gleichzeitig neu mit dem Pool verbunden. Ab ~19:14 WLAN-Störung: Die Fritz!Box (und ein
  Repeater) wechselten von **Kanal 6 auf Kanal 1**. Viele WLAN-Abbrüche (`wifi_disc` bis 58, Gründe 200/201/202/8/49),
  vom PC aus zeitweise auch die Fritz!Box nicht erreichbar. Danach hängen **nerd3CF0 (-62 dBm) und nerd7990 (-59 dBm) am
  Repeater**, nerd46BC/nerdFBD4 an der Fritz!Box. Bis 20:02 noch kurze Aussetzer alle paar Minuten.
- **Nach dem Neustart:**
  1. Logger neu starten (`python poollog.py poollog.txt` in `D:\workspace_temp\NerdMiner_v2\poollog`).
  2. `/info` aller vier prüfen: Kanal, Zugangspunkt, `wifi_disc`. Hängen nerd3CF0/nerd7990 noch am Repeater, per OTA
     neu starten (`tools/ota_upload.py --force <IP>`, gleiche Firmware, MD5 `46f15d29…`).
  3. Klären, warum die Fritz!Box den Kanal gewechselt hat (Auto-Kanal? Störung?), ggf. User fragen.
  4. Offen: Die Firmware bleibt nach einem WLAN-Abbruch am erstbesten Zugangspunkt. Wiederholt sich das, in
     `src/wManager.cpp` beim Wiederverbinden ebenfalls nach Signal wählen.
- Keine geplanten Windows-Aufgaben für dieses Projekt.

### Nachtrag 06.10. nachts (ohne User, Entscheidungen selbst getroffen)

- Stand 00:22: WLAN weiter auf Kanal 1; nerd3CF0/nerd7990 seit dem 04.10. am Repeater (-60 dBm), Mining normal.
  Seit 04.10. 19:25 nur nerd3CF0 dreimal neu mit dem Pool verbunden. Pool-Logger wieder gestartet (PID 44936).
- **Entscheidung: WLAN-Roaming eingebaut** (`426f0e9`, `src/wManager.cpp`). Begründung: Nach einem WLAN-Abbruch
  bleibt der ESP32 am Zugangspunkt, den er beim Wiederverbinden gefunden hat, auch wenn die Fritz!Box danach wieder
  stärker ist. Jetzt: alle 10 min, nur bei Signal < -55 dBm, Suche im Hintergrund nach derselben SSID; ist ein anderer
  Zugangspunkt ≥ 10 dB stärker, `WiFi.reconnect()` (wählt dann selbst nach Signal). Zähler `wifi_roam` in `/info`.
  Schwellen per Build-Flag (`ROAM_CHECK_ms`, `ROAM_RSSI_MAX`, `ROAM_MIN_GAIN_dB`) änderbar.
- Test auf nerd3CF0 mit Testvariante (jede Minute, immer wechseln): 6 Wechsel in 6 min, kein Absturz, Mining lief weiter;
  je Wechsel kurze Unterbrechung + neue Pool-Verbindung (einmal ~80 s nicht erreichbar).
- Verteilt 00:40 auf alle vier (`-379-g426f0e9`, MD5 `4750fbd1…`): alle an der Fritz!Box, -42…-51 dBm, Selbsttest ok.
- Nicht getestet: echter Wechsel Repeater → Fritz!Box durch die Prüfung (nach dem Neustart wählten alle schon richtig).
  Risiko: Sieht der Verbindungsaufbau den stärkeren Zugangspunkt nicht, verbindet der Miner alle 10 min neu.
- **Offen für den User:** Warum hat die Fritz!Box am 04.10. ~19:14 von Kanal 6 auf 1 gewechselt? WLAN-Einstellungen per
  TR-064 nur mit Login (HTTP 401). Vermutlich Autokanal; in der Fritz!Box unter WLAN → Funkkanal prüfen.

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
