---
name: secret-scan
description: Run a secret scanner over changed files and staged diffs.
user-invocable: false
---

Purpose

- Detect probable secrets (API keys, tokens, private keys, credentials) in the developer's changes and working tree.

Behavior

- Prefer the organization's configured secret scanner if available; otherwise invoke a repository-provided scanner.
- Scan staged diffs and unstaged working changes so the developer's intent is respected.
- Exit codes:
  - `0`: no probable secrets found.
  - `3`: probable secrets detected — assistant must pause and present masked excerpts and remediation guidance; require explicit confirmation to continue.
  - other non-zero: execution error — abort and show logs.

Notes

- The assistant must not display full secret contents; only masked excerpts with surrounding context.
- This skill is intentionally isolated so secret scanning tooling can be swapped or updated independently.
