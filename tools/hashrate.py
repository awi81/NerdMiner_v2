#!/usr/bin/env python3
"""Misst die Hashrate aller NerdMiner (CYD) im Netz per WLAN über /info, getrennt nach
Hardware- und Software-Miner (ab Firmware mit hw_hashes/sw_hashes, sonst nur gesamt).

Beispiele (aus dem Projektverzeichnis):
  python tools/hashrate.py                  # alle Geräte suchen, 2 Minuten messen
  python tools/hashrate.py --seconds 300    # länger messen
  python tools/hashrate.py 192.168.178.173  # nur dieses Gerät
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ota_upload  # noqa: E402


def sample(addr):
    try:
        return time.time(), ota_upload.get_info(addr, 4)
    except Exception:
        return None


def main():
    if not sys.stdout.isatty():
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hosts", nargs="*", help="Name oder IP (Standard: alle im lokalen Netz suchen)")
    ap.add_argument("--seconds", type=int, default=120, help="Messdauer")
    ap.add_argument("--subnet", help="zu durchsuchendes Netz, z. B. 192.168.178.0/24")
    args = ap.parse_args()

    devices = [(h, None) for h in args.hosts] if args.hosts else ota_upload.scan(args.subnet)
    addrs = [a for a, _ in devices]
    if not addrs:
        print("Keine Geräte gefunden.")
        return 1

    first = {a: sample(a) for a in addrs}
    khs = {a: [] for a in addrs}
    end = time.time() + args.seconds
    print(f"Messe {len(addrs)} Gerät(e) {args.seconds} s ...", flush=True)
    while time.time() < end:
        time.sleep(5)
        for a in addrs:
            s = sample(a)
            if s and s[1].get("khs", 0) > 0:
                khs[a].append(s[1]["khs"])
    last = {a: sample(a) for a in addrs}

    total = 0.0
    for a in addrs:
        f, l = first[a], last[a]
        if not f or not l:
            print(f"  {a:15s} nicht erreichbar")
            continue
        info = l[1]
        restarted = info.get("uptime_s", 0) < f[1].get("uptime_s", 0)
        line = f"  {info['host']:9s} {a:15s} Laufzeit {info.get('uptime_s', 0) // 60:4d} min"
        if "hw_hashes" in info and not restarted:
            dt = l[0] - f[0]
            hw = ((info["hw_hashes"] - f[1]["hw_hashes"]) & 0xFFFFFFFF) / dt / 1000
            sw = ((info["sw_hashes"] - f[1]["sw_hashes"]) & 0xFFFFFFFF) / dt / 1000
            total += hw + sw
            line += f"  gesamt {hw + sw:6.1f} KH/s  (HW {hw:5.1f}, SW {sw:5.1f})"
        elif khs[a]:
            avg = sum(khs[a]) / len(khs[a])
            total += avg
            line += f"  gesamt {avg:6.1f} KH/s  (Mittel aus {len(khs[a])} Einzelwerten)"
        if "hw_errors" in info and not restarted:
            checked = info["hw_checked"] - f[1]["hw_checked"]
            errors = info["hw_errors"] - f[1]["hw_errors"]
            line += f"  Prüffehler {errors}/{checked}"
            if "hw_hashes" in info:
                # Kandidat = untere 16 Bit null, also 1 von 65536 HW-Hashes; deutlich weniger hieße verlorene Treffer
                expected = ((info["hw_hashes"] - f[1]["hw_hashes"]) & 0xFFFFFFFF) / 65536
                line += f" (erwartet ~{expected:.0f})"
        if "hw_idle" in info and "hw_idle" in f[1] and not restarted:
            # je Zählschritt wartet der HW-Miner 2 ms ohne Job
            idle = (info["hw_idle"] - f[1]["hw_idle"]) * 0.002 / (l[0] - f[0]) * 100
            line += f"  HW-Leerlauf {idle:.1f} %"
        if "hw_kat" in info and info["hw_kat"] >= 0:
            line += "  Selbsttest ok" if info["hw_kat"] == 1 else "  SELBSTTEST FEHLER"
        if restarted:
            line += "  NEUSTART während der Messung"
        if "rssi" in info:
            line += f"  WLAN {info['rssi']} dBm"
        print(line)
    print(f"Summe: {total:.1f} KH/s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
