---
name: eslint-provision
description: Detect missing ESLint config and optionally provision a richer starter template.
user-invocable: false
---

Purpose

- By default inspect the repository for JS/TS files and ensure ESLint configuration exists or offer to provision a richer template. When invoked with `scope=changed` (or from `commit`), limit inspection to the `changed_set`.

Behavior

- Detect ESLint configuration files near `package.json` or in repo root.
- If none found, offer to copy the provided template `.github/skills/commit/templates/eslint.config.js` into the directory containing `package.json` and print exact `npm install --save-dev ...` commands for required devDependencies.
- This skill never stages the copied template automatically; the user reviews and stages it explicitly.

Notes

- Exit codes: `0` on no-op or successful provisioning prompt; other non-zero on error.
