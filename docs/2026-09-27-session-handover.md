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
- **Nachtest erledigt (2026-09-27 abends), nicht übernommen.** Padding nach 48 nop als Flag, nerd3CF0 + nerd46BC
  gegen nerd7990 + nerdFBD4, erst 10 min Baseline aller vier, dann 20 min A/B. HW-Anteil:

  | Gerät | Rolle | vorher | nachher | Δ |
  |---|---|---|---|---|
  | nerd3CF0 | Padding 48 nop | 752,5 | 756,7 | +0,56 % |
  | nerd46BC | Padding 48 nop | 753,8 | 754,6 | +0,11 % |
  | nerd7990 | unverändert | 754,2 | 755,2 | +0,13 % |
  | nerdFBD4 | unverändert | 753,3 | 754,5 | +0,16 % |

  Um die Kontrolldrift bereinigt +0,4 % bzw. ±0, Prüffehler unauffällig. Passt zum Engine-Limit (318 Takte/Nonce).
  Code zurückgenommen, alle vier wieder auf `-343-gf337153` (MD5 `51d5b0fc…`).

### 2. Gheops Frage: Stromversorgung der CYDs — Umstecktest läuft

- Bei ihm sank die Prüffehlerrate eines Boards von 2,4/h (USB am PC) auf 0 (Netzteil).
- **Bei uns:** alle vier an einem Netzteil über ein 1-auf-4-USB-C-Kabel (am Notebook nur beim ersten USB-Flashen).
  Die ~3/h wurden also am Netzteil gemessen (nerd7990 58, nerdFBD4 66 Fehler in 21,5 h).
- **Test seit 2026-09-27 23:10:** nerdFBD4 an eigenem Netzteil (Neustart = Zählerbeginn), die anderen drei weiter am
  Verteiler. Startwerte aus `/info` um 23:10:

  | Gerät | uptime_s | hw_checked | hw_errors |
  |---|---|---|---|
  | nerd3CF0 | 1242 | 14004 | 3 |
  | nerd46BC | 1217 | 14103 | 1 |
  | nerd7990 | 78709 | 906785 | 58 |
  | nerdFBD4 | 40 | 359 | 0 |

- Auswertung frühestens nach 4 h, besser am nächsten Morgen: Fehler je Stunde und je `hw_checked` seit 23:10 vergleichen.
  Nebeneffekt beachten: der Verteiler ist jetzt mit drei statt vier Geräten belastet.
- **Danach posten (User-Freigabe, erst wenn beides vorliegt):** eine Antwort auf #727 mit Padding-Ergebnis und
  Stromversorgung/Umstecktest. Entwurf:

  > @Gheop I retested the block 2 padding step with 48 nops on two boards against two unchanged ones, 20 min:
  > +0.4% on one, nothing on the other (after the drift of the unchanged boards). My earlier try with 32 nops on one
  > board had given +0.15%.
  >
  > About power: the four boards run from one mains USB adapter through a 1-to-4 USB-C cable, not from a PC. The
  > ~3 disagreements per hour per board were measured on that adapter. **[Ergebnis Umstecktest]**

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

1. Umstecktest auswerten (Punkt 2): `/info` aller vier holen, Raten seit 23:10 gegen die Startwerte rechnen.
2. Entwurf ergänzen und auf #727 posten (englisch, knapp, nur Messwerte).
3. `gh pr view 831|832|833 -R BitMaker-hub/NerdMiner_v2` auf Reviews prüfen.
