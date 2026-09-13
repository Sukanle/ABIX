---
applyTo: "amc/**"
---

# AMC instructions

Source of truth: [`docs/amc.md`](../../docs/amc.md) and
[`LANGUAGE-PLUGIN.md`](../../.agents/LANGUAGE-PLUGIN.md).

* AMC is a language-neutral toolchain; a language-specific behaviour belongs in
  a frontend/plugin, not in shared core code.
* Keep the toolchain layers separate: `amc/core` (model / format / query),
  `amc/cpp` (Clang frontend + backend), `amc/dump`, `amc/mcp`.
* CLI failures must use the unified `abix.error/1` envelope; add
  `--error-format` handling for new tools.
* Machine-readable output ships two modes: text for humans and JSON for
  agents/MCP. Keep JSON schemas stable and versioned (`abix.query/1`,
  `abix.metadata/1`, `abix.verify/1`, `abix.error/1`).
* Deterministic output: `amc build` / `amc generate` must be reproducible for
  the same inputs.
* `.abix` / Metadata Region layout changes are **ABI semantic changes** — update
  [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) and add serialization tests.
* Exercise the CLI manually after changes; the integration suite lives in
  `amc/tests/amc_integration_test.py`.
