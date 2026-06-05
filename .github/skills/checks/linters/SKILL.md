---
name: linters
description: Run linters and report non-fixable linter errors for changed files.
user-invocable: false
---

Purpose

- Run project linters (ESLint, golangci-lint, flake8, etc.) across the repository by default and surface non-fixable issues. When invoked with `scope=changed` (or when called from the `commit` orchestration), restrict to `changed_set`.

Behavior

- Detect linter configs and invoke the appropriate linter commands.
- By default run linters across the full repository. If `scope=changed` is supplied, run only on the `changed_set`.
- Where auto-fix is supported, run fixers first and then re-run the linter to report remaining problems.
- Exit codes:
  - `0`: no linter errors remain.
  - `2`: non-fixable linter errors detected — assistant must pause and require explicit user confirmation to proceed.
  - other non-zero: execution error — abort and show logs.

Notes

- The assistant summarizes linter output and offers guidance to fix errors.
- This skill is separate so repositories can customize or replace it independently.
