#!/usr/bin/env python3
"""Firmware per WLAN auf NerdMiner (CYD) laden.

Voraussetzung: Die Geräte laufen bereits mit einer Firmware mit OTA_HTTP_ENABLE (einmalig per USB
über tools/usb_flash_ota.py aufgespielt). Passwort aus --password, $NERDMINER_OTA_PASSWORD oder
ota_secret.ini ([ota] password = ...).

Beispiele (aus dem Projektverzeichnis, nach dem Build):
  python tools/ota_upload.py                 # alle Miner im lokalen Netz suchen und aktualisieren
  python tools/ota_upload.py nerd7BCC.local  # nur dieses Gerät (Name oder IP)
  python tools/ota_upload.py --list          # nur anzeigen, was gefunden wird

Ablauf je Gerät: /info lesen -> Firmware nach /update hochladen -> auf Neustart warten -> per /info
prüfen, dass die MD5 der laufenden Firmware zur Datei passt. Scheitert ein Gerät, wird abgebrochen
(außer mit --keep-going), damit ein Fehler nicht alle Geräte trifft.
"""
import argparse
import base64
import configparser
import concurrent.futures
import hashlib
import ipaddress
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_FIRMWARE = ROOT / ".pio/build/ESP32_2432S028_2USB/firmware.bin"
USER = "nerdminer"


def load_password(cli_value):
    if cli_value:
        return cli_value
    if os.environ.get("NERDMINER_OTA_PASSWORD"):
        return os.environ["NERDMINER_OTA_PASSWORD"]
    cfg = configparser.ConfigParser()
    cfg.read(ROOT / "ota_secret.ini", encoding="utf-8")
    return cfg.get("ota", "password", fallback="").strip()


def get_info(host, timeout=3.0):
    with urllib.request.urlopen(f"http://{host}/info", timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def local_ipv4():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))  # kein Paket wird gesendet, nur Route bestimmen
        return s.getsockname()[0]
    finally:
        s.close()


def local_networks():
    """/24-Netze aller Netzwerkkarten des PCs (z. B. Heimnetz und zusätzlich Gäste-WLAN)."""
    ips = {local_ipv4()}
    try:
        ips |= {a[4][0] for a in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)}
    except OSError:
        pass
    return sorted({ipaddress.ip_network(ip + "/24", strict=False) for ip in ips
                   if not ip.startswith(("127.", "169.254."))})


def scan(subnet=None, timeout=0.8):
    nets = [ipaddress.ip_network(subnet, strict=False)] if subnet else local_networks()
    print("  Netze: " + ", ".join(str(n) for n in nets))

    def probe(ip):
        try:
            info = get_info(str(ip), timeout)
            if str(info.get("host", "")).startswith("nerd"):
                return str(ip), info
        except Exception:
            pass
        return None

    hosts = [ip for net in nets for ip in net.hosts()]
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
        found = [r for r in ex.map(probe, hosts) if r]
    return sorted(found, key=lambda x: x[1]["host"])


def upload(host, data, password, timeout=180):
    boundary = uuid.uuid4().hex
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode() + data + f"\r\n--{boundary}--\r\n".encode()
    req = urllib.request.Request(f"http://{host}/update", data=body, method="POST")
    req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    req.add_header("Authorization", "Basic " + base64.b64encode(f"{USER}:{password}".encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def wait_for(host, md5, old_uptime, timeout=150):
    """Wartet, bis das Gerät die neue Firmware meldet. Neu gestartet, aber mit alter MD5 heißt:
    Update nicht übernommen oder nach Abstürzen auf die vorige Firmware zurückgefallen."""
    end = time.time() + timeout
    last = None
    while time.time() < end:
        time.sleep(3)
        try:
            info = get_info(host, 3)
        except Exception:
            continue  # startet gerade neu
        last = info
        if info.get("md5") == md5:
            return True, info
        if info.get("uptime_s", 1 << 30) < old_uptime:
            return False, info
    return False, last


def main():
    if not sys.stdout.isatty():  # umgeleitet (z. B. Pipe): UTF-8 statt cp1252, damit Umlaute stimmen
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hosts", nargs="*", help="Name oder IP (Standard: alle im lokalen Netz suchen)")
    ap.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    ap.add_argument("--password")
    ap.add_argument("--subnet", help="zu durchsuchendes Netz, z. B. 192.168.178.0/24 (Standard: eigenes /24)")
    ap.add_argument("--list", action="store_true", help="nur Geräte anzeigen")
    ap.add_argument("--force", action="store_true", help="auch hochladen, wenn die Firmware schon läuft")
    ap.add_argument("--keep-going", action="store_true", help="nach einem Fehler mit den übrigen weitermachen")
    args = ap.parse_args()

    if args.hosts:
        devices = []
        for h in args.hosts:
            try:
                devices.append((h, get_info(h)))
            except Exception as e:
                print(f"{h}: nicht erreichbar ({e})")
                if not args.keep_going:
                    return 1
    else:
        print("Suche NerdMiner im lokalen Netz ...")
        devices = scan(args.subnet)
    if not devices:
        print("Keine Geräte gefunden. PC und Miner müssen im selben Netz sein - das Gäste-WLAN der")
        print("Fritz!Box (192.168.179.x) ist vom Heimnetz getrennt. Entweder den PC ins Gäste-WLAN hängen")
        print("oder die Miner ins Heimnetz holen: python tools/usb_flash_ota.py COMx --check-only --wifi-ssid <SSID>")
        return 1
    for addr, info in devices:
        print(f"  {info['host']:9s} {addr:15s} {info['version']:22s} {info['partition']}  "
              f"{info.get('khs', 0)} KH/s  Laufzeit {info.get('uptime_s', 0) // 60} min")
    if args.list:
        return 0

    password = load_password(args.password)
    if not password:
        print("Kein Passwort (ota_secret.ini, $NERDMINER_OTA_PASSWORD oder --password).")
        return 1
    data = args.firmware.read_bytes()
    md5 = hashlib.md5(data).hexdigest()
    print(f"Firmware: {args.firmware} ({len(data) / 1024:.0f} KB, MD5 {md5})")

    failed = 0
    for addr, info in devices:
        name = info["host"]
        if info.get("md5") == md5 and not args.force:
            print(f"{name}: läuft bereits mit dieser Firmware, übersprungen")
            continue
        print(f"{name}: lade hoch ...", flush=True)
        t0 = time.time()
        try:
            status, text = upload(addr, data, password)
        except Exception as e:
            status, text = 0, str(e)
        if status != 200 or "Success" not in text:
            print(f"{name}: Upload fehlgeschlagen (HTTP {status}): {text.strip()[:200]}")
            failed += 1
            if not args.keep_going:
                break
            continue
        print(f"{name}: hochgeladen in {time.time() - t0:.0f} s, warte auf Neustart ...", flush=True)
        ok, new = wait_for(addr, md5, info.get("uptime_s", 1 << 30))
        if ok:
            print(f"{name}: OK - läuft aus {new['partition']}, Version {new['version']}")
        else:
            print(f"{name}: FEHLER - nach dem Neustart nicht die neue Firmware: {new}")
            failed += 1
            if not args.keep_going:
                break
    print("Fertig." if not failed else f"{failed} Gerät(e) fehlgeschlagen.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
