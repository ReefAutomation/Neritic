---
name: dependencies
description: Inspect project dependency manifests for outdated packages, missing lockfiles, and known vulnerabilities. Must produce a structured list of vulnerable dependencies mapped to authoritative advisories (NVD/OSV/GHSA).
argument-hint: dependencies
user-invocable: true
---


Purpose

- Provide a focused dependency health check across the repository's language ecosystems (Go, Node, Python, etc.) that explicitly respects the National Vulnerability Database (NVD).

Limitations & Security notice

- Automated dependency scanners provide best-effort, advisory-driven results — they are not a proof of absence of vulnerabilities. The assistant MUST surface this limitation prominently and MUST NOT present a "clean" or "secure" state as definitive.
- Reasons a scan may miss issues include: incomplete vulnerability feeds, delays between advisory publication and mappings (NVD/OSV/GHSA), transitive dependency complexity, private/internal packages, or tooling/network failures.
- If any ecosystem scan is skipped or partial (tooling missing, Go version mismatch, network blocked), the skill MUST flag the scan as partial and recommend explicit human review and reproducible CI scans with up-to-date tooling.

Behavior

- Detect supported dependency manifests in the repository (e.g. `go.mod`, `package.json`, `requirements.txt`, `pyproject.toml`).
- For each detected ecosystem, run the recommended inspector (for example: `go list -m -u all`, `npm outdated`/`yarn outdated`, `pip list --outdated`, `poetry show --outdated`).
- Check for missing or stale lockfiles (`go.sum`, `package-lock.json`, `pnpm-lock.yaml`, `yarn.lock`, `poetry.lock`) and report suggestions.
- Run vulnerability scanners that consult NVD or canonical vulnerability feeds (prefer OSV mappings to NVD when available). Recommended options:
	- `osv-scanner` or tools that use OSV/NVD mappings;
	- `govulncheck` / `go vuln` for Go modules (which map to upstream advisories/CVEs);
	- `pip-audit` (or `pip-audit` + vulnerability DB mirrors that include NVD/OSV data);
	- `npm audit`/`yarn audit` combined with cross-referencing CVE IDs in NVD.

- If a scanner cannot reach NVD, surface the limitation in results and, when possible, fall back to OSV or repository-source advisories.

Check behavior and requirements

- The skill MUST return a structured vulnerability list (see "Output format" below) and MUST include authoritative identifiers (CVE IDs or GHSA IDs) and direct links to NVD or advisory pages when available.
- If the preferred scanner cannot access NVD due to network issues, the skill MUST fall back to OSV or local advisory mirrors and MUST include a clear note about the fallback.
- If a tool (e.g., `govulncheck`) cannot run (missing toolchain, incompatible Go version), the skill MUST report the limitation and list what checks were skipped.
  
	Example skipped-Go message

	- When the Go vulnerability scan is skipped due to a toolchain mismatch, the skill should emit a clear, actionable message such as:

		"Go scan skipped: `govulncheck` could not run. Detected Go runtime `1.23.0` but project requires `go 1.25.1` (see `go.mod`). To run the Go vulnerability scan install Go >= 1.25.1 or run the scanner in a container: `docker run --rm -v "$PWD":/src -w /src golang:1.25.1 bash -lc "go install golang.org/x/vuln/cmd/govulncheck@latest && govulncheck ./..."`. The scan was recorded as SKIPPED — please re-run after fixing the environment." 

	- The message must include: detected tool version, required version, the reason scan was skipped, and at least one concrete remediation step (local install or Docker command). Do NOT imply absence of vulnerabilities when a scan is skipped.
- The skill MUST check for and report missing or stale lockfiles (`go.sum`, `package-lock.json`, `pnpm-lock.yaml`, `yarn.lock`, `poetry.lock`).
- The skill MUST NOT auto-modify manifests or dependencies. It may suggest exact commands for remediation.

Robustness and retries

- Implement reasonable timeouts (default 30s per external call) and up to 2 retries for transient network errors.
- Cache OSV/NVD query results for short-lived runs to limit repeated network calls in CI loops.
- When running multiple ecosystems, continue best-effort scanning even if one ecosystem scan fails; include per-ecosystem status in the output.

Exit codes

- `0` — no actionable dependency issues detected.
- `5` — outdated or vulnerable dependencies found (NVD/OSV/GHSA matches) — assistant must pause and present summary and remediation options.
- `6` — partial scan: some ecosystems could not be scanned (tooling missing or version mismatch). Output includes skipped checks and reasons.
- other non-zero — execution error; return logs and abort.

Output format

- The primary output MUST be a concise, human-readable report summarizing findings per ecosystem. The report should include:
	- a short header with overall status (OK / Issues found / Partial scan),
	- per-ecosystem sections listing vulnerable packages with: package name, installed version, severity, authoritative IDs (CVE/GHSA/OSV), advisory URL, whether a fix is available and suggested fix version, and a short remediation command.
	- a short "Actions" section with recommended next steps (e.g., run `npm audit fix`, update a direct dependency, install Go 1.25 and re-run `govulncheck`).

- Example human-readable output:

	Dependency check — Summary: 1 moderate advisory found (node), go scan skipped (tooling)

	Node (web/):
	- ws @ 8.0.0 — Moderate — GHSA-58qx-3vcg-4xpx
		Advisory: https://github.com/advisories/GHSA-58qx-3vcg-4xpx
		Fix available: yes — upgrade to 8.20.1
		Remediation: `npm --prefix web install ws@^8.20.1` (or update upstream depender)

	Go:
	- Scan skipped: Go toolchain 1.23.0 incompatible with go.mod (requires 1.25.1)

	Actions:
	1) Run `npm --prefix web audit fix` to apply safe fixes, then `npm --prefix web update`.
	2) Install Go 1.25+ and run `govulncheck ./...`.

- The skill SHOULD also produce an optional machine-readable JSON block (same schema as previously specified) when requested via an argument flag (for CI consumption). When both are produced, the human-readable summary must appear first.

- The assistant must not automatically upgrade or modify dependency manifests without explicit user approval.


Notes

- The assistant must not automatically upgrade or modify dependency manifests without explicit user approval.
- When reporting vulnerabilities, include authoritative identifiers (CVE IDs) and links to the corresponding NVD entries where available, and provide exact commands for remediation (e.g., `npm update`, `go get -u`, `pip install --upgrade ...`).

Fix mode (`--fix`)

- The skill supports an optional `--fix` flag. When provided, the skill will attempt safe, non-destructive fixes after completing scans for ecosystems where automated safe fixes exist.
- Behavior by ecosystem:
	- Node (`package.json`): prefer fixing the root (direct) dependency recorded in `package.json` rather than only modifying transitive entries in the lockfile. Behavior when `--fix` is provided:
		1. Identify advisories that affect packages in the dependency tree. For each advisory, try to map the advisory to a package that is a direct dependency in `package.json` (the "root" or "top-level" depender).
		2. If a direct dependency has a safe fix version available, update the version in `package.json` to the suggested fix (use exact semver strategy consistent with repository policy), then run `npm install --prefix <dir>` to regenerate `package-lock.json`.
		3. After regenerating the lockfile run `npm --prefix <dir> audit --json` to verify whether the advisory is resolved.
		4. If no suitable direct dependency can be updated for an advisory (i.e., the vulnerable package is purely transitive and no direct depender can be upgraded safely), fall back to `npm --prefix <dir> audit fix` to apply safe transitive fixes.

		- Record all file modifications (`package.json`, `package-lock.json`) and produce a clear diff and `fix_results` describing commands run and exit codes.
		- The skill MUST NOT commit or push changes automatically. It will pause and require explicit user approval before creating any commit or performing a push. In CI/non-interactive mode an explicit `--yes`/`--ci-approve` flag is required to proceed without human confirmation.

		Notes:
		- Updating direct dependencies is preferred because it makes the remediation explicit in `package.json` and avoids fragile lockfile-only edits that don't reflect intended API compatibility.
		- Regenerating `package-lock.json` via `npm install` ensures reproducible installs for other developers and CI.
	- Python: attempt `pip-audit --fix` only if the installed `pip-audit` supports `--fix`; otherwise report suggested `pip install --upgrade <pkg>` commands and do not auto-modify.
	- Go: do NOT auto-upgrade Go modules by default. The skill will suggest `go get <module>@<version>` commands and only run them if the user explicitly approves after review. (Automatic `go get -u` is risky for transitive upgrades.)
	- Other ecosystems: prefer vendor/tooling-provided safe-fix commands; if none exist, only provide remediation suggestions.

- Safety and confirmations:
	- `--fix` will only run when explicitly provided by the user (or CI job configured to allow it).
	- If `--fix` would modify manifest files (`package.json`, `go.mod`, `requirements.txt`, lockfiles), the skill MUST list the planned file changes and require explicit confirmation before applying them, unless the environment is non-interactive CI and the caller passed an additional `--yes` flag (CI-only mode).
	- All fix operations are logged; the skill returns a `fix_results` section in the human-readable summary and optional JSON output listing commands run, files changed, and exit codes.

- Exit codes (additional):
	- `7` — fixes applied successfully (one or more ecosystems changed).
	- `8` — fixes attempted but some failed; check `fix_results` for details.
