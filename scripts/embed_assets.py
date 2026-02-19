#!/usr/bin/env python3

# PlatformIO pre-build script: embed assets as .inc files
# Import("env")

import os
import subprocess
import sys
import tempfile
import re
import argparse
import logging
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

# Setup logging
logging.basicConfig(
	level=logging.INFO,
	format='[%(levelname)s] %(message)s'
)


# Use project root as base (PlatformIO sets cwd to project root)
ASSET_DIR_DIST = os.path.join(os.getcwd(), 'dist')
ASSET_DIR_SRC = os.path.join(os.getcwd(), 'defaults')
OUT_DIR = os.path.join(os.getcwd(), 'src/inc')

# Output file constants
INDEX_HTML_INC = 'index_html.inc'
INDEX_JS_INC = 'index_js.inc'
STYLE_CSS_INC = 'style_css.inc'
CONFIG_JSON_INC = 'config_default.inc'
PRESETS_JSON_INC = 'presets_json.inc'
TIMEZONES_JSON_INC = 'timezones_json.inc'

ASSETS = [
	(ASSET_DIR_DIST, 'index.html', INDEX_HTML_INC),
	(ASSET_DIR_DIST, 'index.js', INDEX_JS_INC),
	(ASSET_DIR_DIST, 'style.css', STYLE_CSS_INC),
	(ASSET_DIR_SRC, 'config.json', CONFIG_JSON_INC),
	(ASSET_DIR_SRC, 'presets.json', PRESETS_JSON_INC),
	(ASSET_DIR_SRC, 'timezones.json', TIMEZONES_JSON_INC),
]


# Ensure output directory exists
os.makedirs(OUT_DIR, exist_ok=True)


# Only delete and regenerate .inc files for assets that have changed, unless force is True
def asset_needs_update(src_path, inc_path, force=False):
	"""
	Determine if the asset needs to be regenerated.
	Returns True if the source file is newer than the inc file, or if force is True.
	"""
	if force:
		return True
	if not os.path.exists(inc_path):
		return True
	src_mtime = os.path.getmtime(src_path)
	inc_mtime = os.path.getmtime(inc_path)
	return src_mtime > inc_mtime



import gzip

def to_inc(infile, outfile):
	"""
	Gzip HTML, JS, and CSS files before embedding as C header (.inc) files using xxd.
	JSON files are embedded as plain text (not gzipped).
	"""
	try:
		infile_path = infile  # infile is now absolute path
		outfile_path = os.path.join(OUT_DIR, outfile)
		var_base = os.path.splitext(outfile)[0]  # e.g., index_html
		var_name = f"web_{var_base}"
		logging.info(f'Embedding: {infile_path} -> {outfile_path} (var: {var_name}')
		if not os.path.exists(infile_path):
			logging.error(f'Source file not found: {infile_path}')
			return False
		ext = os.path.splitext(infile_path)[1]
		is_json = ext == '.json'
		# Gzip if not JSON
		if not is_json:
			with open(infile_path, 'rb') as f:
				file_content = f.read()
			with tempfile.NamedTemporaryFile('wb', delete=False, suffix=ext+'.gz') as gz_tmp:
				with gzip.GzipFile(fileobj=gz_tmp, mode='wb', compresslevel=9) as gzfile:
					gzfile.write(file_content)
				gz_tmp.flush()
				gz_path = gz_tmp.name
			xxd_input_path = gz_path
		else:
			xxd_input_path = infile_path
		with tempfile.NamedTemporaryFile('r', delete=False, encoding='utf-8', suffix='.inc') as xxd_tmp:
			subprocess.run(['xxd', '-i', '-n', var_name, xxd_input_path], stdout=xxd_tmp, check=True)
			xxd_tmp_path = xxd_tmp.name
		with open(xxd_tmp_path, 'r', encoding='utf-8') as xxd_file:
			xxd_lines = xxd_file.readlines()
		# Replace first line with const and WEB_PROGMEM for non-json
		first = xxd_lines[0]
		first = first.replace('unsigned char', 'const unsigned char')
		xxd_lines[0] = first
		if is_json:
			with open(outfile_path, 'w', encoding='utf-8') as out:
				out.writelines(xxd_lines)
		else:
			macro_block = (
				"#ifndef WEB_PROGMEM\n"
				"#define WEB_PROGMEM\n"
				"#endif\n"
			)
			# Insert 'WEB_PROGMEM' before '='
			if '=' in first:
				parts = first.split('=')
				left = parts[0].rstrip()
				right = '='.join(parts[1:])
				first = f"{left} WEB_PROGMEM = {right}"
				xxd_lines[0] = first
			with open(outfile_path, 'w', encoding='utf-8') as out:
				out.write(macro_block)
				out.writelines(xxd_lines)
		os.remove(xxd_tmp_path)
		if not is_json:
			os.remove(gz_path)
		logging.info(f'Success: {outfile_path}')
		return True
	except Exception as e:
		logging.error(f'Failed to embed {infile}: {e}')
		return False


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
		logging.error(f'npm build failed: {e}')
		sys.exit(1)

def get_force_flag():
	"""
	Parse command-line arguments and environment variables to determine if force regeneration is requested.
	Returns True if --force is passed or EMBED_ASSETS_FORCE is set.
	"""
	parser = argparse.ArgumentParser()
	parser.add_argument('--force', action='store_true', help='Force regeneration of all .inc files')
	args, _ = parser.parse_known_args()
	return args.force or os.environ.get('EMBED_ASSETS_FORCE', '0') in ('1', 'true', 'yes', 'on')


def process_asset(src_path, inc_path, force):
	"""
	Process a single asset: regenerate the .inc file if needed.
	Returns True if successful, False otherwise.
	"""
	if asset_needs_update(src_path, inc_path, force=force):
		if os.path.exists(inc_path):
			try:
				os.remove(inc_path)
			except Exception as e:
				logging.warning(f'Could not delete {inc_path}: {e}')
		return to_inc(src_path, os.path.basename(inc_path))
	else:
		logging.info(f'Skipping unchanged asset: {os.path.basename(src_path)}')
		return True


def main():
	"""
	Main entry point: runs npm build, processes all assets, and logs results.
	Exits with error if any asset fails to embed.
	"""
	if env.IsCleanTarget():
		return
	run_npm_build()
	force = get_force_flag()
	all_ok = True
	for asset in ASSETS:
		asset_dir, src, dst = asset
		src_path = os.path.join(asset_dir, src)
		inc_path = os.path.join(OUT_DIR, dst)
		ok = process_asset(src_path, inc_path, force)
		all_ok = all_ok and ok

	if all_ok:
		logging.info('Web assets embedded as .inc files.')
	else:
		logging.error('Some assets failed to embed. See errors above.')
		sys.exit(1)

main()
