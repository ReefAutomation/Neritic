#!/usr/bin/env python3
"""Run npm build to generate web assets in ./dist"""
import subprocess
import sys
try:
    print("Building web assets...")
    subprocess.run(["npm", "run", "build"], check=True, capture_output=True)
except subprocess.CalledProcessError as e:
    print(f"Build failed: {e}, but continuing...")
