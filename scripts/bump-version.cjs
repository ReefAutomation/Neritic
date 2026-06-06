#!/usr/bin/env node

const fs = require("node:fs");
const path = require("node:path");

const nextVersion = process.argv[2];

if (!nextVersion) {
  console.error("Missing version argument. Expected semantic version like 1.2.3");
  process.exit(1);
}

const versionFile = path.resolve(__dirname, "..", "VERSION");
fs.writeFileSync(versionFile, `${nextVersion}\n`, "utf8");
console.log(`Updated VERSION to ${nextVersion}`);
