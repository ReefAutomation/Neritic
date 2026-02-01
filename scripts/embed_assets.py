#!/usr/bin/env python3


# PlatformIO pre-build script: embed assets as .inc files
# Import("env")

import os
import subprocess
import sys
import tempfile
import re
import configparser
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
ASSET_DIR_SRC = os.path.join(os.getcwd(), 'src/defaults')
OUT_DIR = os.path.join(os.getcwd(), 'src/inc')
ASSETS = [
	(ASSET_DIR_DIST, 'index.html', 'index_html.inc'),
	(ASSET_DIR_DIST, 'index.js', 'index_js.inc'),
	(ASSET_DIR_DIST, 'style.css', 'style_css.inc'),
	(ASSET_DIR_SRC, 'config.json', 'config_default.inc'),
	(ASSET_DIR_SRC, 'presets.json', 'presets_json.inc'),
	(ASSET_DIR_SRC, 'timezones.json', 'timezones_json.inc'),
	('DYNAMIC', 'app_js.inc'),
]


# Ensure output directory exists
os.makedirs(OUT_DIR, exist_ok=True)


# Only delete and regenerate .inc files for assets that have changed, unless force is True
def asset_needs_update(src_path, inc_path, force=False):
	if force:
		return True
	if not os.path.exists(inc_path):
		return True
	src_mtime = os.path.getmtime(src_path)
	inc_mtime = os.path.getmtime(inc_path)
	return src_mtime > inc_mtime


def minify_asset(infile, ext, do_minify=True):
	# No extra minification; use Vite output as-is
	with open(infile, 'r', encoding='utf-8') as f:
		return f.read()

def to_inc(infile, outfile, do_minify=True):
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
		minified = minify_asset(infile_path, ext, do_minify)
		with tempfile.NamedTemporaryFile('w+', delete=False, encoding='utf-8', suffix=ext) as tmp:
			tmp.write(minified)
			tmp.flush()
			tmp_path = tmp.name
		with tempfile.NamedTemporaryFile('w+', delete=False, encoding='utf-8', suffix='.inc') as xxd_tmp:
			subprocess.run(['xxd', '-i', '-n', var_name, tmp_path], stdout=xxd_tmp, check=True)
			xxd_tmp.flush()
			xxd_tmp_path = xxd_tmp.name
		with open(xxd_tmp_path, 'r', encoding='utf-8') as xxd_file:
			xxd_content = xxd_file.read()
		array_decl_re = re.compile(r'^unsigned char\s+(\w+)\[\]\s*=\s*\{', re.MULTILINE)
		match = array_decl_re.search(xxd_content)
		if not match:
			logging.error(f'Could not find array declaration in {outfile_path}')
			return False
		var_name = match.group(1)
		# Extract array and length variable
		array_decl_re = re.compile(r'(unsigned char\s+\w+\[\]\s*=\s*\{.*?\};)', re.DOTALL)
		len_decl_re = re.compile(r'(unsigned int\s+\w+_len\s*=\s*\d+;)', re.DOTALL)
		array_match = array_decl_re.search(xxd_content)
		len_match = len_decl_re.search(xxd_content)
		if not array_match or not len_match:
			logging.error(f'Could not extract array or length in {outfile_path}')
			return False
		array_decl = array_match.group(1)
		len_decl = len_match.group(1)
		# If the file is a .json, do not use PROGMEM even for ESP8266/AVR
		is_json = ext == '.json'
		if is_json:
			branch = f"const unsigned char {var_name}[] = {array_decl[array_decl.find('{'):array_decl.find('};')+2]}\n{len_decl.replace('static ', '')}\n"
			with open(outfile_path, 'w', encoding='utf-8') as out:
				out.write(branch)
		else:
			branch_esp = f"#if defined(ESP8266) || defined(ARDUINO_ARCH_AVR)\nconst unsigned char {var_name}[] PROGMEM = {array_decl[array_decl.find('{'):array_decl.find('};')+2]}\n{len_decl.replace('static ', '')}\n"
			branch_else = f"#else\nconst unsigned char {var_name}[] = {array_decl[array_decl.find('{'):array_decl.find('};')+2]}\n{len_decl.replace('static ', '')}\n#endif\n"
			with open(outfile_path, 'w', encoding='utf-8') as out:
				out.write(branch_esp)
				out.write(branch_else)
		os.remove(xxd_tmp_path)
		os.remove(tmp_path)
		logging.info(f'Success: {outfile_path}')
		return True
	except Exception as e:
		logging.error(f'Failed to embed {infile}: {e}')
		return False


def main():
	if env.IsCleanTarget():
		return


	# Run npm build before embedding assets
	try:
		logging.info('Running npm run build to generate dist assets...')
		subprocess.run(['npm', 'run', 'build'], check=True)
		logging.info('npm build completed.')
	except Exception as e:
		logging.error(f'npm build failed: {e}')
		sys.exit(1)

	minify_opt = os.environ.get('PLATFORMIO_MINIFY')
	if minify_opt is None:
		config = configparser.ConfigParser()
		config.read(os.path.join(os.getcwd(), 'platformio.ini'))
		minify_opt = config.get('common', 'minify', fallback='true')
	do_minify = minify_opt.lower() in ('1', 'true', 'yes', 'on')


	# Add force parameter via environment variable or command line
	parser = argparse.ArgumentParser()
	parser.add_argument('--force', action='store_true', help='Force regeneration of all .inc files')
	args, unknown = parser.parse_known_args()
	force = args.force or os.environ.get('EMBED_ASSETS_FORCE', '0') in ('1', 'true', 'yes', 'on')
	all_ok = True
	# Find hashed JS and CSS files
	dist_assets = ASSET_DIR_DIST
	js_files = [f for f in os.listdir(dist_assets) if re.match(r'index-.*\.js$', f)]
	css_files = [f for f in os.listdir(dist_assets) if re.match(r'index-.*\.css$', f)]
	app_js_files = [f for f in os.listdir(dist_assets) if re.match(r'app-.*\.js$', f)]

	# Use the first match (should only be one per build)
	index_js = js_files[0] if js_files else None
	style_css = css_files[0] if css_files else None
	app_js = app_js_files[0] if app_js_files else None

	for asset in ASSETS:
		if asset[0] == 'DYNAMIC':
			# Map dynamic assets
			if asset[1] == 'index_js.inc' and index_js:
				src_path = os.path.join(dist_assets, index_js)
				inc_path = os.path.join(OUT_DIR, 'index_js.inc')
			elif asset[1] == 'style_css.inc' and style_css:
				src_path = os.path.join(dist_assets, style_css)
				inc_path = os.path.join(OUT_DIR, 'style_css.inc')
			elif asset[1] == 'app_js.inc' and app_js:
				src_path = os.path.join(dist_assets, app_js)
				inc_path = os.path.join(OUT_DIR, 'app_js.inc')
			else:
				continue
		else:
			asset_dir, src, dst = asset
			src_path = os.path.join(asset_dir, src)
			inc_path = os.path.join(OUT_DIR, dst)
		if asset_needs_update(src_path, inc_path, force=force):
			if os.path.exists(inc_path):
				try:
					os.remove(inc_path)
				except Exception as e:
					logging.warning(f'Could not delete {inc_path}: {e}')
			ok = to_inc(src_path, os.path.basename(inc_path), do_minify)
			all_ok = all_ok and ok
		else:
			logging.info(f'Skipping unchanged asset: {os.path.basename(src_path)}')
	if all_ok:
		logging.info('Web assets embedded as .inc files.')
	else:
		logging.error('Some assets failed to embed. See errors above.')
		sys.exit(1)

main()
