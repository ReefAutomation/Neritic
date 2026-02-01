# PlatformIO pre-build script to generate a dynamic version string for firmware

import os
import sys
import subprocess
import logging
from datetime import datetime
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

# Setup logging
logging.basicConfig(
	level=logging.INFO,
	format='[%(levelname)s] %(message)s'
)

def get_version():
    """Read version from VERSION file."""
    version_file = os.path.join(os.getcwd(), "VERSION")
    try:
        with open(version_file, "r") as f:
            return f.read().strip()
    except Exception as e:
        logging.error(f"[gen_version] ERROR reading VERSION file: {e}")
        return "unknown"

def is_dirty():
    """Check for uncommitted git changes."""
    try:
        status = subprocess.check_output(["git", "status", "--porcelain"]).decode().strip()
        return bool(status)
    except Exception:
        logging.warning('Could not determine git dirty state')
        return False

def make_version_string():
    version = get_version()
    if is_dirty():
        dt = datetime.now().strftime("%Y%m%d-%H%M%S")
        version = f"{version}-{dt}"
    return version

def write_version_headers(version):
    """Write extern declaration and definition to separate .inc files."""
    inc_dir = os.path.join(os.getcwd(), "src", "inc")
    os.makedirs(inc_dir, exist_ok=True)
    version_literal = f'"{version}"'  # C string literal
    # Declaration
    inc_file = os.path.join(inc_dir, "version.inc")
    with open(inc_file, "w") as f:
        f.write("extern const char* FW_VERSION;\n")
    # Definition
    def_file = os.path.join(inc_dir, "version_def.inc")
    with open(def_file, "w") as f:
        f.write(f"const char* FW_VERSION = {version_literal};\n")
    logging.info(f"[gen_version] Generated inc/version.inc (extern) and inc/version_def.inc (def): FW_VERSION = {version_literal}")
    # Optionally: log that assets are now in dist
    logging.info("[gen_version] NOTE: Web assets are now built to ./dist via Preact/Vite.")

def main():
    if env.IsCleanTarget():
        return
    version = make_version_string()
    write_version_headers(version)

main()
