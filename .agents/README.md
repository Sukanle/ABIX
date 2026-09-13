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

## Entry points (auto-discovered by tools)

Tooling looks for instructions in fixed locations, so the repository keeps thin
shims that point here — the content lives in this directory:

| Discovered by | File |
|---------------|------|
| Codex / generic agents | [`../AGENTS.md`](../AGENTS.md) |
| Claude Code | [`../CLAUDE.md`](../CLAUDE.md) |
| Gemini | [`../GEMINI.md`](../GEMINI.md) |
| GitHub Copilot | [`../.github/copilot-instructions.md`](../.github/copilot-instructions.md) |
| Copilot path-scoped rules | [`../.github/instructions/`](../.github/instructions/) |

Path-scoped rules stay under `.github/instructions/` because GitHub Copilot
requires that location; keeping a second copy here would let the two drift.

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
