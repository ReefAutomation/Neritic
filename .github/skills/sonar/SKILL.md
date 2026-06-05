---
name: sonar
description: Interactive `/sonar` assistant command: run Sonar analysis, query issues or hotspots, and prepare small, deterministic repair suggestions.
argument-hint: sonar
user-invocable: true
---

Purpose
-------
Automate common SonarQube tasks for maintainers via the SonarQube MCP server: query project issues, query Security Hotspots, request targeted file analysis, and prepare reversible, pattern-based repair suggestions for simple problems.

Prerequisites
-------------
- Access to the SonarQube MCP server used by your organization. The skill uses the workspace's MCP integration rather than a local `sonar-scanner`.
- A Sonar user token with appropriate permissions available to the MCP service or configured in the MCP connection. Local `SONAR_TOKEN`/`SONAR_HOST_URL` are optional when the MCP integration already provides credentials.
- The workspace must have the SonarQube MCP tools available to the assistant (MCP operations such as `mcp_sonarqube_search_sonar_issues_in_projects`, `mcp_sonarqube_search_security_hotspots`, `sonarqube_analyze_file`, and `sonarqube_exclude_from_analysis`).

Behavior Overview
-----------------
- `scan` action: asks the SonarQube MCP to run or schedule analysis. When a full server scan is not available, the skill will invoke targeted file analysis via `sonarqube_analyze_file` for modified files.
 - Default interactive policy: the skill operates non-interactively by default and will not prompt the maintainer for decisions during automated flows. Any operation that requires maintainer confirmation must be explicitly requested via flags (for example `--confirm` or an explicit `--interactive` flag).
 - `scan` action: asks the SonarQube MCP to run or schedule analysis. When a full server scan is not available, the skill will invoke targeted file analysis via `sonarqube_analyze_file` for modified files.
- `query` action: uses the MCP `mcp_sonarqube_search_sonar_issues_in_projects` call to list issues for given `projectKeys`, `rules`, or explicit `issue` keys. By default the query uses `resolved=false`.
- `hotspots` action: uses the MCP `mcp_sonarqube_search_security_hotspots` call to list Security Hotspots.
- Fallback behavior: if an issues query returns zero results, the skill will (by default) call the Hotspots MCP call to surface security hotspots that are not represented as regular issues. Pass `--no-hotspots` to disable this fallback.

Commands and Flags
------------------
- `/sonar scan` — request the MCP to run or schedule a server analysis and report outcome; if unsupported, the skill will perform targeted file analysis via `sonarqube_analyze_file` and report results.
- `/sonar scan --include-resolved` — include resolved/closed issues when querying.
- `/sonar query --rule <ruleKey>` — query for unresolved issues matching a rule (e.g., `javascript:S5852`) using `mcp_sonarqube_search_sonar_issues_in_projects`.
 - `/sonar repair --issue <issueKey>` — prepare a suggested, reversible textual patch for the specified issue. The assistant will produce WorkspaceEdit-style patches and will only apply edits when `--apply --confirm` or `--interactive` flags are provided. Uses MCP issue data and `sonarqube_analyze_file` to re-run analysis after edits.
 - `/sonar repair --rule <ruleKey> [--limit N]` — prepare suggested, reversible textual patches for the first `N` unresolved issues matching `ruleKey` (defaults to `N=10`) via MCP queries. The assistant will list targeted issues and propose per-file WorkspaceEdit patches; patches are applied only when `--apply --confirm` (or `--interactive`) are provided.
- `/sonar --hotspots` — explicitly fetch hotspots via the MCP hotspots method (`mcp_sonarqube_search_security_hotspots`).
- `--no-hotspots` — opt out of automatic hotspots fallback when an issues search returns no results.
 - `/sonar coverage [--threshold PERCENT] [--limit N]` — query coverage via MCP, list files below `--threshold` (default 80%), generate conservative test scaffolds for uncovered functions up to `--limit` files.
- `/sonar coverage [--threshold PERCENT] [--limit N]` — query coverage via MCP, list files below `--threshold` (default 80%), and (by default) operate non-interactively to generate WorkspaceEdit-style deterministic unit tests for safely testable code paths. The assistant will not pause for confirmation unless `--interactive` or `--confirm` is provided; without these flags it will skip changes that require human judgment and surface patches for manual review.

What the skill returns
----------------------
- For issues: `component`, `textRange`, `message`, `rule`, `severity`, and `quickFixAvailable` (when the MCP provides it).
- For hotspots: `key`, `component`, `line`, `message`, `status`, and `ruleKey`.

Coverage support
----------------
- Purpose: help maintainers identify low-coverage files and automatically generate real, deterministic unit tests (not scaffolds) for safely testable code paths.
- `/sonar coverage` behavior (autonomous by default):
 	- call `mcp_sonarqube_search_files_by_coverage` to find files with coverage below the provided `--threshold` (default 80%).
 	- for each candidate file (respecting `--limit`), call `mcp_sonarqube_get_file_coverage_details` to obtain uncovered line ranges and branch info.
 	- analyze the source to identify exported or top-level pure functions and deterministic entry points that can be safely unit-tested (avoid complex integration points, external network calls, or native code).
 	- synthesize deterministic test files that import the module, invoke functions with deterministic inputs, and assert explicit expected results or stable structural properties derived from static analysis or documented behavior.
 	- apply generated test files directly (write WorkspaceEdit-style patches into the working tree) and re-run per-file analysis via `sonarqube_analyze_file` (or `analyze_file_list`) to update coverage; repeat iteratively until the workspace coverage reaches the `--threshold` (default 80%) or no further safe, testable targets are found.
 	- the skill will not prompt the maintainer during normal automated coverage generation. If interactive confirmation for ambiguous or non-trivial edits is desired, pass `--interactive` or `--confirm` to enable a single confirmation prompt per logical change. By default the skill writes proposed WorkspaceEdit-style patches into the working tree only when `--apply` is used; otherwise it outputs the patches for manual review.
 	- the skill will NOT create commits, pushes, or pull requests, and it will not suggest creating them automatically; maintainers are expected to review WorkspaceEdit patches and use their normal VCS workflows to commit or open PRs.

Notes on conservative behavior:
	- The assistant will NOT generate snapshot-style or placeholder scaffolding tests by default. When a safe, deterministic assertion cannot be inferred, the assistant will either skip the target or produce a WorkspaceEdit-style refactor patch. Such refactor patches are not applied automatically; they are presented for manual review unless `--interactive --apply` (or `--confirm --apply`) are provided.
	- Small, safe refactors required to enable deterministic testing (for example, extracting a pure helper) will be produced as separate WorkspaceEdit patches and will only be applied when explicit `--apply --confirm` flags are provided.

 Notes and safety rules for test generation
 ----------------------------------------
 - The assistant will only generate deterministic, assertion-based tests that validate explicit behavior using pure inputs and stable outputs. Tests must be real unit tests (no placeholder scaffolds).
 - The assistant will not create snapshot-style tests or one-off scaffolding as an automated outcome. Snapshot tests may be suggested only as a documented manual option in the proposal phase and only with explicit maintainer approval.
 - The skill will not modify production code solely to make it easier to test. When a small, safe refactor is required to enable deterministic testing (for example, extracting a pure helper), the assistant will propose the refactor as a separate patch and request confirmation before applying it.
 - Generated tests are always presented as WorkspaceEdit-style patches for maintainer review; the assistant will never commit, push, or create pull requests on behalf of a maintainer. It will not suggest creating PRs automatically.


Repair Assistance
Repair-by-rule behavior
-----------------------
- When invoked with `--rule`, the skill will:
	- call the MCP issue search (`mcp_sonarqube_search_sonar_issues_in_projects`) to find unresolved issues matching the provided rule for the configured project(s)
	- if the query returns zero results, automatically fetch Security Hotspots via the MCP hotspots method (unless `--no-hotspots`) and present them for manual review
	- for each matching issue (up to `--limit`), attempt a conservative, textual quick-fix template when a deterministic substitution is available
	- skip and report any issue where a safe replacement cannot be determined
	- group suggested edits by file and present a per-file patch summary for interactive confirmation before applying edits

Operational steps when applying patches
--------------------------------------
- Before making edits: the skill will attempt to use the MCP `sonarqube_exclude_from_analysis` or an automatic-analysis toggle API to disable automatic analysis if the MCP exposes it, preventing intermediate events while edits are staged.
 - The skill will prepare WorkspaceEdit-style patches. Patches are only applied when `--apply --confirm` (or `--interactive`) are provided; otherwise patches are presented for manual review.
- After applying patches locally, the skill MUST call the MCP `sonarqube_analyze_file` (or an `analyze_file_list` helper, if available) to re-analyze the modified files.
- Finally, the skill will re-enable automatic analysis via the MCP toggle API if it was disabled.

Notes on Hotspots fallback
-------------------------
- The documented fallback is: when an issues `--rule` query returns no results, the skill will (by default) call the MCP hotspots method and surface any Security Hotspots. Hotspots are presented for manual review only — the skill will not attempt automated fixes for hotspots.

Limitations and Safety
----------------------
- Not a full refactoring tool: no complex program analysis or cross-file semantic refactors.
- Does not push commits or modify remote branches; produces WorkspaceEdit-style patches for maintainer review and will not commit, stage, push, or create pull requests on behalf of the maintainer.
- Hotspots API calls require additional Sonar permissions. If the token lacks hotspot access, the skill will report the error and stop the hotspot step.
- The skill favors conservative, reversible edits. If a safe textual replacement cannot be determined, the skill will surface the finding and recommend a manual review.

No-workaround policy
---------------------
- Repairs prepared or applied by the skill MUST NOT be workaround or fallback code. The assistant will only propose edits that directly address the flagged problem (root-cause fixes) via deterministic, semantics-preserving textual substitutions. The skill will not propose or apply changes that alter program flow to hide, suppress, or sidestep the underlying issue (for example: adding silent `try/catch` wrappers that swallow errors, inserting feature flags or configuration toggles to bypass checks, or replacing logic with alternative behavior that merely avoids the problematic code path).
- If a safe, root-cause textual fix cannot be determined, the skill will skip automated repair and surface the finding for manual review. To allow non-root-cause edits (workarounds) you must explicitly opt in by passing `--allow-workarounds` (not the default).

Fallback-removal policy
------------------------
- When an issue is detected in code that implements a "fallback" behavior (for example, functions or variables with names containing `fallback`, `attemptFallback`, `tryFallback`, or clearly-documented fallback render/compat branches), the skill will attempt to produce a conservative repair that removes or reduces the fallback only when a deterministic, root-cause substitution is available.
- Detection heuristics: the skill will mark an issue as involving a fallback when the MCP issue's `component`/`message` or source context contains common fallback identifiers (e.g. `fallback`, `attemptFallbackRender`, `fallbackMethod`, `Fallback`, `*_fallback`, or explicit comments like "Fallback:"). This heuristic is conservative and used only to classify candidates for fallback-removal suggestions.
- Repair strategy for fallback issues:
	- If the underlying cause can be deterministically fixed (for example, replace a missing validation with an explicit check or remove an unnecessary secondary code path that duplicates but weakens a primary implementation), the skill will propose that root-cause edit as a per-file patch.
	- If the fallback merely masks an upstream failure (for example, a network retry that hides an authentication error), and no safe automated fix can be inferred, the skill will NOT apply a workaround-removal patch. Instead it will surface a human-review suggestion describing the risk of the fallback and recommended remediation steps.
	- The skill will never transform a fallback into a new silent-failure path (e.g., replacing logic with an empty `catch {}`) or insert toggles to bypass checks.
- Opt-out and explicit control: pass `--no-fallback-removal` to disable any automatic attempts to remove fallbacks. To perform non-root-cause edits that intentionally remove a fallback without a deterministic fix, pass `--allow-workarounds` together with an explicit confirmation (not recommended).

Examples
--------
- `/sonar scan` — run analysis and list unresolved issues.
- `/sonar query --rule javascript:S5852` — return unresolved S5852 issues; if none found, automatically query hotspots (unless `--no-hotspots`).
- `/sonar repair --issue ca827621-090b-42a4-be07-cd48f8129493` — prepare a suggested fix for the specified issue and produce a WorkspaceEdit patch. The patch will only be applied when `--apply --confirm` (or `--interactive`) is provided.

Troubleshooting
---------------
- If MCP calls fail, check the MCP connection and that the assistant has access to the SonarQube MCP tools in the workspace. Ensure the MCP service has the necessary Sonar credentials.
- If hotspot calls return 401/403 via the MCP, ensure the token configured in the MCP has Hotspot viewing permissions in SonarQube (user token, not a project token).

Notes for Maintainers
---------------------
- Review patches locally. The skill will not commit, stage, push, or create pull requests, and it will not offer, suggest, or attempt to perform commits or open pull requests. Use your normal workflows to commit or open PRs if desired.
- If you want the skill to always include hotspots in queries, use `--hotspots` explicitly; automatic fallback occurs only when an issues query returns zero results.

Implementation guidance for integrators
--------------------------------------
- The assistant will use the workspace MCP functions when available. Integrators should ensure the MCP exposes these operations to the assistant: `mcp_sonarqube_search_sonar_issues_in_projects`, `mcp_sonarqube_search_security_hotspots`, `sonarqube_analyze_file`, `sonarqube_exclude_from_analysis`, and `sonarqube_analyze_file`/`analyze_file_list` helpers.
 - The assistant will use the workspace MCP functions when available. Integrators should ensure the MCP exposes these operations to the assistant: `mcp_sonarqube_search_sonar_issues_in_projects`, `mcp_sonarqube_search_security_hotspots`, `sonarqube_analyze_file`, `sonarqube_exclude_from_analysis`, and `sonarqube_analyze_file`/`analyze_file_list` helpers.
 - For coverage functionality, integrators should also expose: `mcp_sonarqube_search_files_by_coverage`, `mcp_sonarqube_get_file_coverage_details`, and `mcp_sonarqube_get_component_measures` so the assistant can list low-coverage files and fetch per-file uncovered line ranges required to generate targeted tests.
- If the MCP does not support full-server scan triggering, the skill will fall back to invoking `sonarqube_analyze_file` on the modified files and then re-query issues.

Duplicate-code repair
---------------------
- Purpose: offer conservative, reversible suggestions to reduce textual duplication detected by Sonar's duplication engine using the MCP duplication APIs. This is intended for small, local duplicated helpers or identical blocks that can be safely consolidated without deep semantic analysis.
Command: `/sonar repair-duplicates [--min-lines N] [--limit M] [--consolidate-module <path>]`
	- `--min-lines N` — only consider duplicated blocks of at least N lines (default: 8).
	- `--limit M` — limit the number of duplicate locations processed (default: 20).
	- `--consolidate-module <path>` — optional path (relative to project root) where the assistant should extract shared helpers. If omitted, the assistant will select a sensible consolidation location.

How it works (MCP + AI)
	- The assistant will call the MCP duplication API (`mcp_sonarqube_search_duplicated_files`) to discover duplicated files and duplicated blocks. The MCP returns authoritative per-block locations (component file path plus start/end line numbers) which the assistant uses as repair targets.
	- For each duplicated-block candidate (up to `--limit`), the assistant will:
		- Fetch the exact source fragment for each reported location using the MCP-provided start/end lines and surrounding context.
		- Normalize formatting and verify the fragments are semantically equivalent under conservative heuristics (whitespace/formatting differences allowed, but differing free-variable usage or statements that reference distinct local scope will disqualify automatic repair).
		- Use the assistant's AI code-generation capability to synthesize a WorkspaceEdit-style patch that extracts the duplicated logic into the chosen consolidation module and replaces each occurrence with an import and thin delegating wrapper where needed. The AI input includes the original fragments, filenames, surrounding context, and a strict instruction to produce behavior-preserving, minimal edits only.
		- Validate the generated patch syntactically (basic parsing) and skip the candidate if validation fails or produces uncertainty markers.
	- The assistant will automatically apply validated, conservative patches to the working tree (no interactive dry-run or manual confirmation) and then call `sonarqube_analyze_file` (or `analyze_file_list`) to re-analyze modified files.

Safety and constraints
	- Duplicate-code repair is intentionally conservative: it avoids changing program behavior, will not inline or reorder code in ways that change execution, and will not produce non-deterministic runtime changes.
	- The assistant will not fix duplication by adding fallback or workaround logic; it will perform only root-cause consolidations or skip automation when unsafe.
	- Because duplication findings have no rule key, the assistant treats duplication as a separate repair category and relies on MCP block locations plus conservative AI synthesis rather than rule-based quick-fixes.
