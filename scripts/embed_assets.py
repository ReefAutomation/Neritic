#!/usr/bin/env python3


# PlatformIO pre-build script: embed assets as .inc files
# Import("env")
import os
import subprocess
import sys
import urllib.request
import tempfile
import re
import configparser
import argparse

# Skip script if PlatformIO target is erase, or clean
pio_targets = os.environ.get('PIOENV', '') + ' ' + ' '.join(sys.argv)
if any(x in pio_targets for x in ['erase', 'clean', 'buildfs', 'uploadfs']):
	print('embed_assets.py: Skipping script for erase/clean target.')
	sys.exit(0)

# Use project root as base (PlatformIO sets cwd to project root)
ASSET_DIR = os.path.join(os.getcwd(), 'src/assets')
OUT_DIR = os.path.join(os.getcwd(), 'src/inc')

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





# Ensure html-minifier-terser and terser are available once at the start
def ensure_html_minifier():
	try:
		subprocess.run(['npx', 'html-minifier-terser', '--version'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, input='')
	except Exception:
		print('html-minifier-terser not found. Installing...')
		subprocess.check_call(['npm', 'install', 'html-minifier-terser@6.1.0'])
		print('html-minifier-terser installed.')

def ensure_terser():
	try:
		subprocess.run(['npx', 'terser', '--version'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, input='')
	except Exception:
		print('terser not found. Installing...')
		subprocess.check_call(['npm', 'install', 'terser@5.44.1'])
		print('terser installed.')

ensure_html_minifier()
ensure_terser()


# Add config.json as config_default.inc (no minification)
ASSETS = [
	('index.html', 'index_html.inc'),
	('wifi.html', 'wifi_html.inc'),
	('app.js', 'app_js.inc'),
	('style.css', 'style_css.inc'),
	('fflate.min.js', 'fflate_min_js.inc'),
	('config.html', 'config_html.inc'),
	('config.js', 'config_js.inc'),
	('config.json', 'config_default.inc'),
	('timezones.json', 'timezones_json.inc'),
	('presets.json', 'presets_json.inc'),
]


def minify_asset(infile, ext, do_minify=True):
	def minify_with_html_minifier(infile, ext):
		with tempfile.NamedTemporaryFile('w+', delete=False, encoding='utf-8', suffix=ext) as tmp_in:
			with open(infile, 'r', encoding='utf-8') as f:
				tmp_in.write(f.read())
			tmp_in.flush()
			tmp_in_path = tmp_in.name
		tmp_out_path = tmp_in_path + '.min'
		args = ['npx', 'html-minifier-terser', tmp_in_path, '-o', tmp_out_path]
		# Add options based on file type
		if ext == '.html':
			args += [
				'--collapse-whitespace', '--remove-comments', '--minify-js', '--minify-css'
			]
		elif ext == '.css':
			args += ['--minify-css']
		try:
			subprocess.run(args, check=True)
			with open(tmp_out_path, 'r', encoding='utf-8') as f:
				minified = f.read()
			os.remove(tmp_in_path)
			os.remove(tmp_out_path)
			return minified
		except Exception as e:
			print(f"ERROR: html-minifier-terser failed for {infile}: {e}", file=sys.stderr)
			sys.exit(1)
	def minify_with_terser(infile):
		with tempfile.NamedTemporaryFile('w+', delete=False, encoding='utf-8', suffix='.js') as tmp_in:
			with open(infile, 'r', encoding='utf-8') as f:
				tmp_in.write(f.read())
			tmp_in.flush()
			tmp_in_path = tmp_in.name
		tmp_out_path = tmp_in_path + '.min'
		args = ['npx', 'terser', tmp_in_path, '-o', tmp_out_path, '--compress', '--mangle']
		try:
			subprocess.run(args, check=True)
			with open(tmp_out_path, 'r', encoding='utf-8') as f:
				minified = f.read()
			os.remove(tmp_in_path)
			os.remove(tmp_out_path)
			return minified
		except Exception as e:
			print(f"ERROR: Terser failed for {infile}: {e}", file=sys.stderr)
			sys.exit(1)
	if do_minify:
		if ext == '.js':
			return minify_with_terser(infile)
		elif ext in ('.html', '.css'):
			return minify_with_html_minifier(infile, ext)
	with open(infile, 'r', encoding='utf-8') as f:
		return f.read()

def to_inc(infile, outfile, do_minify=True):
	try:
		infile_path = os.path.join(ASSET_DIR, infile)
		outfile_path = os.path.join(OUT_DIR, outfile)
		var_base = os.path.splitext(outfile)[0]  # e.g., index_html
		var_name = f"web_{var_base}"
		print(f'Embedding: {infile_path} -> {outfile_path} (var: {var_name})')
		if not os.path.exists(infile_path):
			print(f'ERROR: Source file not found: {infile_path}', file=sys.stderr)
			return False
		ext = os.path.splitext(infile)[1]
		def minify_js_with_terser(infile):
			with tempfile.NamedTemporaryFile('w+', delete=False, encoding='utf-8', suffix='.js') as tmp_in:
				with open(infile, 'r', encoding='utf-8') as f:
					tmp_in.write(f.read())
				tmp_in.flush()
				tmp_in_path = tmp_in.name
			tmp_out_path = tmp_in_path + '.min'
			try:
				subprocess.run([
					'npx', 'terser', tmp_in_path, '-o', tmp_out_path, '--compress', '--mangle'
				], check=True)
				with open(tmp_out_path, 'r', encoding='utf-8') as f:
					minified = f.read()
				os.remove(tmp_in_path)
				os.remove(tmp_out_path)
				return minified
			except Exception as e:
				print(f"ERROR: Terser minification failed for {infile}: {e}", file=sys.stderr)
				sys.exit(1)
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
			print(f'ERROR: Could not find array declaration in {outfile_path}', file=sys.stderr)
			return False
		var_name = match.group(1)
		# Extract array and length variable
		array_decl_re = re.compile(r'(unsigned char\s+\w+\[\]\s*=\s*\{.*?\};)', re.DOTALL)
		len_decl_re = re.compile(r'(unsigned int\s+\w+_len\s*=\s*\d+;)', re.DOTALL)
		array_match = array_decl_re.search(xxd_content)
		len_match = len_decl_re.search(xxd_content)
		if not array_match or not len_match:
			print(f'ERROR: Could not extract array or length in {outfile_path}', file=sys.stderr)
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
		print(f'Success: {outfile_path}')
		return True
	except Exception as e:
		print(f'ERROR: Failed to embed {infile_path}: {e}', file=sys.stderr)
		return False


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
for src, dst in ASSETS:
	src_path = os.path.join(ASSET_DIR, src)
	inc_path = os.path.join(OUT_DIR, dst)
	if asset_needs_update(src_path, inc_path, force=force):
		if os.path.exists(inc_path):
			try:
				os.remove(inc_path)
			except Exception as e:
				print(f'WARNING: Could not delete {inc_path}: {e}')
		ok = to_inc(src, dst, do_minify)
		all_ok = all_ok and ok
	else:
		print(f'Skipping unchanged asset: {src}')
if all_ok:
	print('Web assets embedded as .inc files.')
else:
	print('Some assets failed to embed. See errors above.', file=sys.stderr)
	sys.exit(1)
