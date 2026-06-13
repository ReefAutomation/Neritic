#!/usr/bin/env python3
"""Run npm ci or npm install to fetch dependencies"""
import subprocess
import sys
try:
    subprocess.run(["npm", "ci", "--prefer-offline"], check=True, capture_output=True)
except Exception:
    subprocess.run(["npm", "install", "--prefer-offline"], check=True, capture_output=True=False)
