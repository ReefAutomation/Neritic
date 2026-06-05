---
name: checks
description: Orchestrator command to run repository checks (formatters, linters, dependencies, secret-scan, large-files, eslint-provision). Accepts an optional check name to run a single check.
argument-hint: checks
user-invocable: true
---

Purpose

- Provide a single entrypoint skill `checks` that invokes the per-check skills located in the `checks/` folder.

Behavior

- When invoked, `checks` will:
  1. By default run checks across the entire repository codebase (all tracked source files). This ensures CI-like, comprehensive scans are the default behavior.
  2. Accept optional arguments to tune invocation:
     - `check`: name of a single sub-skill to run (examples: `formatters`, `linters`, `dependencies`, `secret-scan`, `large-files`, `eslint-provision`).
     - `scope`: when set to `changed` (for example when `/commit` orchestrator invokes `checks`), the skill will compute the repository `changed_set` (Added/Modified and untracked files) and run sub-skills only against those files.
     - `--fix`: optional flag which **enables** safe, non-destructive remediation attempts in sub-skills that support it (for example `dependencies --fix`). By default (when `--fix` is not provided) no check should perform automated remediation or modify files.
    
   - Non-interactive sub-skills

   - Sub-skills invoked by the `checks` orchestrator SHOULD be non-interactive. When a sub-skill supports both interactive and non-interactive modes, `checks` will request the non-interactive behavior.

   - `docker-scout` specifics: When the `docker-scout` sub-skill runs, prefer scanning a built container image rather than the raw filesystem. Behavior should be:
     1. If the caller provides an `--image <image-ref>` argument, use that image reference for scanning.
     2. Otherwise, attempt to detect a suitable image name from the repository (for example a `name` in `package.json`, or `nortantis:scan`), and check for a local image with that name.
     3. If no local image exists, build the image from the repository `Dockerfile` using `docker build -t <image-ref> .` (non-interactive). Use `--no-cache` only when explicitly requested.
     4. Run `docker scout quickview <image-ref>` followed by `docker scout cves <image-ref>` (non-interactive). Use `--format`/`--output` options to save machine-readable reports when available.
     5. If building the image or running the scan fails, return a non-zero error and include the raw CLI logs in the aggregated checks output.

   - The `docker-scout` sub-skill MUST NOT modify repository files. When the user requested `--fix`, the skill may suggest commands to remediate (for example, Dockerfile base image updates or `apt-get` package pins) but must not apply them automatically.
  3. If `check=all` or no `check` argument is provided, sequentially invoke the following skills (using the selected `scope`):
     - `formatters`
     - `linters`
     - `dependencies`
     - `docker-scout`  # Docker image/file vulnerability scan using `docker scout quickview`
     - `secret-scan`
     - `large-files`
     - `eslint-provision`
  3. Collect exit codes and logs from each sub-skill and return a consolidated result. When a single check is requested, only that check's exit code/logs are returned.

Exit codes and behavior

- `0` — all checks passed.
- `2` — linters reported non-fixable errors (pause and require confirmation).
- `3` — secret scanner found probable secrets (pause and require confirmation).
- `4` — large files detected (pause and require confirmation).
- other non-zero — execution error; return logs and abort.

Notes

- `checks` is intended as a convenience to call the individual check skills in order; repositories may still provide a single wrapper script if they prefer.
- Skill names for subskills are expected to match their folder names under `.github/skills/checks/`.

Commit and push policy

- The `checks` skill MUST NOT create commits or push changes to the repository under any circumstance. It may run fix commands in a working tree only when the caller provided `--fix`, but it must leave any resulting file modifications uncommitted. The skill must pause and show diffs and require explicit approval before creating any commit or performing a push.
- When fixes would modify files (lockfiles, manifests, source files), the skill MUST produce a clear diff, list of changed files, and exact remediation commands. The skill MUST then pause and request explicit user approval before creating any commit or performing a push. In CI/non-interactive mode the skill may optionally return the planned changes and exit with a non-zero status unless an explicit `--yes`/`--ci-approve` flag was provided by the caller.
