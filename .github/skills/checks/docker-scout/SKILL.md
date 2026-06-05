---
name: docker-scout
description: Scan Dockerfiles and container images for known vulnerabilities using `docker scout quickview`.
argument-hint: docker-scout
user-invocable: false
---

Purpose

- Provide a focused check that inspects `Dockerfile` artifacts and repository container images (when available) for known vulnerabilities using Docker Scout's `quickview` command.

Behavior

- Detect `Dockerfile` files in the repository root and subdirectories. If no `Dockerfile` is present, the skill will no-op with exit code `0` and a brief note.
 - For each detected `Dockerfile` (or the repository root when a `Dockerfile` is at root) the skill will invoke Docker Scout directly (no wrapper scripts). The implementing agent MUST execute each CLI call as an individual command (for example, invoke `docker scout quickview` as one command, then invoke `docker scout cves` for each discovered target as separate commands). Do not run multi-command shell scripts or chained pipelines. Prefer capturing CLI output on stdout and parse that output in-memory to produce a concise report. Example invocation form (tooling may require an explicit artifact scheme):

   docker scout quickview fs://<path>

   # Note: run `docker scout quickview` without `--output` so the report is written to
   # stdout and can be parsed in-memory. The `--output` flag writes the report to the
   # provided file path; passing `--output json` will create a file literally named
   # `json` in the current working directory. To avoid writing any report files into the
   # repository, capture and parse stdout (or run the command in a temporary working
   # directory and remove any artifacts). The default non-file mode is preferred for
   # chat-first operation.

  The skill must not print or suggest ad-hoc shell scripts that batch multiple calls together; instead present exact, minimal remediation steps when needed. Any attempt to *apply* fixes must only occur when the caller provided `--fix` to the `checks` orchestrator; otherwise the skill will only report and recommend fixes.

 - The skill should capture and parse machine-readable data when possible, but it MUST NOT write report files into the repository. Prefer capturing and parsing CLI stdout (no `--output`) and parse that text into structured results. If machine-readable JSON is only available via an output file, write it to a non-repository temporary location (for example `/tmp`) and securely delete it after parsing.
  
- Automatic CVE listing

- After running `docker scout quickview` the skill MUST automatically run `docker scout cves <target>` for each image target found (for example `local://nortantis:local`), without prompting the user. The CVE listing should be requested in machine-readable form when supported and parsed into the final report. The skill will include the full CVE list per-target (advisory id, severity, affected package/component, fixed versions or suggested mitigation) in the human-readable summary.

- The skill must not require interactive confirmation before invoking `docker scout cves` — this behavior is mandatory for the `docker-scout` skill. If the environment prevents running `cves` (missing CLI, network issue, or permission), the skill should surface a clear error and continue with other targets.

- Non-interactive enforcement

- This skill is explicitly non-interactive when run via the `checks` orchestrator. It MUST NOT emit follow-up questions such as "Would you like me to run docker scout cves..." or request confirmation to run `cves`. The implementing agent must run `quickview` and then immediately run `cves` for each discovered target.

- Output and files

 - Chat-first output

 - The skill MUST produce a concise, human-readable summary directly in the chat (the conversation response) when invoked via the `checks` orchestrator. The chat report must include which Dockerfile(s) were scanned (path links when possible), a per-context list of found vulnerabilities (package/component, installed/base version, severity, advisory ID and link), and suggested remediations or exact commands for human triage.

 - The skill MUST NOT write machine-readable CVE files into the repository by default. If machine-readable output is captured for parsing, it should be kept in a temporary location and only written to disk outside the repository or returned to the caller on request; writing to `build/docker-scout/` is disallowed for the chat-first mode.
 - The skill MUST NOT write machine-readable CVE files into the repository by default. To enforce this:
   - Run `docker scout` commands from a temporary working directory (e.g. `tmpdir=$(mktemp -d) && pushd "$tmpdir"`) and capture `--output json` there, or use an explicit output file outside the repo (for example `--output-file /tmp/quickview.json`).
   - If the CLI prints JSON to stdout, capture it and parse it in-memory rather than letting the CLI create files in the repo.
   - Remove or securely delete temporary files after parsing. NEVER leave `json`, `scout-cves.json`, or similarly named files in the repository root.

 - Example non-interactive behavior when scanning `local://nortantis:local`:

  * Run `docker scout quickview` and then run `docker scout cves` for each discovered target as separate, sequential commands executed by the agent; capture machine-readable output only for parsing and keep it ephemeral (do not write into the repository). Present the parsed, human-readable summary directly in chat rather than saving files in the repository.
   * If callers explicitly request machine-readable artifacts, the skill may offer them as downloadable attachments or write them to a configurable external path (not the repository work tree) after explicit confirmation.

- If any command fails due to missing CLI or environment, the skill must include an actionable error message and exit with code `10` for scanner invocation failure. Partial failures for individual targets should be aggregated and reported, but must not cause the skill to prompt for human confirmation.
- If `docker` or `docker scout` are not present in PATH or the Docker daemon is unreachable, the skill MUST return an exit code indicating the scanner could not run (see exit codes) and include clear remediation steps (install Docker, sign in to Docker Hub if required, or install Docker Scout CLI plugin).

Robustness

- Use sensible timeouts for the command (default ~60s per invocation) and surface transient network errors as retriable information in the report.
- Continue scanning other Dockerfiles even if one invocation fails; aggregate results and per-file statuses in the final report.

Exit codes

- `0` — no vulnerabilities detected in scanned Dockerfiles/contexts.
- `9` — vulnerabilities found that require human review (assistant must pause and present summary and remediation options).
- `10` — scanner could not be invoked (missing `docker`/`docker scout` or Docker daemon unavailable); report what was tried and how to fix it.
- other non-zero — execution error; include raw logs.

Output

- The human-readable summary must include:
  - which Dockerfile(s) were scanned (path links when possible),
  - per-context list of found vulnerabilities: package/component, installed/base version, severity, advisory ID and link, and suggested remediation (e.g., update base image tag or rebuild image with patched base),
  - actionable remediation commands where strictly necessary for human triage (for example: `# update base image in Dockerfile: FROM ubuntu:22.04 -> FROM ubuntu:22.04@sha256:<patched>`). The skill will avoid recommending general convenience commands unless they are required to verify a remediation.
  - a short "Next steps" section advising CI scans, pinning base images, and re-running `docker scout quickview` after fixes.

Notes

- The skill must not attempt automatic remediation or create commits. It will provide exact commands and diffs (if applicable) for human review, but will only attempt to run remediation steps when the caller explicitly supplied `--fix` to `checks`. Even then, any file modifications remain uncommitted and the skill must present diffs and require explicit confirmation before committing or pushing.
- The skill is intentionally minimal: it relies on `docker scout quickview` output and does not attempt to re-implement vulnerability databases.
