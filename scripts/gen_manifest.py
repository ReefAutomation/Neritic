# This script generates a manifest.json for esp32FOTA OTA updates
# It should be run in GitHub Actions after firmware build and before release

import os
import json
import glob
import re

# Find the firmware file (assume only one .bin.gz in build output)

# Accept directory argument (default: current dir)
import sys
search_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
bin_files = []
for root, dirs, files in os.walk(search_dir):
    for f in files:
        if f.endswith('.bin.gz') and f.startswith('firmware_esp32d'):
            bin_files.append(os.path.join(root, f))
if not bin_files:
    raise Exception('No .bin.gz firmware for esp32d found!')


# Allow firmware type to be set via env var or CLI arg, default 'DeepGlow'
firmware_type = os.environ.get('FIRMWARE_TYPE')
if len(sys.argv) > 2:
    firmware_type = sys.argv[2]
if not firmware_type:
    firmware_type = 'DeepGlow'

# Allow base_url to be set via env var or CLI arg, default ''
base_url = os.environ.get('FIRMWARE_BASE_URL', '')
if len(sys.argv) > 3:
    base_url = sys.argv[3]
base_url = base_url.rstrip('/') + '/' if base_url else ''

# Read version from VERSION file
try:
    with open('VERSION', 'r') as vf:
        version = vf.read().strip()
except Exception:
    version = "0.0.0"

manifest = []
for firmware_path in bin_files:
    filename = os.path.basename(firmware_path)
    # Match 'firmware_ENV_VER.bin.gz' (e.g., firmware_esp32d_1.0.0.bin.gz)
    env_match = re.search(r'firmware_([a-zA-Z0-9_]+)_', filename)
    if not env_match:
        raise Exception(f"Could not extract environment from filename: {filename}")
    env = env_match.group(1)
    manifest.append({
        "type": firmware_type,
        "env": env,
        "version": version,
        "url": base_url + filename
    })

with open('manifest.json', 'w') as f:
    json.dump(manifest, f, indent=2)
print('Wrote manifest.json:', manifest)
