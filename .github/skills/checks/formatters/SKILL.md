---
name: formatters
description: Run code formatters and auto-fix style issues for changed files.
user-invocable: false
---

Purpose

- Run formatting tools (Prettier, Black, gofmt, etc.) across the repository by default. When invoked with `scope=changed` (or when called from the `commit` orchestration), restricts work to the computed `changed_set`.
- Apply safe auto-fixes where supported and report remaining issues.

Behavior

- Detect formatting tool configuration in the repository and invoke configured commands.
- By default run formatters against the whole repository (or the set of files matching the repository's language/config patterns). If `scope=changed` is supplied, run formatters only on the `changed_set`.
- Exit codes:
  - `0`: all formatters applied successfully, no remaining style errors.
  - `1`: formatters applied but there are outstanding errors (requires review/confirmation).
  - other non-zero: execution error — abort and show logs.

Notes

- The assistant will not auto-stage files after formatting without explicit user approval.
- This skill is intended to be invoked by the `commit` orchestration skill as a separate check.
