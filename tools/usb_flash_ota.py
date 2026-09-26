#!/usr/bin/env python3
"""Einmaliger USB-Schritt: stellt NerdMiner (CYD) auf die OTA-Partitionstabelle um und spielt die
Firmware mit Update per WLAN auf. Danach gehen Updates mit tools/ota_upload.py ohne Kabel.

Geschrieben werden nur App (0x10000), otadata (0xE000) und Partitionstabelle (0x8000). NVS (WLAN,
Statistik) und SPIFFS (Pool/Wallet) bleiben unberührt. Die Boards können GPIO0 nicht per USB
steuern, daher muss zum Flashen einmal die BOOT-Taste gehalten werden. Läuft die neue Firmware schon,
wird nicht geflasht (keine BOOT-Taste nötig).

Ablauf je Gerät: Firmware-Stand prüfen -> ggf. flashen -> Boot-Log prüfen (Bilder, Konfiguration,
Update-Server) -> ggf. ins WLAN --wifi-ssid wechseln (per USB-Befehl, ohne BOOT) -> vom PC aus per
WLAN erreichbar? -> Test-Update per WLAN (gleiche Firmware in den anderen Slot).

Aufruf (aus dem Projektverzeichnis, nach dem Build, im eigenen Terminal wegen Passwortabfrage):
  python tools/usb_flash_ota.py --all --wifi-ssid awi2  # alle Geräte nacheinander am selben Kabel
  python tools/usb_flash_ota.py COM7 --wifi-ssid awi2   # nur das Gerät an COM7
  python tools/usb_flash_ota.py COM7 --check-only       # nicht flashen, nur neu starten und prüfen
WLAN-Passwort aus $NERDMINER_WIFI_PASS oder verdeckte Abfrage (einmal für alle Geräte).
"""
import argparse
import getpass
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

import serial
from serial.tools import list_ports

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ota_upload  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / ".pio/build/ESP32_2432S028_2USB"
FIRMWARE = BUILD / "firmware.bin"
PIO = Path.home() / ".platformio"
ESPTOOL = PIO / "packages/tool-esptoolpy/esptool.py"
BOOT_APP0 = PIO / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

INTERESTING = re.compile(r"\[IMG\]|\[OTA\]|\[USB\]|IP address|PoolString|btcString|Authorization|"
                         r"Guru|Backtrace|panic|abort|rst:|Entered Configuration Mode|Sprite Error", re.I)
NEW_FW_MARK = re.compile(r"\[OTA\]|\[IMG\]|\[USB\]")


def ch340_ports():
    return sorted(p.device for p in list_ports.comports() if "1A86" in p.hwid.upper())


def open_port(port):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False  # beim Öffnen keinen Reset auslösen
    s.rts = False
    s.open()
    return s


def reset(s):
    s.rts = True   # EN low -> Neustart (BOOT muss losgelassen sein)
    time.sleep(0.2)
    s.rts = False


def read_lines(s, seconds, echo=True, until=None):
    lines, buf, end = [], b"", time.time() + seconds
    while time.time() < end:
        buf += s.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            lines.append(line)
            if echo and INTERESTING.search(line):
                print("  " + line.strip()[:170], flush=True)
            if "waiting for download" in line:
                print("  -> Gerät hängt im Download-Modus: BOOT loslassen, ich starte neu ...", flush=True)
                reset(s)
            if until and until(line):
                return lines
    return lines


def has_new_firmware(port):
    """Läuft schon die OTA-Firmware? Erkennt sie an NM-INFO-Antwort oder an ihren Boot-Meldungen."""
    s = open_port(port)
    try:
        for _ in range(4):
            s.write(b"NM-INFO\n")
            if any(NEW_FW_MARK.search(l) for l in read_lines(s, 2.5, echo=False, until=lambda l: "[USB]" in l)):
                return True
        return False
    finally:
        s.close()


def flash(port):
    files = [(0x10000, FIRMWARE), (0xE000, BOOT_APP0), (0x8000, BUILD / "partitions.bin")]
    for _, f in files:
        if not f.exists():
            sys.exit(f"Fehlt: {f} (vorher bauen: python -m platformio run -e ESP32_2432S028_2USB)")
    print("\n>>> BOOT-Taste jetzt gedrückt halten. Sobald 'Chip is ESP32' erscheint, loslassen. <<<\n", flush=True)
    # App zuerst: bricht es danach ab, startet die neue (kleinere) App auch noch mit der alten Tabelle
    cmd = [sys.executable, str(ESPTOOL), "--chip", "esp32", "--port", port, "--baud", "921600",
           "--before", "default_reset", "--after", "no_reset", "--connect-attempts", "0", "write_flash"]
    for addr, f in files:
        cmd += [hex(addr), str(f)]
    if subprocess.call(cmd) != 0:
        return False
    print("\n>>> Fertig geschrieben. BOOT loslassen - Neustart in 3 s <<<", flush=True)
    time.sleep(3)
    return True


def boot_log(port, seconds, command=None):
    """Neustart (per Reset oder per USB-Befehl, nach dem die Firmware selbst neu startet) und Log lesen."""
    s = open_port(port)
    try:
        if command:
            s.write((command + "\n").encode("utf-8"))
        else:
            reset(s)
        # bis der Update-Server meldet (plus etwas Hashrate), höchstens `seconds`
        seen = {"ota": None}

        def until(line):
            if "Passwort falsch" in line:  # WLAN-Test der Firmware gescheitert, kein Neustart
                return True
            if "[OTA] Update per WLAN bereit" in line:
                seen["ota"] = time.time()
            return seen["ota"] is not None and time.time() - seen["ota"] > 12
        return "\n".join(read_lines(s, seconds, until=until))
    finally:
        s.close()


def analyze(text):
    m = re.search(r"\[OTA\] Update per WLAN bereit: http://(\S+?)\.local/update bzw\. http://([0-9.]+)/update", text)
    rates = [float(r) for r in re.findall(r"avg\. hashrate ([0-9.]+) KH/s", text)]
    res = {
        "img_ok": "[IMG] Selbsttest: 8/8" in text,
        "crashed": bool(re.search(r"Guru Meditation|Backtrace|abort\(\)", text)),
        "config_missing": "Entered Configuration Mode" in text,
        "host": m.group(1) if m else None,
        "ip": m.group(2) if m else None,
        "rate": sum(rates[len(rates) // 2:]) / len(rates[len(rates) // 2:]) if rates else None,
    }
    print(f"  -> Bilder {'OK' if res['img_ok'] else 'FEHLER'}, Absturz {'JA' if res['crashed'] else 'nein'}, "
          f"{'EINRICHTUNGS-HOTSPOT (WLAN-Verbindung fehlgeschlagen oder Konfiguration fehlt)' if res['config_missing'] else 'Konfiguration ok'}, "
          f"Update-Server {res['host'] + ' ' + res['ip'] if res['host'] else 'nicht gestartet'}"
          + (f", {res['rate']:.0f} KH/s" if res["rate"] else ""), flush=True)
    return res


def reachable(ip):
    try:
        return ota_upload.get_info(ip, 5)
    except Exception:
        return None


def ota_test(ip, info):
    """Echtes Update per WLAN: gleiche Firmware in den anderen Slot, danach Slot und MD5 prüfen."""
    password = ota_upload.load_password(None)
    data = FIRMWARE.read_bytes()
    md5 = hashlib.md5(data).hexdigest()
    print(f"  Test-Update per WLAN nach {ip} (läuft aus {info['partition']}) ...", flush=True)
    status, text = ota_upload.upload(ip, data, password)
    if status != 200 or "Success" not in text:
        print(f"  -> Upload fehlgeschlagen (HTTP {status}): {text.strip()[:150]}")
        return False
    end = time.time() + 150
    while time.time() < end:
        time.sleep(3)
        new = reachable(ip)
        if new and new.get("partition") != info["partition"]:
            ok = new.get("md5") == md5
            print(f"  -> {'OK' if ok else 'FEHLER'}: läuft jetzt aus {new['partition']}, MD5 {'passt' if ok else 'weicht ab'}")
            return ok
        if new and new.get("uptime_s", 1 << 30) < info.get("uptime_s", 0) and new.get("partition") == info["partition"]:
            print("  -> FEHLER: neu gestartet, aber im alten Slot (Rückfall?)")
            return False
    print("  -> FEHLER: nach dem Update nicht wieder erreichbar")
    return False


def process(port, args, wifi_password):
    print(f"\n=== Gerät an {port} ===", flush=True)
    if not args.check_only:
        if has_new_firmware(port):
            print("  Neue Firmware läuft bereits, Flashen übersprungen")
        elif not flash(port):
            print("  -> esptool fehlgeschlagen")
            return False
    print(f"  Neustart, Boot-Log (max. {args.seconds} s):", flush=True)
    res = analyze(boot_log(port, args.seconds))
    if not res["img_ok"] or res["crashed"] or res["config_missing"] or not res["ip"]:
        return False

    info = reachable(res["ip"])
    if not info and args.wifi_ssid:
        print(f"  Vom PC nicht erreichbar -> wechsle ins WLAN '{args.wifi_ssid}':", flush=True)
        command = "NM-WIFI " + json.dumps({"ssid": args.wifi_ssid, "pass": wifi_password})
        text = boot_log(port, args.seconds, command)
        if "Passwort falsch" in text:
            print(f"  -> WLAN '{args.wifi_ssid}' nicht erreichbar oder Passwort falsch; Gerät bleibt im bisherigen WLAN")
            return False
        res = analyze(text)
        if not res["ip"]:
            return False
        info = reachable(res["ip"])
    if not info:
        print(f"  -> {res['host']} ({res['ip']}) ist vom PC aus per WLAN nicht erreichbar")
        return False
    md5 = hashlib.md5(FIRMWARE.read_bytes()).hexdigest()
    print(f"  Per WLAN erreichbar: {info['host']} {res['ip']}, Slot {info['partition']}, "
          f"MD5 {'passt' if info.get('md5') == md5 else 'weicht ab'}", flush=True)
    if args.skip_ota_test:
        return info.get("md5") == md5
    return ota_test(res["ip"], info)


def main():
    if not sys.stdout.isatty():
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="COM-Port (Standard: der einzige CH340-Port)")
    ap.add_argument("--all", action="store_true", help="Geräte nacheinander am selben Kabel bearbeiten")
    ap.add_argument("--check-only", action="store_true", help="nicht flashen, nur neu starten und prüfen")
    ap.add_argument("--wifi-ssid", help="ins dieses WLAN wechseln, falls der PC das Gerät nicht erreicht")
    ap.add_argument("--skip-ota-test", action="store_true", help="kein Test-Update per WLAN")
    ap.add_argument("--seconds", type=int, default=60, help="max. Wartezeit auf den Start der Firmware")
    args = ap.parse_args()

    if not ota_upload.load_password(None):
        sys.exit("Kein OTA-Passwort in ota_secret.ini - ohne Passwort ist das Update per WLAN abgeschaltet")
    wifi_password = None
    if args.wifi_ssid:
        wifi_password = os.environ.get("NERDMINER_WIFI_PASS") or getpass.getpass(f"WLAN-Passwort für '{args.wifi_ssid}': ")

    if not args.all:
        ports = [args.port] if args.port else ch340_ports()
        if len(ports) != 1:
            sys.exit(f"Bitte Port angeben, gefundene CH340-Ports: {ports or 'keine'}")
        ok = process(ports[0], args, wifi_password)
        print("\nFERTIG - Update per WLAN funktioniert." if ok else "\nNICHT FERTIG - siehe oben.")
        return 0 if ok else 1

    results = {}
    try:
        while True:
            print("\nWarte auf ein Gerät am USB-Kabel (Strg+C beendet) ...", flush=True)
            while not ch340_ports():
                time.sleep(1)
            port = ch340_ports()[0]
            time.sleep(1)
            ok = process(port, args, wifi_password)
            results[f"{port} #{len(results) + 1}"] = ok
            print(f"\n>>> {'FERTIG' if ok else 'FEHLGESCHLAGEN'}. Gerät abziehen, nächstes anstecken. <<<", flush=True)
            while port in ch340_ports():
                time.sleep(1)
    except KeyboardInterrupt:
        pass
    print("\nZusammenfassung: " + ", ".join(f"{k}: {'OK' if v else 'FEHLER'}" for k, v in results.items()))
    return 0 if results and all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
