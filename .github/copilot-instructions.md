# ABIX — GitHub Copilot Instructions

The ABIX agent knowledge base lives in [`.agents/`](../.agents/). For
repository-wide guidance, follow the project constitution there:

* [`.agents/AGENTS.md`](../.agents/AGENTS.md) — principles, change
  classification, documentation authority, completion checklist
* [`.agents/ABI-SPEC.md`](../.agents/ABI-SPEC.md) — normative ABI rules
* [`.agents/ARCHITECTURE.md`](../.agents/ARCHITECTURE.md) — invariants and
  forbidden patterns

Do not duplicate those rules here; this file only adds GitHub-specific advice.

## GitHub-specific guidance

* Keep pull requests focused and small enough to review.
* Reference the relevant spec/architecture section in the PR description.
* Do not claim tests passed unless they were actually run; paste the command
  and result.
* For ABI changes, include compatibility analysis and a regression test.
* For AMC/CLI changes, mention the commands you exercised.
* Prefer updating `docs/` (and the matching `_zh` edition) over adding prose to
  code comments.

## Path-scoped rules

More specific instructions live in
[`.github/instructions/`](instructions/): `cpp`, `runtime`, `amc`, `abi`, and
`docs` instruction files are applied by path.
