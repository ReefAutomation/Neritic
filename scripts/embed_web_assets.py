#!/usr/bin/env python3

# PlatformIO pre-build script: embed web assets as .inc files
Import("env")
import os

import subprocess
import sys


import tempfile


# Skip script if PlatformIO target is erase, or clean
import sys
pio_targets = os.environ.get('PIOENV', '') + ' ' + ' '.join(sys.argv)
if any(x in pio_targets for x in ['erase', 'clean']):
	print('embed_web_assets.py: Skipping script for erase/clean target.')
	sys.exit(0)

# Use project root as base (PlatformIO sets cwd to project root)
ASSET_DIR = os.path.join(os.getcwd(), 'data')
OUT_DIR = os.path.join(os.getcwd(), 'src/web_assets')
# Ensure output directory exists
os.makedirs(OUT_DIR, exist_ok=True)



# Download fflate.min.js from CDN if not present
import urllib.request
FFLATE_URL = 'https://cdn.jsdelivr.net/npm/fflate/umd/index.min.js'
FFLATE_LOCAL = os.path.join(ASSET_DIR, 'fflate.min.js')
if not os.path.exists(FFLATE_LOCAL):
	print('Downloading fflate.min.js...')
	try:
		urllib.request.urlretrieve(FFLATE_URL, FFLATE_LOCAL)
		print('fflate.min.js downloaded.')
	except Exception as e:
		print(f'ERROR: Failed to download fflate.min.js: {e}', file=sys.stderr)
		sys.exit(1)


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

ASSETS = [
	('index.html', 'index_html.inc'),
	('wifi.html', 'wifi_html.inc'),
	('app.js', 'app_js.inc'),
	('style.css', 'style_css.inc'),
	('fflate.min.js', 'fflate_min_js.inc'),
]


def minify_asset(infile, ext, do_minify=True):
	def minify_with_html_minifier(infile, ext):
		import tempfile
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
		import tempfile
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
			import tempfile
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
		import tempfile
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
		import re
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
		# Compose both branches with full declaration
		branch_esp = f"#if defined(ESP8266) || defined(ARDUINO_ARCH_AVR)\nstatic const unsigned char {var_name}[] PROGMEM = {array_decl[array_decl.find('{'):array_decl.find('};')+2]}\n{len_decl}\n"
		branch_else = f"#else\nstatic const unsigned char {var_name}[] = {array_decl[array_decl.find('{'):array_decl.find('};')+2]}\n{len_decl}\n#endif\n"
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


# Read minify option from PlatformIO env
minify_opt = os.environ.get('PLATFORMIO_MINIFY')
if minify_opt is None:
	# Try to read from platformio.ini
	import configparser
	config = configparser.ConfigParser()
	config.read(os.path.join(os.getcwd(), 'platformio.ini'))
	minify_opt = config.get('common', 'minify', fallback='true')
do_minify = minify_opt.lower() in ('1', 'true', 'yes', 'on')

all_ok = True
for src, dst in ASSETS:
	ok = to_inc(src, dst, do_minify)
	all_ok = all_ok and ok
if all_ok:
	print('Web assets embedded as .inc files.')
else:
	print('Some assets failed to embed. See errors above.', file=sys.stderr)
	sys.exit(1)
