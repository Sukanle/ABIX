# ABIX Agent Knowledge Base

This directory is the home of the ABIX **project constitution and developer
knowledge base** for AI agents (and humans). One authoritative source per
knowledge type; do not duplicate content across these files — link instead.

| File | Answers | Authority |
|------|---------|-----------|
| [`AGENTS.md`](AGENTS.md) | how an agent should work here | behaviour |
| [`ABI-SPEC.md`](ABI-SPEC.md) | what a legal ABIX ABI is | **normative** |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | how the system is shaped; invariants; forbidden patterns | design |
| [`LANGUAGE-PLUGIN.md`](LANGUAGE-PLUGIN.md) | how to add a language | extension |
| [`ROADMAP.md`](ROADMAP.md) | what is in scope now | scope |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | contribution workflow + change types | process |
| [`../docs/`](../docs/) | detailed implementation knowledge | detail |

## Entry point

The canonical entry point is **[`AGENTS.md`](AGENTS.md)** in this directory.
Agents that support `.agents/AGENTS.md` discovery read it directly; there is no
duplicated copy elsewhere.

Platform-mandated files stay where the platform requires and link back here:

| Tool | Location |
|------|----------|
| GitHub Copilot (repository-wide) | [`../.github/copilot-instructions.md`](../.github/copilot-instructions.md) |
| GitHub Copilot (path-scoped) | [`../.github/instructions/`](../.github/instructions/) |

Path-scoped rules stay under `.github/instructions/` because GitHub Copilot
requires that location; keeping a second copy here would let the two drift.

If a tool only discovers instructions at the repository root (for example
`CLAUDE.md` or `GEMINI.md`), add a one-line shim there that imports
`.agents/AGENTS.md` rather than copying its content.

## Skills

Task-specific reusable instructions live in [`skills/`](skills/):

* [`skills/git-commit-style/`](skills/git-commit-style/SKILL.md) — commit
  message conventions.

## Documentation authority

When sources disagree:

```text
ABI-SPEC.md > ARCHITECTURE.md > LANGUAGE-PLUGIN.md > CONTRIBUTING.md
            > ROADMAP.md > docs/ > implementation > comments > assumptions
```

Existing code is evidence of the current state, not the definition of intended
ABI semantics. See [`AGENTS.md`](AGENTS.md) §3.
