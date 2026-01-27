

Import("env")
import os
from datetime import datetime

import subprocess

version_file = os.path.join(os.getcwd(), "VERSION")
try:
    with open(version_file, "r") as f:
        version = f.read().strip()
except Exception as e:
    print(f"[gen_version] ERROR reading VERSION file: {e}")
    version = "unknown"


# Append date-time if there are uncommitted git changes
def has_uncommitted_changes():
    try:
        status = subprocess.check_output(["git", "status", "--porcelain"]).decode().strip()
        return bool(status)
    except Exception:
        return False

if has_uncommitted_changes():
    dt = datetime.now().strftime("%Y%m%d-%H%M%S")
    version = f"{version}-{dt}"

env.Append(CPPDEFINES=[('APP_VERSION', '\\"{}\\"'.format(version))])
print(f"[gen_version] Set APP_VERSION macro: {version}")
