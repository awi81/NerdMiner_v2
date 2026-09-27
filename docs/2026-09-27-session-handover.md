# Session-Handover 2026-09-27

Stichworte: Abschluss der Hashrate-Session (467 → 792 KH/s je CYD), Gheops Antwort auf #727
mit zwei offenen Fragen. Messdetails in `2026-09-26-session-handover-abend.md`. Diese Datei dient
als Übergabe an die nächste Session.

---

## Ausgangslage

- Branch `awiEdition` auf `e54495b`, gepusht nach `origin`. Upstream `upstream/main` unverändert auf `e3a04b7`.
- Alle vier CYDs auf `a193daa` (Version `-1.6.3-343-gf337153`). Stand 2026-09-27 20:50:
  seit ~19,5 h ohne Unterbrechung (`reset_reason` 1 = Einschalten, alle vier gleichzeitig um ~01:20;
  nicht durch ein Update), Selbsttest ok, Prüffehler 53–70 bei je ~810.000 Treffern (~3 pro Stunde je Gerät).

---

## Erledigt in dieser Session

| Commit / Link | Bereich | Was |
|---|---|---|
| `a193daa` | `src/mining.cpp`, `src/otaUpdate.cpp`, `src/wManager.cpp`, `tools/hashrate.py` | Assembler-Schleife (Grundlage Gheop), im Flash, Block-1-Hälfte während Block 3 (48 nop), 64K-HW-Jobs, Selbsttest, `/info` `hw_kat`/`hw_idle`/`hw_bench`; WLAN-Start ohne Hotspot-Hänger |
| `f337153`, `e54495b` | `docs/` | Handover Abend inkl. Messtabelle und verworfener Varianten |
| [#727 Kommentar](https://github.com/BitMaker-hub/NerdMiner_v2/pull/727#issuecomment-5849968237) | Upstream | CYD-Messdaten; früheren DPORT-Kommentar mit Update-Hinweis korrigiert |
| Geräte | alle 4 | Abschlussmessung 30 min: 790–792 je Gerät, Summe 3164 KH/s |

---

## Offene Punkte für die nächste Session

### 1. Gheops Padding-Schritt: bei ihm +2,2 %, bei uns +0,15 %

- Gheop (#727, 2026-09-27 16:04, Commit `84a54f7` in `Gheop/NerdMiner_v2` all-fixes): TEXT[8]/TEXT[15] für Block 3
  während Block 2 schreiben, nach 48 nop → klassischer ESP32 793,5 → 811,4 KH/s.
- Das ist praktisch unsere verworfene Variante `NERD_ASM_PADEARLY` (Runde 4: 32 nop, 793,4 gegen 792,2).
  Unterschied zu Gheop: 32 statt 48 nop, nur ein Gerät, Prefill 32 statt 48.
- Nachtest: Padding nach 48 nop (Code aus `84a54f7`, `nerd_sha_nonce_run_asm` in `src/mining.cpp`) auf 2 Geräten
  gegen 2 unveränderte, 20–30 min. Nur übernehmen, wenn der Gewinn auf beiden Geräten sichtbar ist.

### 2. Gheops Frage: Stromversorgung der CYDs

- Bei ihm sank die Prüffehlerrate eines Boards von 2,4/h (USB am PC) auf 0 (Netzteil). Unsere Rate: ~3/h je Gerät.
- Offen: Wie werden die vier CYDs im Rahmen versorgt (Netzteil, USB-Hub, PC)? Antwort des Users einholen,
  dann kurz auf #727 antworten.

### 3. Upstream-PRs #831–833

- Weiter ohne Review, konfliktfrei. Gheop schreibt, dass offene PRs in `BitMaker-hub` seit Juli keine Antwort bekommen.

### 4. WLAN-Fix beobachten

- Seit dem Fix 15 Updates ohne Ausfall. `tools/ota_upload.py` meldet einen Ausfall als
  `FEHLER - nach dem Neustart nicht die neue Firmware`; dann `reset_reason` in `/info` prüfen.

### 5. Bewusst nicht weiterverfolgt

- SW-Hashing in die Wartezeiten des HW-Kerns verschränken (~+3 %): vom User am 2026-09-27 verworfen.
- Kleinkram unverändert: Versionsanzeige (`git describe` ohne `--tags`), `README.md:198`.

---

## Wichtige Konventionen aus dieser Session (Memory-relevant)

- Commit und Push ohne Rückfrage erlaubt. **Why:** ausdrückliche Freigabe des Users. **How to apply:** fertige, geprüfte Stände committen und `git push origin awiEdition`.
- PR-Antworten werden zu Beginn der nächsten Session geprüft, nicht per Zeitplan. **Why:** Entscheidung des Users 2026-09-27.
- Varianten per `PLATFORMIO_BUILD_FLAGS` + eigenem `PLATFORMIO_BUILD_DIR`; ein Gerät je Variante reicht (Geräte streuen < 0,3 %).
- Python-Skripte nicht per Bash-Heredoc (`\\` wird zu `\`), sondern per Write-Tool in den Scratchpad.

---

## Aktionen in der neuen Session — Empfehlung

1. User nach der Stromversorgung der CYDs fragen (Punkt 2).
2. Padding-Nachtest nach Gheops `84a54f7` (Punkt 1): `git fetch https://github.com/Gheop/NerdMiner_v2.git all-fixes`,
   Variante bauen, auf 2 Geräte per `tools/ota_upload.py --firmware <bin> <IP>`, messen mit `tools/hashrate.py --seconds 1200`.
3. Kurze Antwort auf #727 mit Ergebnis und Stromversorgung (englisch, knapp, nur Messwerte).
4. `gh pr view 831|832|833 -R BitMaker-hub/NerdMiner_v2` auf Reviews prüfen.
