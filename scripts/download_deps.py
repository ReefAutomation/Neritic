#!/usr/bin/env python3

# PlatformIO pre-build script: download required dependencies (fflate.min.js, miniz.h)
import os

import sys
import urllib.request

ASSET_DIR = os.path.join(os.getcwd(), 'data')
DEPENDENCIES = [
    {
        'name': 'fflate.min.js',
        'url': 'https://cdn.jsdelivr.net/npm/fflate/umd/index.min.js',
        'local': os.path.join(ASSET_DIR, 'fflate.min.js'),
    }
]


# Download fflate.min.js as before
for dep in DEPENDENCIES:
    dep_dir = os.path.dirname(dep['local'])
    print(f"[DEBUG] Checking dependency: {dep['name']}")
    os.makedirs(dep_dir, exist_ok=True)
    if not os.path.exists(dep['local']):
        try:
            urllib.request.urlretrieve(dep['url'], dep['local'])
        except Exception as e:
            sys.exit(1)
