"""PlatformIO pre-build script: build the canonical firmware version string.

The firmware reports this string through `/api/version`, `/api/update` and its
mDNS TXT record, and clients compare it against the release manifest, so it has
exactly two shapes:

    vX.Y.Z                    release build (HEAD is the released tag) — never
                              carries a stamp
    vX.Y.Z-YYYYmmdd-HHMMSS    test build (no released tag at HEAD); the stamp
                              is UTC, so it does not depend on the clock or
                              time zone the build happens to run under

The leading `v` matches the Git tag, the release manifest and the published
firmware file name, so a version string from the device compares directly with
a version string from GitHub.
"""

import os
import subprocess
import logging
import re
from datetime import datetime, timezone
from SCons.Script import DefaultEnvironment # type: ignore

env = DefaultEnvironment()

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='[%(levelname)s] %(message)s'
)

def get_version():
    """Read base version from VERSION file."""
    version_file = os.path.join(os.getcwd(), "VERSION")
    try:
        with open(version_file, "r") as f:
            return f.read().strip()
    except Exception as e:
        logging.exception(f"[gen_version] ERROR reading VERSION file: {e}")
        return "unknown"

def canonical_version(version):
    """Return the version with the canonical leading `v` (e.g. `v2.0.0`).

    The VERSION file holds the bare semver (`2.0.0`); releases, tags and the
    OTA manifest all carry the `v` prefix, so the firmware must too.
    """
    return version if version.startswith("v") else f"v{version}"

def build_timestamp():
    """Return the build stamp carried by every test-build version string.

    UTC: the PlatformIO build environment does not inherit the shell's time
    zone, so a local-time stamp would silently be offset from the wall clock
    the build was started from.
    """
    return datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")

def is_dirty():
    """Check for uncommitted git changes."""
    try:
        status = subprocess.check_output(["git", "status", "--porcelain"]).decode().strip()
        return bool(status)
    except Exception:
        logging.warning('Could not determine git dirty state')
        return False

def is_valid_version_tag(tag):
    """Return True if the tag matches the required format vx.y.z."""
    return re.match(r'^v\d+\.\d+\.\d+$', tag) is not None

def is_on_origin_main(commit):
    """
    Check if the given commit (tag or hash) is reachable from origin/main.
    Returns True if origin/main exists and contains the commit.
    """
    try:
        # Make sure we have the latest remote info (optional: add --quiet to suppress output)
        subprocess.run(["git", "fetch", "origin", "main"], check=False, capture_output=True)
        # Check if commit is an ancestor of origin/main
        result = subprocess.run(
            ["git", "merge-base", "--is-ancestor", commit, "origin/main"],
            capture_output=True
        )
        return result.returncode == 0
    except Exception as e:
        logging.warning(f"Cannot verify origin/main: {e}")
        return False

def get_release_tag():
    """
    Return the released version string when HEAD *is* a release:
      - HEAD is exactly tagged vx.y.z
      - that commit is reachable from origin/main
    Otherwise return None.

    Uncommitted changes do NOT disqualify the tag. A release build must never
    carry a build stamp, so the released tag at HEAD decides the version; a
    modified tree is reported as a warning instead of silently turning the
    build into a test build.
    """
    try:
        # Get the exact tag at HEAD
        result = subprocess.run(
            ["git", "describe", "--tags", "--exact-match"],
            capture_output=True,
            text=True,
            check=True
        )
        tag = result.stdout.strip()
        if not is_valid_version_tag(tag):
            logging.info(f"[gen_version] Found tag '{tag}' but it does not match vx.y.z – ignoring")
            return None

        # Get the commit hash that this tag points to
        commit_hash = subprocess.check_output(["git", "rev-parse", "HEAD"]).decode().strip()

        # Verify that the commit is on origin/main
        if not is_on_origin_main(commit_hash):
            logging.info(f"[gen_version] Tag {tag} is not reachable from origin/main – ignoring")
            return None

        if is_dirty():
            logging.warning(
                f"[gen_version] Working tree has uncommitted changes – reporting released version {tag} "
                "with no build stamp"
            )

        logging.info(f"[gen_version] Using release tag version: {tag}")
        return tag
    except subprocess.CalledProcessError:
        # No exact tag at HEAD
        logging.debug("No exact Git tag found at HEAD")
        return None
    except Exception as e:
        logging.warning(f"Unexpected error while checking tag: {e}")
        return None

def make_version_string():
    """
    Build the final version string:
      - A release (HEAD is exactly a vx.y.z tag reachable from origin/main) →
        the tag itself, `vx.y.z`, with NO build stamp: a released firmware must
        never look like a test build.
      - Any other build (no released tag at HEAD) →
        `vx.y.z-YYYYmmdd-HHMMSS`, so an image built outside a release always
        says so and can never be mistaken for a released one.
    """
    base_version = canonical_version(get_version())
    release_tag = get_release_tag()

    if release_tag:
        return release_tag

    version = f"{base_version}-{build_timestamp()}"

    # Touch CMakeLists.txt to force CMake reconfiguration (if used).
    # Required, not cosmetic: PlatformIO only converts the embed_txtfiles
    # assets during a CMake configure, so changing the version alone leaves the
    # previous build's string in the image.
    cmake_file = "CMakeLists.txt"
    if os.path.exists(cmake_file):
        os.utime(cmake_file, None)
        open(cmake_file, "a").close()

    logging.info(f"[gen_version] Test build – using version: {version}")

    return version

def write_version_headers(version):
    """Write the version string to .pio/.version as a plain text file."""
    def_file = os.path.join(".pio", ".version")
    os.makedirs(os.path.dirname(def_file), exist_ok=True)
    with open(def_file, "wb") as f:
        f.write(version.encode('utf-8'))
    logging.info(f"[gen_version] Generated .version with {version}")

def main():
    if env.IsCleanTarget():
        return
    version = make_version_string()
    write_version_headers(version)

main()
