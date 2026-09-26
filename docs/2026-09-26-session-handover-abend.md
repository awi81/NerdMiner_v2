# Session-Handover 2026-09-26 (Abend)

Stichworte: Hashrate 467 → 792 KH/s je CYD (Summe ~3170), HW-SHA am Limit der Engine,
Ursache der 4-Minuten-Ausfälle nach Updates behoben. Fortsetzung von `2026-09-26-session-handover.md`.

---

## Erledigt

| Commit | Was |
|---|---|
| `a193daa` | Nonce-Schleife des klassischen ESP32 in Assembler (Grundlage Gheop, PR #727 / all-fixes `4d00b0b`), im Flash statt IRAM, obere Hälfte von Block 1 während Block 3 (nach 48 nop), 64K Nonces je HW-Job, Selbsttest Block 125552 beim Start, `/info`: `hw_kat`, `hw_idle`, `hw_bench`; WLAN-Start: gespeichertes WLAN bis zu 6 × 15 s selbst versuchen, bevor der Einrichtungs-Hotspot startet |
| Geräte | alle 4 auf `a193daa` (MD5 `28e1c8e9…`), per WLAN; Abschlussmessung 30 min: 790–792 je Gerät, Summe 3164 KH/s, 5 Prüffehler bei 83.000 Treffern |

## Messungen (A/B per WLAN, Zählerdifferenzen, `tools/hashrate.py`)

| Schritt | je Gerät | Bemerkung |
|---|---|---|
| Ausgangslage | 467 | C-Schleife mit APB-Vorlesen |
| Gheops Assembler-Schleife | 696–699 | rohe Reads gehen, früheres „100 % falsch“ war Reihenfolge |
| + HW-Miner auf Kern 0 | 673 | −3,3 %, verworfen |
| + Schleife im Flash statt IRAM | 747 | +7 %, auch SW 29 → 38 (IRAM-Konkurrenz der Kerne) |
| + Block-1-Hälfte während Block 3 | 780 | +4,5 %; ≤24 nop ~10 % falsche Hashes, ab 32 nop keine |
| + 64K-Jobs | 792 | HW-Leerlauf 1,7 → 0,2 % |
| Padding vorab, 40/48 nop | 791–793 | kein Effekt mehr → Engine-Limit |
| SW-SHA -O2/-O3 | 784 | SW 38 → 30,5, verworfen |
| SW-SHA zusätzlich im Flash | 738 | −1 %, verworfen |
| Warten per Taktzähler statt BUSY-Poll | – | Block/LOAD 58/10 Takte korrekt (338 statt 347 Takte je Nonce), 52/8 alle Hashes falsch → ≤2,6 % ohne Marge, verworfen |

Taktdiagnose (`-DNERD_ASM_BENCH`): Block 86, LOAD 30 Takte inkl. Poll → 3 × 86 + 2 × 30 = 318 Takte
je Nonce = 754 KH/s HW bei 240 MHz, genau der gemessene Wert.

Prüffehler: ~1 von 15.000 Kandidaten (~2,7/h je Gerät), werden verworfen; Kandidatenzahl entspricht
der Erwartung (1 von 65.536 Hashes), es gehen also keine Treffer verloren.

## Offene Punkte

1. **`awiEdition`** ist gepusht. Commit und Push darf ich laut User ohne Rückfrage.
2. **PR #727**: Messdaten gepostet (issuecomment-5849968237), früheren DPORT-Kommentar korrigiert; Antworten von Gheop beobachten.
3. **PRs #831–833**: noch ohne Review, konfliktfrei.
4. **WLAN-Fix beobachten**: Seit dem Fix ~16 Updates ohne Ausfall (vorher 2 Ausfälle). Weiter prüfen, ob noch ein Gerät 4 min fehlt (`reset_reason` 3 mit kurzer Laufzeit).
   Ursache war: WiFiManager 2.0.17 beendet den blockierenden Hotspot nicht, wenn sich das Gerät im Hintergrund verbindet.
5. **Resthebel**: Der HW-Kern wartet ~75 % der Zeit auf die Engine; SW-Hashing in diese Wartezeit zu verschränken
   brächte grob +3 %, ist aber aufwendig (Assembler, Registerdruck, Risiko für die HW-Taktung).
6. Unverändert offen: Versionsanzeige `-1.6.3-…` (`git describe` ohne `--tags`), `README.md:198` („huge app“).
