# PlatformIO pre-build script to generate a dynamic version string for firmware

import os
import sys
import subprocess
import logging
import re
from datetime import datetime
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

def get_clean_tag():
    """
    Return the current Git tag as a clean version string ONLY if:
      - The tag matches vx.y.z
      - The working tree is clean (no uncommitted changes)
      - The commit is reachable from origin/main
    Otherwise return None.
    """
    # First, ensure the tree is clean – no uncommitted modifications
    if is_dirty():
        logging.info("[gen_version] Working tree is dirty – cannot use a tag as clean version")
        return None

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

        logging.info(f"[gen_version] Using clean tag version: {tag}")
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
      - If a clean tag (vx.y.z, on origin/main, tree clean) exists → use it.
      - Else if the tree is dirty → append a timestamp to the VERSION file value.
      - Else → use the VERSION file value as is.
    """
    version = get_version()
    clean_tag = get_clean_tag()

    if clean_tag:
        return clean_tag

    if is_dirty():
        dt = datetime.now().strftime("%Y%m%d-%H%M%S")
        version = f"{version}-{dt}"
        # Touch CMakeLists.txt to force CMake reconfiguration (if used)
        cmake_file = "CMakeLists.txt"
        if os.path.exists(cmake_file):
            os.utime(cmake_file, None)
            open(cmake_file, "a").close()
        logging.info(f"[gen_version] Dirty build – using timestamped version: {version}")
    else:
        logging.info(f"[gen_version] Clean build (no valid tag) – using base version: {version}")

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
