"""Verify a LOGI4W board is advertising its expected BLE name.

Usage:  python verify-ble.py [name-substring]   (default: MyPropane)
Exit:   0 = a matching advertiser was seen, 1 = not seen / scan error.
Used by build-and-provision.ps1 stage 5 (non-fatal there).
"""
import asyncio
import sys

WANT = (sys.argv[1] if len(sys.argv) > 1 else "MyPropane").lower()


async def main():
    print(f"scanning 9 s for BLE advertisers matching '{WANT}'...")
    try:
        devs = await BleakScanner.discover(timeout=9.0, return_adv=True)
        items = [(adv.local_name or d.name or "", addr, adv.rssi) for addr, (d, adv) in devs.items()]
    except TypeError:  # older bleak without return_adv
        found = await BleakScanner.discover(timeout=9.0)
        items = [((d.name or ""), d.address, getattr(d, "rssi", "?")) for d in found]

    hits = [t for t in items if WANT in (t[0] or "").lower()]
    if hits:
        print("FOUND:")
        for n, a, r in hits:
            print(f"   {n}   {a}   RSSI {r} dBm")
        return 0
    names = sorted({n for n, a, r in items if n})
    print(f"NOT FOUND: no advertiser matching '{WANT}' ({len(items)} seen; sample: {names[:8]})")
    return 1


try:
    from bleak import BleakScanner
except ImportError:
    print("BLE scan unavailable: 'bleak' not installed (pip install bleak)")
    sys.exit(1)

sys.exit(asyncio.run(main()))
