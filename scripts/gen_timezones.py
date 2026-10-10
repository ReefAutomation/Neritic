#!/usr/bin/env python3
"""Generate and validate the canonical timezones.json data asset.

``defaults/timezones.json`` is the single source of truth for the timezone data
shipped to devices: the firmware embeds it as a bootstrap fallback and fetches a
fresh copy at runtime from the ``main`` branch.

The ``offset`` and ``tz`` fields of every entry are *generated* from the IANA
database by this script. ``defaults/timezones.json`` is therefore a build
artifact: it is rewritten on every PlatformIO build and may legitimately be
absent beforehand. Do not hand-edit it — edit ``ZONE_NAMES`` below instead,
which is the curated list of zones the firmware supports.

Generation reads the pinned ``tzdata`` wheel (see ``requirements.txt``) and
never the host's ``/usr/share/zoneinfo``: a stale host database would generate
outdated rules. A missing pin is a hard error, not a silent fallback.

Each generated entry is:

    {
      "name":   IANA timezone name, e.g. "America/New_York",
      "offset": standard-time UTC offset in hours (positive east of UTC),
      "tz":     POSIX TZ string encoding the standard offset and, where
                applicable, the daylight-saving transition rules
    }

For every zone the generator samples the IANA transitions across the horizon
window and expresses them as one recurring POSIX rule
(``M<month>.<week>.<dow>[/<time>]``), then re-checks the result hour by hour
against IANA. A zone whose rules cannot be expressed that way — a fixed-date or
Ramadan-based transition, negative DST, or rules that change inside the window —
fails the run: the file is left untouched and CI opens an issue for a human.

Usage:
    scripts/gen_timezones.py                    # generate + validate + write
    scripts/gen_timezones.py --validate         # validate the file, never write
    scripts/gen_timezones.py --data PATH        # alternate timezone data file

PlatformIO also runs this file directly as a pre-build script (``extra_scripts``
in ``platformio.ini``), so the data file is regenerated before the firmware
embeds it and need not exist beforehand. Exit status is non-zero when
generation or validation fails, so a build or CI run stops rather than shipping
stale rules.
"""

from __future__ import annotations

import argparse
import datetime
import importlib
import importlib.util
import json
import os
import sys
import time
import zoneinfo
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

UTC = datetime.timezone.utc

# SCons exec()s this file as a PlatformIO pre-script without defining __file__,
# so the script's own location is only resolved on the CLI path, below.
DATA_FILE_NAME = os.path.join("defaults", "timezones.json")

# The curated zone set the firmware supports. This is the only hand-maintained
# input: everything else in the data file is generated from IANA. Adding a zone
# here is all that is needed — the next build emits it. Keep the list sorted;
# the output is written in this order.
#
# Africa/Casablanca is deliberately absent: its transitions follow Ramadan, so
# no single recurring POSIX rule can express its year, and the generator would
# fail the build rather than ship a wrong rule.
ZONE_NAMES: tuple[str, ...] = (
    "Africa/Accra",
    "Africa/Algiers",
    "Africa/Cairo",
    "Africa/Johannesburg",
    "Africa/Lagos",
    "Africa/Nairobi",
    "America/Anchorage",
    "America/Argentina/Buenos_Aires",
    "America/Asuncion",
    "America/Bogota",
    "America/Caracas",
    "America/Chicago",
    "America/Denver",
    "America/Halifax",
    "America/Havana",
    "America/Lima",
    "America/Los_Angeles",
    "America/Mexico_City",
    "America/Montevideo",
    "America/New_York",
    "America/Panama",
    "America/Phoenix",
    "America/Santiago",
    "America/Sao_Paulo",
    "America/Toronto",
    "America/Vancouver",
    "Asia/Bangkok",
    "Asia/Dhaka",
    "Asia/Dubai",
    "Asia/Ho_Chi_Minh",
    "Asia/Hong_Kong",
    "Asia/Jakarta",
    "Asia/Jerusalem",
    "Asia/Karachi",
    "Asia/Kathmandu",
    "Asia/Kolkata",
    "Asia/Kuala_Lumpur",
    "Asia/Manila",
    "Asia/Riyadh",
    "Asia/Seoul",
    "Asia/Shanghai",
    "Asia/Singapore",
    "Asia/Taipei",
    "Asia/Tehran",
    "Asia/Tokyo",
    "Australia/Adelaide",
    "Australia/Brisbane",
    "Australia/Melbourne",
    "Australia/Perth",
    "Australia/Sydney",
    "Etc/UTC",
    "Europe/Amsterdam",
    "Europe/Athens",
    "Europe/Berlin",
    "Europe/Brussels",
    "Europe/Bucharest",
    "Europe/Budapest",
    "Europe/Copenhagen",
    "Europe/Dublin",
    "Europe/Helsinki",
    "Europe/Istanbul",
    "Europe/Kyiv",
    "Europe/Lisbon",
    "Europe/London",
    "Europe/Madrid",
    "Europe/Moscow",
    "Europe/Oslo",
    "Europe/Paris",
    "Europe/Prague",
    "Europe/Rome",
    "Europe/Stockholm",
    "Europe/Vienna",
    "Europe/Warsaw",
    "Europe/Zurich",
    "Pacific/Auckland",
    "Pacific/Fiji",
    "Pacific/Honolulu",
)

# How far ahead to generate/validate. A single POSIX TZ string cannot express a
# rule that itself changes in some future year (e.g. Chile moving its DST end
# date in 2029). Once such a change enters this horizon the comparison fails and
# CI flags it, prompting a human to decide how to encode the zone.
HORIZON_YEARS = 2

# The IANA database is sampled at this resolution when locating transitions.
# Real transitions land on 15-minute boundaries at worst (e.g. :30 DST deltas),
# so this never misses one.
SAMPLE_STEP = datetime.timedelta(minutes=15)

# Pinned IANA database. Pointing zoneinfo at this wheel instead of the host's
# /usr/share/zoneinfo keeps generation deterministic and current: a stale host
# database can lag the IANA release, which would generate outdated rules or
# report false mismatches. Bump the pin in requirements.txt to pick up a new
# release.
PINNED_TZDATA = "tzdata"


class ZoneNotRepresentable(Exception):
    """Raised when a zone's IANA rules fit no single POSIX TZ string."""


class PinnedDataMissing(Exception):
    """Raised when the pinned IANA database is not installed."""


def pinned_zoneinfo_dir() -> str:
    """Directory holding the pinned IANA database (inside the tzdata wheel)."""
    spec = importlib.util.find_spec(PINNED_TZDATA)
    if spec is None or not spec.origin:
        raise PinnedDataMissing(
            f"pinned '{PINNED_TZDATA}' package is required (see "
            "requirements.txt); refusing to generate or validate against the "
            "host tzdata, which may be stale"
        )

    data_dir = os.path.join(os.path.dirname(spec.origin), "zoneinfo")
    if not os.path.isdir(data_dir):
        raise PinnedDataMissing(
            f"{data_dir} is missing; reinstall the '{PINNED_TZDATA}' package"
        )
    return data_dir


def use_pinned_iana_data() -> str:
    """Restrict zoneinfo to the pinned tzdata wheel; return its IANA version.

    A missing package is a hard error — generating or validating against the
    host database would produce results that depend on the machine running the
    script.
    """
    zoneinfo.reset_tzpath([pinned_zoneinfo_dir()])
    ZoneInfo.clear_cache()

    module = importlib.import_module(PINNED_TZDATA)
    return getattr(module, "IANA_VERSION", module.__version__)


def posix_footer(name: str) -> str:
    """The POSIX TZ rule tzcode stores at the end of a zone's TZif file.

    This is IANA's own statement of the zone's rule after its last transition,
    which is how a zone that drops daylight saving (e.g. America/Vancouver) is
    recognised: its footer carries no DST rules at all.
    """
    path = os.path.join(pinned_zoneinfo_dir(), name)
    try:
        with open(path, "rb") as fh:
            lines = fh.read().splitlines()
    except OSError as exc:
        raise ZoneNotRepresentable(f"{name}: cannot read TZif data ({exc})")

    if not lines:
        raise ZoneNotRepresentable(f"{name}: TZif file has no POSIX footer")
    return lines[-1].decode("ascii").strip()


def horizon_window(years: int = HORIZON_YEARS) -> tuple[datetime.datetime,
                                                        datetime.datetime]:
    """Return the (start, end) UTC window used for generation and validation.

    Anchored to 1 January so repeated runs within a year see identical data and
    CI does not churn out spurious changes.
    """
    start = datetime.datetime(datetime.datetime.now(UTC).year, 1, 1, tzinfo=UTC)
    return start, start.replace(year=start.year + years)


def default_data_path() -> str:
    """Path to the data file next to this script (CLI use only)."""
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(repo_root, DATA_FILE_NAME)


def load_entries(path: str) -> list[dict]:
    """Read and shape-check the generated timezone data file."""
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)

    if not isinstance(doc, list):
        raise ValueError(f"{path}: expected a JSON array of timezone objects")

    entries: list[dict] = []
    for index, item in enumerate(doc):
        if not isinstance(item, dict):
            raise ValueError(f"{path}: entry {index} is not an object")
        name = item.get("name")
        if not isinstance(name, str) or not name:
            raise ValueError(f"{path}: entry {index} has no 'name'")
        if not isinstance(item.get("offset"), (int, float)):
            raise ValueError(f"{path}: '{name}' has no numeric 'offset'")
        tz = item.get("tz")
        if not isinstance(tz, str) or not tz:
            raise ValueError(f"{path}: '{name}' has no 'tz' POSIX string")
        entries.append({"name": name, "offset": item["offset"], "tz": tz})
    return entries


def normalize(entries: list[dict]) -> str:
    """Return canonical JSON text: sorted by name, stable formatting."""
    ordered = sorted(entries, key=lambda e: e["name"])
    return json.dumps(ordered, indent=2) + "\n"


# ── POSIX TZ string synthesis ───────────────────────────────────────────────


def posix_offset(seconds: float) -> str:
    """Render a POSIX offset field: the value added to local time to get UTC."""
    total = -int(round(seconds))
    sign = "-" if total < 0 else ""
    hours, rest = divmod(abs(total), 3600)
    minutes, secs = divmod(rest, 60)
    if secs:
        return f"{sign}{hours}:{minutes:02d}:{secs:02d}"
    if minutes:
        return f"{sign}{hours}:{minutes:02d}"
    return f"{sign}{hours}" if hours else "0"


def posix_name(abbreviation: str) -> str:
    """Render an abbreviation, quoting it when it is not a POSIX name."""
    if abbreviation.isalpha() and len(abbreviation) >= 3:
        return abbreviation
    return f"<{abbreviation}>"


def days_in_month(year: int, month: int) -> int:
    nxt = datetime.date(year + (month == 12), month % 12 + 1, 1)
    return (nxt - datetime.timedelta(days=1)).day


def transition_rule(local_before: datetime.datetime) -> str:
    """POSIX ``M<month>.<week>.<dow>[/<time>]`` for a transition's local time.

    ``local_before`` is the wall clock immediately before the transition, which
    is the basis POSIX interprets a rule's ``/<time>`` field in.
    """
    year, month = local_before.year, local_before.month
    week = (local_before.day - 1) // 7 + 1
    if local_before.day + 7 > days_in_month(year, month):
        week = 5  # "last <weekday> of the month"
    dow = (local_before.weekday() + 1) % 7  # POSIX: Sunday == 0
    clock = (
        f"{local_before.hour}"
        if local_before.minute == 0
        else f"{local_before.hour}:{local_before.minute:02d}"
    )
    # The time is always written out, even when it equals POSIX's implicit
    # 02:00 default, so the file stays self-documenting.
    return f"M{month}.{week}.{dow}/{clock}"


def zone_transitions(zone: ZoneInfo, start: datetime.datetime,
                     end: datetime.datetime
                     ) -> list[tuple[datetime.datetime, datetime.timedelta,
                                     datetime.timedelta, str]]:
    """Sample a zone across the window and return its offset transitions.

    Each item is ``(instant, offset_before, offset_after, abbreviation_after)``.
    """
    transitions: list[tuple[datetime.datetime, datetime.timedelta,
                            datetime.timedelta, str]] = []
    instant = start
    previous = instant.astimezone(zone).utcoffset()
    if previous is None:
        raise ZoneNotRepresentable(f"{zone.key}: zone reports no UTC offset")
    while instant < end:
        instant += SAMPLE_STEP
        current = instant.astimezone(zone)
        offset = current.utcoffset()
        abbreviation = current.tzname()
        if offset is None or not abbreviation:
            raise ZoneNotRepresentable(
                f"{zone.key}: zone reports no offset or abbreviation"
            )
        if offset != previous:
            transitions.append((instant, previous, offset, abbreviation))
            previous = offset
    return transitions


def offset_hours(seconds: float) -> float | int:
    """Convert an offset in seconds to the hours value used in the data file."""
    hours = seconds / 3600.0
    return int(hours) if hours.is_integer() else round(hours, 4)


def generate_entry(name: str, start: datetime.datetime,
                   end: datetime.datetime) -> dict:
    """Synthesize the ``offset`` and ``tz`` fields for one zone from IANA."""
    zone = ZoneInfo(name)

    # A footer without DST rules is IANA saying the zone has no recurring
    # transition left: once it has taken effect the offset is fixed, even if the
    # window still contains the change that led there. Emit IANA's own rule and
    # let validation check it from the point it applies.
    footer = posix_footer(name)
    if "," not in footer:
        settled = end.astimezone(zone)
        settled_offset = settled.utcoffset()
        if settled_offset is None:
            raise ZoneNotRepresentable(f"{name}: zone reports no UTC offset")
        return {
            "name": name,
            "offset": offset_hours(settled_offset.total_seconds()),
            "tz": footer,
        }

    transitions = zone_transitions(zone, start, end)
    if not transitions:
        raise ZoneNotRepresentable(
            f"{name}: IANA defines DST rules but none apply inside the window"
        )

    # tzcode labels the offset a fall-back transition returns to as standard
    # time, so the direction of each transition identifies which offset is
    # standard and which is daylight time.
    starts, ends = [], []
    standard = daylight = None
    for instant, before, after, _ in transitions:
        local_before = (instant + before).replace(tzinfo=None)
        rule = transition_rule(local_before)
        if after > before:  # spring forward: daylight time begins
            daylight = after
            standard = before if standard is None else standard
            starts.append(rule)
        else:  # fall back: standard time resumes
            standard = after
            daylight = before if daylight is None else daylight
            ends.append(rule)

    if standard is None or daylight is None:
        raise ZoneNotRepresentable(f"{name}: no standard/daylight pair found")

    if len(set(starts)) != 1 or len(set(ends)) != 1:
        raise ZoneNotRepresentable(
            f"{name}: transitions do not repeat one rule "
            f"(starts={sorted(set(starts))}, ends={sorted(set(ends))})"
        )

    delta = daylight - standard
    if delta <= datetime.timedelta(0):
        raise ZoneNotRepresentable(
            f"{name}: negative or zero DST delta ({delta}) cannot be encoded "
            "as a standard/daylight pair"
        )

    standard_abbr = daylight_abbr = None
    for _, before, after, abbreviation in transitions:
        if after > before:
            daylight_abbr = abbreviation
        else:
            standard_abbr = abbreviation
    if not standard_abbr or not daylight_abbr:
        raise ZoneNotRepresentable(f"{name}: could not sample abbreviations")

    tz = posix_name(standard_abbr) + posix_offset(standard.total_seconds())
    tz += posix_name(daylight_abbr)
    if delta != datetime.timedelta(hours=1):  # POSIX assumes standard + 1h
        tz += posix_offset(daylight.total_seconds())
    tz += f",{starts[0]},{ends[0]}"

    return {
        "name": name,
        "offset": offset_hours(standard.total_seconds()),
        "tz": tz,
    }


# ── Validation ──────────────────────────────────────────────────────────────


def posix_wall_clock(posix: str, ts: float) -> tuple[int, int, int, int, int]:
    """Return (year, month, day, hour, minute) for ``ts`` under a POSIX TZ."""
    os.environ["TZ"] = posix
    time.tzset()
    lt = time.localtime(ts)
    return (lt.tm_year, lt.tm_mon, lt.tm_mday, lt.tm_hour, lt.tm_min)


def posix_standard_offset(posix: str) -> float:
    """Standard-time UTC offset in hours encoded by a POSIX TZ string."""
    os.environ["TZ"] = posix
    time.tzset()
    # time.timezone is seconds WEST of UTC for standard time.
    return -time.timezone / 3600.0


def validate(entries: list[dict], years: int = HORIZON_YEARS) -> list[str]:
    """Validate every entry against the pinned IANA database.

    Returns a list of human-readable errors; empty means all good.
    """
    errors: list[str] = []
    window_start, window_end = horizon_window(years)

    seen: set[str] = set()
    for entry in entries:
        name = entry["name"]
        posix = entry["tz"]
        if name in seen:
            errors.append(f"{name}: duplicate entry")
            continue
        seen.add(name)

        try:
            zone = ZoneInfo(name)
        except ZoneInfoNotFoundError:
            errors.append(f"{name}: IANA zone not found in pinned tzdata")
            continue

        # 1. Declared standard offset must match what the POSIX string encodes.
        actual_std = posix_standard_offset(posix)
        if abs(actual_std - float(entry["offset"])) > 1e-9:
            errors.append(
                f"{name}: POSIX standard offset {actual_std}h != declared "
                f"{entry['offset']}h"
            )
            continue

        # 2. Hourly wall-clock comparison across the horizon window.
        #
        # A fixed-offset rule only describes the zone once IANA stops changing
        # its offset; earlier in the window the zone still followed its old
        # rules, so compare from the last transition instead of the window start.
        compare_from = window_start
        if "," not in posix:
            settled = zone_transitions(zone, window_start, window_end)
            if settled:
                compare_from = settled[-1][0]

        bad: list[str] = []
        hours = int((window_end - compare_from).total_seconds() // 3600)
        for h in range(hours):
            dt = compare_from + datetime.timedelta(hours=h)
            ts = dt.timestamp()
            posix_wall = posix_wall_clock(posix, ts)
            iana = dt.astimezone(zone)
            iana_wall = (iana.year, iana.month, iana.day, iana.hour, iana.minute)
            if posix_wall != iana_wall:
                bad.append(dt.strftime("%Y-%m-%d %H:%M UTC"))
                if len(bad) >= 3:
                    break
        if bad:
            errors.append(
                f"{name}: wall-clock mismatch at {bad} (rules may have changed)"
            )

    return errors


def run(data_path: str, validate_only: bool) -> int:
    """Regenerate (or validate) the data file. Returns a process exit code."""
    try:
        iana_version = use_pinned_iana_data()
    except PinnedDataMissing as exc:
        print(f"timezone data unavailable: {exc}", file=sys.stderr)
        return 1
    print(f"iana-version: {iana_version}")

    try:
        if validate_only:
            entries = load_entries(data_path)
        else:
            start, end = horizon_window()
            entries = [generate_entry(name, start, end) for name in ZONE_NAMES]
    except ZoneNotRepresentable as exc:
        print(
            f"timezone generation FAILED (IANA {iana_version}):", file=sys.stderr
        )
        print(f"  - {exc}", file=sys.stderr)
        return 1
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"timezone data invalid: {exc}", file=sys.stderr)
        return 1

    errors = validate(entries)
    if errors:
        print(
            f"timezone validation FAILED (IANA {iana_version}):", file=sys.stderr
        )
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    if validate_only:
        print(f"{data_path} is valid ({len(entries)} zones, IANA {iana_version}).")
        return 0

    content = normalize(entries)
    try:
        with open(data_path, "r", encoding="utf-8") as fh:
            existing = fh.read()
    except FileNotFoundError:
        # Expected: the data file is an artifact and need not exist yet.
        existing = ""

    if existing != content:
        with open(data_path, "w", encoding="utf-8") as fh:
            fh.write(content)
        print(
            f"Regenerated {data_path} ({len(entries)} zones, IANA "
            f"{iana_version})."
        )
    else:
        print(
            f"{data_path} is up to date ({len(entries)} zones, IANA "
            f"{iana_version})."
        )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--data",
        default=default_data_path(),
        help="timezone data file to read/write (default: defaults/timezones.json)",
    )
    parser.add_argument(
        "--validate",
        action="store_true",
        help="validate the file as committed; never regenerate or write",
    )
    args = parser.parse_args()
    return run(args.data, args.validate)


def run_as_pre_script() -> None:
    """PlatformIO entry point (see ``extra_scripts`` in ``platformio.ini``).

    Generates the data file before the firmware embeds it, so the file does not
    need to exist on a clean checkout. A failure aborts the build instead of
    letting it embed stale or missing rules.
    """
    from SCons.Script import DefaultEnvironment  # type: ignore

    env = DefaultEnvironment()
    if env.IsCleanTarget():
        return

    # PlatformIO runs SCons from the project root; resolve the data file there
    # because __file__ is unavailable in this execution context.
    data_path = os.path.join(os.getcwd(), DATA_FILE_NAME)
    print(f"[gen_timezones] Generating {DATA_FILE_NAME} from IANA...")
    status = run(data_path, validate_only=False)
    if status != 0:
        sys.exit(status)
    print("[gen_timezones] Timezone data generation completed.")


# Under PlatformIO this file is executed by SCons as a pre-build script, where
# sys.argv belongs to the build. Everywhere else it behaves as the CLI above.
if "SCons.Script" in sys.modules:
    run_as_pre_script()
elif __name__ == "__main__":
    sys.exit(main())
