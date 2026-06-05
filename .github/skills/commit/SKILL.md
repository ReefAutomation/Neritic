---
name: commit
description: Interactive `/commit` assistant command: propose commit message from git diff, stage all changes, and commit after confirmation.
argument-hint: commit
user-invocable: true
---

When the user invokes the `/commit` command, the assistant will:

When the user invokes `/commit`, the assistant performs three distinct phases:

1. Inspect the repository and working tree (sanity + changed files):
   - Run `git status --porcelain --branch` and `git diff --name-only --diff-filter=AM HEAD` to determine branch health and `changed_set`.
   - If a merge/rebase/conflict is in progress, abort and ask the user to resolve it.

2. Run checks (delegated):
   - Rather than relying on a single helper script, `/commit` delegates checks to discrete, focused skills named below:
      - `formatters` — formatting and autofixes.
      - `linters` — linting and non-fixable errors.
      - `dependencies` — dependency vulnerability and outdated-package checks (NVD/OSV/GHSA-aware).
      - `docker-scout` — Dockerfile/image vulnerability scan using `docker scout quickview`.
      - `secret-scan` — secret scanning.
      - `large-files` — large file detection (>5MB).
      - `eslint-provision` — optional ESLint provisioning when JS/TS files changed.
   - Each check skill describes its invocation, exit codes, and when the assistant must pause for confirmation.

      - When the `/commit` orchestrator invokes these checks it MUST call the `checks` skill with `scope=changed` so sub-skills operate only on the computed `changed_set`.

      - The `/commit` orchestrator accepts an optional `--skip-checks` (or `skip_checks`) flag. When provided, the assistant will skip invoking the `checks` skill entirely and proceed directly to commit-message generation and staging. This is intended for advanced workflows where the caller intentionally bypasses automated checks; use with caution.

         NOTE: `checks` here refers to an assistant/skill invocation, not an OS shell program. The assistant should invoke the repository's `checks` skill (for example, via the assistant's skill/capability API), and must not attempt to run a literal `checks` command in the user's shell. Example skill-like invocations (conceptual):

         - `checks --scope=changed` (invoke the checks skill to run all checks against changed files)
         - Or skip checks when invoking `/commit` by passing `--skip-checks`, e.g. `/commit --skip-checks`.
         - `checks --check=linters --scope=changed` (invoke the checks skill to run only linters against changed files)

         If the `checks` skill is not available, the assistant should skip running checks and continue to the commit-message generation and staging steps.

3. Commit message generation and commit execution:
   - The assistant synthesizes a multiline commit message based on the actual content changes (the diff), not simply a list of filenames, and presents it for user confirmation/edit. The assistant MUST inspect the diffs for the computed `changed_set` (for example via `git diff --staged` or `git diff HEAD -- <paths>`) and summarize what was changed and why at a code/content level. The required format is:

      - A concise one-line header (summary) — preferably <= 50 characters.
      - A blank line.
      - An unstructured narrative paragraph (one or more sentences) describing the change in freeform prose. The narrative should explain the reason for the change, what was modified (behaviour, API, tests, docs), and any developer- or user-visible impact. When relevant, mention added or updated tests and migration steps.

   - Guidance for message content:
      - Prefer summarizing the content of diffs: code paths modified, bugs fixed, logic added or removed, and tests added. Do not default to enumerating filenames; only include filenames when they provide helpful context (e.g., "update config loader in `settings.go`").
      - Highlight user-facing changes, breaking changes, or required follow-ups (e.g., run `go mod tidy`, database migrations, or `npm install`).
      - Keep the header atomic and the body explanatory and actionable for other developers.

      Example:

      Fix crash on startup when config missing

      The app attempted to read an unset env var and panicked; add a safe fallback and unit tests to cover the code path. No user-facing impact; developers should run `make test` after pulling.

   - For the final staging/commit step the assistant will:
         - Present the proposed commit message and a summary of the changed files for user confirmation; by default, when the user approves, stage all changes.
         - If the user approves, execute the necessary `git` commands directly in the working tree, for example:

         git add --all
         # Use a heredoc to provide a multiline commit message safely
         git commit --file=- <<'COMMIT_MSG'
         <One-line header>

         <Unstructured narrative describing why the change was made, what changed, and any impact>
         COMMIT_MSG

      - If the user prefers to run commands locally, provide the exact `git` commands to run and do not execute them automatically.

Rules & constraints:
   - Never push to remotes automatically.
   - Each check is a separate skill and must be implemented or provided by the repository owner if the repo requires deterministic automation.
   - The assistant must not conflate multiple checks into a single script without explicit repository policy that mandates it.

See the sibling `checks-*` skills for details on each check's behavior and exit codes.
        - `checks/formatters` — formatting and autofixes.
