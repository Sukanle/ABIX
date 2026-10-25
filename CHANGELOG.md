# Changelog

All notable changes to ABIX are recorded here. Older releases are available in
the GitHub release history.

## v0.2.3 — Runtime-only package

Packaging release: the header-only runtime and its CMake package are now
installable without AMC, LLVM, Clang, fmt or the static ABIX libraries.

### Added

- `ABIX::runtime` and the ABIX package metadata are created and installed
  outside the AMC/LLVM branch.
- `ABIX_BUILD_RUNTIME_ONLY=ON` skips AMC, tests, benchmarks and DLLs for
  runtime-only builds.
- `ABIX_INSTALL` controls ABIX install rules; it defaults to ON only for a
  top-level build.
- `abix-cmake-runtime-package-test` installs a runtime-only package and
  consumes it with `ABIX::runtime` while fmt is disabled.
- Logging now has an explicit `LogState`, atomic sink/level registration, a
  runtime threshold, and a length-aware `LogRecord` callback with truncation.
- `ABIX_LOG_*_TO(state, ...)` macros route diagnostics through an explicit
  state for host-owned logging.

### Changed

- `ABIXConfig.cmake` requires MICS for the runtime, but only requires fmt when
  the format/ABI/metadata/tools components or the full aggregate are used.
- `ABIX::ABIX` is always available: it is runtime-only in a runtime-only
  package and gains the optional libraries in a full build.
- Subproject defaults for tests, benchmarks, AMC, DLL variants and bundled MICS
  installation are now OFF. `MICS_INSTALL` is no longer forced.
- Global build-type, compiler-option and compile-command settings are limited
  to top-level builds.
- The logging default threshold is `Warning`; callers that need `Info`/`Debug`
  must lower it. The legacy `log_sink_t` callback remains source-compatible.
- Log macros no longer rely on the GNU `##__VA_ARGS__` extension.

### Compatibility

- Additive packaging change; no `TypeID`, `.abix` format or public ABI change.
- `find_package(ABIX 0.2 CONFIG REQUIRED)` and `ABIX::runtime` work from the
  installed prefix without a LLVM/AMC installation.

## v0.2.2 — Built-in type IDs

Additive release: predefined canonical `TypeId` constants for the primitive ABI
descriptors. No existing identity or wire format changes.

### Added

- `SKL/ABIX/Model/ABI.h` exposes predefined IDs for void/bool, signed and
  unsigned integers, pointer-sized `isize`/`usize`, IEEE and extended floating
  formats, `long double`, and the character kinds.
- `wint_t` resolves to its underlying integer `TypeId`; ABIX does not invent a
  `prim:wint` descriptor because `wint_t` is a typedef, not a distinct ABI kind.
- `test/builtin_types_test.cpp` checks every predefined ID against
  `canonical_type_id()` and the host `long double` / character signedness.

### Changed

- Normalised the internal bfloat16 format spelling to `bf16`, matching the
  canonical `prim:fbf16` descriptor.

### Compatibility

- Additive only: existing `TypeID` / `LayoutHash` / `ABIHash` values are
  unchanged.
- No `.abix` format or public ABI change.

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
