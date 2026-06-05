---
name: large-files
description: Detect very large files (>5MB) among changed or staged files.
user-invocable: false
---

Purpose

- Identify large files that may be accidentally added to the repository and surface them for review.

Behavior

- By default scan repository files (tracked files matching common source and asset patterns) for sizes exceeding configured thresholds (default 5MB). If `scope=changed` is supplied, scan only the `changed_set`.
- Exit codes:
  - `0`: no large files detected.
  - `4`: large files detected — assistant must pause and require explicit confirmation to continue.
  - other non-zero: execution error — abort and show logs.

Notes

- The assistant suggests alternatives (use LFS, remove from commit, or split files) when large files are found.
