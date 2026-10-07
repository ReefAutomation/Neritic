#!/usr/bin/env python3
"""Regenerate defaults/timezones.json from the IANA tz database.

Each entry is {"name": <IANA zone>, "tz": <POSIX TZ rule>}. The rule is the
footer of the zone's TZif file shipped in the `tzdata` PyPI package, so it
encodes the offset and DST start/end rules. The zone list is taken from the
existing file; add a name there (any "tz") and rerun to include a new zone.

Usage: pip install -U tzdata && python scripts/gen_timezones.py
"""
import json
import pathlib
import sys

import tzdata

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "defaults" / "timezones.json"
ZONEINFO = pathlib.Path(tzdata.__file__).parent / "zoneinfo"


def posix_rule(name: str) -> str:
    data = (ZONEINFO / name).read_bytes()
    if not data.startswith(b"TZif") or not data.endswith(b"\n"):
        raise ValueError(f"{name}: not a TZif v2+ file with a footer")
    return data[:-1].rsplit(b"\n", 1)[1].decode("ascii")


def main() -> int:
    names = sorted(z["name"] for z in json.loads(OUT.read_text()))
    zones = []
    for name in names:
        rule = posix_rule(name)
        if not rule:
            print(f"{name}: empty rule", file=sys.stderr)
            return 1
        zones.append({"name": name, "tz": rule})
    lines = [
        '  { "name": %s, "tz": %s }' % (json.dumps(z["name"]), json.dumps(z["tz"]))
        for z in zones
    ]
    OUT.write_text("[\n" + ",\n".join(lines) + "\n]\n")
    print(f"{len(zones)} zones, tzdata {tzdata.IANA_VERSION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
