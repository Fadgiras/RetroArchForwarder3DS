#!/usr/bin/env python3
"""Builds the game index shipped in romfs, one file per system.

    python make_index.py                            # PlayStation, the default
    python make_index.py "Sega - Mega-CD - Sega CD"
    python make_index.py "Nintendo - Super Nintendo Entertainment System" --source no-intro

Output lands in romfs/db/<system>.txt, one record per line:

    SERIAL|Game Name (Region)

The serial is empty for systems that have none, cartridge platforms mostly.
The app uses it two ways: a disc that states its own serial resolves straight
to a name, and the on-console keyboard filters the names of the same file.
So an index without serials still makes the keyboard work.

Sources, both from libretro-database, the one RetroArch scans with, so the
names line up with the thumbnail repository:

    redump    disc systems, carries serials
    no-intro  cartridge systems, names only

Available systems: https://github.com/libretro/libretro-database
"""

import argparse
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

BASE = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat"
DEFAULT_SYSTEM = "Sony - PlayStation"
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "romfs", "db")


def fetch(system, source):
    url = "%s/%s/%s.dat" % (BASE, source, urllib.parse.quote(system))
    print("fetching %s" % url)
    try:
        with urllib.request.urlopen(url, timeout=180) as r:
            return r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        if e.code == 404:
            sys.exit("no %s database for %r - check the exact system name" % (source, system))
        raise


def parse(text):
    """Yields (serial, name); serial is empty when the dat carries none."""
    for block in re.finditer(r'game \(\s*name "([^"]+)"(.*?)\n\)', text, re.S):
        name, body = block.group(1), block.group(2)
        serials = re.findall(r'serial "([^"]+)"', body)
        if not serials:
            yield "", name
            continue
        for raw in serials:
            for one in (s.strip() for s in raw.split(",")):
                if re.fullmatch(r"[A-Z]{2,4}-?\d{3,5}", one):
                    yield one, name


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("system", nargs="?", default=DEFAULT_SYSTEM)
    ap.add_argument("--source", choices=("redump", "no-intro"), default="redump")
    args = ap.parse_args()

    text = fetch(args.system, args.source)

    by_serial = {}
    names = set()
    for serial, name in parse(text):
        names.add(name)
        if not serial:
            continue
        # Several dumps share a serial (revisions, EDC variants). Keep the
        # shortest name: the plain edition, the one most likely to have artwork.
        if serial not in by_serial or len(name) < len(by_serial[serial]):
            by_serial[serial] = name

    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, args.system + ".txt")

    # Entries with a serial first, then the names that have none, so the
    # keyboard still sees every title.
    seen = set(by_serial.values())
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        for serial in sorted(by_serial):
            f.write("%s|%s\n" % (serial, by_serial[serial]))
        for name in sorted(n for n in names if n not in seen):
            f.write("|%s\n" % name)

    size = os.path.getsize(out_path)
    print("%d serials, %d titles -> %s (%.2f MB)"
          % (len(by_serial), len(names), out_path, size / 1048576))


if __name__ == "__main__":
    sys.exit(main())
