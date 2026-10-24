# Changelog

All notable changes to ABIX are recorded here. Older releases are available in
the GitHub release history.

## v0.2.1 — Maintenance

This is a maintenance release: it fixes existing behavior and adds tests and
documentation. It does not change the public ABI or the error protocol.

### Fixed

- `last_error()` is now genuinely thread-local. Its documentation now states
  that it is per-thread and per-DSO host diagnostic state, not a cross-module
  error channel.

### Changed

- Existing error-status enums now carry explicit numeric values and stable
  `to_string()` spellings. Existing numbers are unchanged; this is additive.
- `docs/development/roadmap.md` and the README list the maintenance release.

### Tests

- Added thread-isolation and stable error-string tests.
- Added cross-language error-propagation coverage for success, known failure and
  an unknown status code.
- Added `[[error]]` negative tests: no contract, unknown function, out-of-range
  parameters/fields, and unknown status preservation.
- Added a provider-side Rust panic boundary test (caught panic and uncaught
  panic abort).

### Documentation

- Documented the per-thread / per-DSO scope of `last_error()`.
- Documented that Zig does not project error sets / error unions yet.

### Compatibility

- No `.abix` format change.
- No `TypeID` / `LayoutHash` / `ABIHash` change.
- `[[error]]` v1 and `abix.error/1` are unchanged.
- No public ABI or generated-code semantic change.
