#!/usr/bin/env node
'use strict';

/*
 * Generates locate/public/timezones.json — the zone-name list the static
 * location page checks a browser-detected zone against.
 *
 * The canonical data lives in defaults/timezones.json, which is itself a build
 * artifact produced by scripts/gen_timezones.py. Copying the names out of it
 * here keeps a single source of truth instead of a hand-maintained second list.
 *
 * Usage:  node scripts/build-locate.cjs
 */

const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.resolve(__dirname, '..');
const SOURCE = path.join(ROOT, 'defaults', 'timezones.json');
const TARGET = path.join(ROOT, 'locate', 'public', 'timezones.json');

const relative = (file) => path.relative(ROOT, file) || file;

function fail(message) {
  console.error(`build-locate: ${message}`);
  process.exit(1);
}

if (!fs.existsSync(SOURCE)) {
  fail(
    `missing ${relative(SOURCE)} — generate it first with ` +
      '"source .venv/bin/activate && python scripts/gen_timezones.py"'
  );
}

let entries;
try {
  entries = JSON.parse(fs.readFileSync(SOURCE, 'utf8'));
} catch (error) {
  fail(`cannot parse ${relative(SOURCE)}: ${error.message}`);
}

if (!Array.isArray(entries) || entries.length === 0) {
  fail(`${relative(SOURCE)} must hold a non-empty array`);
}

const names = [];
const seen = new Set();

entries.forEach((entry, index) => {
  if (entry === null || typeof entry !== 'object' || Array.isArray(entry)) {
    fail(`entry #${index} of ${relative(SOURCE)} is not an object`);
  }
  if (typeof entry.name !== 'string' || entry.name.trim() === '') {
    fail(`entry #${index} of ${relative(SOURCE)} has no usable "name"`);
  }
  if (seen.has(entry.name)) {
    fail(`duplicate zone name "${entry.name}" in ${relative(SOURCE)}`);
  }
  seen.add(entry.name);
  names.push(entry.name);
});

names.sort((a, b) => a.localeCompare(b, 'en'));

fs.mkdirSync(path.dirname(TARGET), { recursive: true });
fs.writeFileSync(TARGET, `${JSON.stringify(names, null, 2)}\n`);

console.log(`build-locate: wrote ${names.length} zone names to ${relative(TARGET)}`);
