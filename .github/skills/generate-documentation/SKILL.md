---
name: generate-documentation
description: Procedure for an agent to analyze the repository code and produce a documentation-ready report describing how map generation is implemented.
---

# Codebase Analysis — Generate Map-Generation Report

Purpose
This SKILL is a procedural instruction for an automation agent to analyze the repository source code and produce a documentation-ready report that explains how map generation is implemented. The SKILL itself contains no analysis results, formulas, or code excerpts — it only prescribes the steps the agent must run and the artifacts it must produce.

Scope

- Repository-scoped: the agent will analyze the workspace source files, tests, and build configuration to locate map-generation implementation details.
- Focus: discover entrypoints, configuration sources, generation steps, debug/export hooks, test coverage, and environment assumptions.

Audience

- Automation agents and engineers who will run the analysis and produce human-facing documentation.

---

## Agent responsibilities (high level)

- Perform a static inspection of the codebase to discover how map generation is implemented.
- Produce machine-readable artifacts that capture the analysis (file lists, call graph, settings schema, run-step checklist).
- Produce a short human-facing report `analysis/report.md` that summarizes findings and points to artifacts and files to inspect further.

---

## Required agent actions (step-by-step)

1. Workspace scan

- Enumerate source files, tests, and build files.
- Identify files that reference map-generation-related keywords (e.g., "map", "generate", "settings", "seed", "world", "graph", "render", "export").

2. Locate entrypoints

- Find the programmatic entrypoints and public API surfaces that trigger map generation (main methods, CLI runners, Gradle tasks, or test fixtures).
- Record the file path, symbol name or script, and the minimal invocation needed to execute generation (no execution yet — only discovery).

3. Discover configuration and presets

- Identify configuration sources (settings files, serialized settings, presets, CLI flags, or configuration classes).
- Extract the available presets or example settings file paths and list them.

4. Identify data flow and outputs

- Determine where the generator produces outputs (image files, CSV/JSON debug exports, logs) and which output formats are supported.
- Record filenames, directories, and any configurable output paths.

5. Find debug and test hooks

- Locate existing tests, debug dump code, or developer helpers that already export intermediate artifacts.
- Record how tests invoke the generator and what assertions they perform.

6. Produce a call graph and dependency map

- Build a lightweight call graph of modules/functions/methods involved in generation (coarse-grained: filename → calls → files). Export as `analysis/callgraph.dot` and `analysis/callgraph.csv`.

7. Summarize algorithms and responsibilities (discovery only)

- For each major step discovered (site generation, topology creation, elevation computation, water classification, rivers, biomes, rendering), list the source locations (files/paths) that implement or orchestrate that step. Do not include formulas or implementation details in the SKILL; record file references only.

8. Environment and determinism

- Record build and runtime configuration (build tool, JDK version if present in CI config, wrapper scripts). Identify where RNG seeds are set or passed through configuration files or code.

9. Produce artifacts

- Write the following artifacts into `analysis/`:
  - `file-list.csv` — list of files inspected with short tags (e.g., "entrypoint", "config", "rendering", "test").
  - `callgraph.dot` and `callgraph.csv` — coarse call graph of generation flow.
  - `settings-sources.md` — list of example settings files or presets found.
  - `outputs.md` — list of known output types and paths.
  - `report.md` — short human-facing summary with findings, recommended follow-ups, and exact file links for deeper inspection.
- Log runtime metadata and the agent's discovery steps to `analysis/run.log`.

10. Exit status and validation

- Exit with zero when artifacts are produced successfully.
- If the agent cannot locate any plausible generator entrypoint, write a diagnostic note to `analysis/run.log` and exit non-zero.

---

## Reporting format and constraints

- Filenames and paths in artifacts must be workspace-relative.
- The `report.md` should be concise (<= 80 lines) and contain:
  - Discovered entrypoints and how to invoke them.
  - Locations of settings/presets.
  - Where debug/test hooks exist.
  - Recommended next steps to produce documentation (e.g., run generator with a sample preset, add CI task to export CSVs).

---

## Deliverables from the agent (when run)

- `analysis/file-list.csv`
- `analysis/callgraph.dot`
- `analysis/callgraph.csv`
- `analysis/settings-sources.md`
- `analysis/outputs.md`
- `analysis/report.md`
- `analysis/run.log`

---

## Questions for the user

- Confirm preferred artifact formats (CSV + DOT + Markdown are default). OK?
- Should the agent run the generator as part of this analysis or only perform static code analysis? (default: static-only unless you request execution).

\*\*\* End SKILL
