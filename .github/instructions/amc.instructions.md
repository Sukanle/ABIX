---
applyTo: "amc/**"
---

# AMC instructions

Source of truth: [`docs/amc/amc.md`](../../docs/amc/amc.md) and
[`LANGUAGE-PLUGIN.md`](../../.agents/LANGUAGE-PLUGIN.md).

`applyTo` uses the lowercase `amc/` prefix recorded in the git index, which is
what a fresh clone sees; the working tree displays `AMC/` on case-insensitive
filesystems.

- AMC is a language-neutral toolchain; a language-specific behaviour belongs in
  a frontend/plugin, not in shared core code.
- Keep the toolchain layers separate: `amc/src/Core` (model / format / query /
  metadata), `amc/src/Lang` (one provider per language, `C++/`, `Rust/`),
  `amc/src/Tool` (`Dump/`, `MCP/`), `amc/src/CLI`, `amc/src/Util`.
- Every `amc-<language>` executable is a standalone provider speaking the
  JSON-lines protocol from `amc/src/Lang/Lang.cpp`. Adding a language means
  adding a provider, not special-casing the driver.
- CLI failures must use the unified `abix.error/1` envelope; add
  `--error-format` handling for new tools.
- Machine-readable output ships two modes: text for humans and JSON for
  agents/MCP. Keep JSON schemas stable and versioned (`abix.query/1`,
  `abix.metadata/1`, `abix.verify/1`, `abix.error/1`).
- Deterministic output: `amc build` / `amc generate` must be reproducible for
  the same inputs.
- `.abix` / Metadata Region layout changes are **ABI semantic changes** — update
  [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) and add serialization tests.
- Exercise the CLI manually after changes; the integration suite lives in
  `amc/tests/amc_integration_test.py`.
