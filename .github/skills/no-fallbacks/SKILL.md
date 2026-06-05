---
name: no-fallbacks
description: Global rule for AI agents: do not introduce ad-hoc fallbacks, heuristics, or workaround patches in code or decisions. Require explicit human approval or server-side fixes instead.
user-invocable: true
---

# No-Fallbacks — Global Agent Rule

This skill defines a global, repository-wide rule for any AI agent (Copilot, CLI agents, cloud agents, etc.) making decisions, edits, or suggestions related to this codebase.

### Applicability

- Applies to any code changes, automation, or AI-driven decision affecting this repository — not limited to any single directory or feature. Agents should load and follow this skill when acting on the project.

### Core Rule

- Do NOT introduce implicit fallbacks, heuristic "workarounds", or silent data synthesis in code or automated actions. Always prefer a root-cause fix (server-side validation, stricter contracts, or explicit UX affordances) and obtain explicit human approval before adding any heuristic behavior.

### Required behavior for agents

1. Use canonical data as authoritative

-- If canonical fields are provided by a server API, treat them as authoritative. Do not synthesize alternate values on the client as a silent fallback.

2. No silent heuristics or data synthesis

- Never implement code that guesses, coerces, or fills in missing fields without explicit user instruction.

3. Fail fast and notify humans

- If required canonical data is missing or malformed, the agent should (a) avoid making heuristic changes, and (b) surface the issue to the developer by opening an issue, adding a PR comment, or prompting the user with explicit options.

4. Require consent for fallbacks

- If a user explicitly requests a fallback, the agent may implement it only after documenting the heuristic, adding tests, and providing a user-configurable toggle to opt out.

5. Idempotent and deterministic appliers

- Implement appliers and mapping code to be idempotent and deterministic; prefer stable comparisons against current state rather than transient signatures or heuristics.

6. No silent overwrites of user state

-- Do not overwrite user-edited local state unless the server provides a canonical replacement and the user explicitly requested the replacement.

### Operational requirements

- Agents MUST reference this skill before making changes that could introduce fallback logic.
- When the agent proposes a change that relaxes strictness or introduces heuristics, it must include an explicit call-to-action for human approval in the PR description.



### Notes for maintainers

- This is an instruction-only skill. To change the repository policy, update this file and discuss in code review.
