#!/usr/bin/env python3

import subprocess
import sys
import logging
from SCons.Script import DefaultEnvironment # type: ignore

env = DefaultEnvironment()

# Setup logging
logging.basicConfig(
	level=logging.INFO,
	format='[%(levelname)s] %(message)s'
)


def run_npm_build():
	"""
	Run 'npm run build' to generate frontend assets in the dist directory.
	Exits the script if the build fails.
	"""
	try:
		logging.info('Running npm run build to generate dist assets...')
		subprocess.run(['npm', 'run', 'build'], check=True)
		logging.info('npm build completed.')
	except Exception as e:
		logging.exception(f'npm build failed: {e}')
		sys.exit(1)


def main():
	"""
	Main entry point: runs npm build, processes all assets, and logs results.
	Exits with error if any asset fails to embed.
	"""
	if env.IsCleanTarget():
		return
	run_npm_build()

main()
