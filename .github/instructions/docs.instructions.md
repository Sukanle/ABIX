---
applyTo: "{docs/**,**/*.md}"
---

# Documentation instructions

* The README is a project entry point: **What / Why / Demo / Architecture /
  Where next**. Do not turn it into a design paper; move detail into `docs/`.
* One authoritative source per knowledge type:
  * `.agents/ABI-SPEC.md` — what a legal ABIX ABI is
  * `.agents/ARCHITECTURE.md` — design map and invariants
  * `.agents/LANGUAGE-PLUGIN.md` — extension contract
  * `.agents/AGENTS.md` — how an agent should work
  * `.agents/ROADMAP.md` — what is in scope now
  * `docs/` — detailed implementation knowledge
  Do not duplicate content across them; link instead.
* The repository is bilingual: most documents have a `_zh` edition. Update the
  matching edition when you change user-visible content.
* Keep code samples runnable against the current CLI; do not document commands
  that do not exist.
* Prefer concrete examples over abstract prose.
* Do not claim behaviour the implementation does not have (especially
  performance numbers or unsupported platforms).
