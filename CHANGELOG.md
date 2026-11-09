# Changelog

All notable changes to ABIX are recorded here. Older releases are available in the GitHub release history.

## v0.4.0 — Breaking ABIX-STD layouts and advanced cross-language contracts (2026-11-09)

### Breaking

- Replaced `abix_optional<T>` with the explicit `{ uint32_t discriminant; uint32_t reserved; alignas(T) T value; }` layout. Only `0` and `1` are valid discriminants; a non-zero `reserved` fails closed; `value` is not read for None.
- Replaced `abix_result<T>` with the explicit `{ int32_t status; uint32_t reserved; alignas(T) T value; }` layout. `status` is a transport status, not the 16-byte `ErrorCode`.
- Replaced `abix_str<CodeUnit>` with `{ const CodeUnit *data; uint64_t len; uint32_t encoding; uint32_t reserved; }`; the former implicit trailing padding is now an explicit validity sentinel.
- Removed the v0.3.x layouts without a compatibility layer or `_v1`/`_v2` aliases. Old `.abix` artifacts and generated bindings must be regenerated with AMC v0.4.0+.
- The package version is now `0.4.0` with `SameMinorVersion` compatibility: `find_package(ABIX 0.4.0)` succeeds, while `0.3.x` and `0.5.x` requests fail.

### Added

- `SKL/ABIX/Interface/OwnerABI.h`: the C-visible canonical `abix_owner_handle` (`object`, `mode`, `flags`, `destroy`, `module_instance`) with 64-bit layout assertions; `skl::abix::owner::handle` is now its C++ alias.
- `SKL/ABIX/STD/OwnedString.h` / `OwnedSpan.h`: `abix_owned_str` and `abix_owned_span<T>` as an existing borrowed view plus an owner, so the view stays zero-copy and the owner is the only release path.
- `cmake/owner_handle.json` and the installed `ABIX_OWNER_HANDLE_JSON`: the canonical 64-bit owner-handle layout for AMC and language adapters.
- `test/conformance/`: one native library consumed by C, C++, Rust and Zig consumers with explicit `#[repr(C)]` / `extern struct` adapters, including 32-bit/64-bit C/C++ layout checks, invalid-discriminant and non-zero-`reserved` rejection, and owned-string/owned-span release.
- `test/std_contract_test.cpp`: explicit v0.4.0 layout assertions for `int32_t`, `int64_t` and pointer instances, the owner handle and owned views, plus v0.3.x optional/result byte-layout negative cases.
- Advanced-contract decisions recorded in the local `memory/` notes: A4-0 gap audit, A4-1 Variant/Range/MapView decision, A4-2 Interface/trait-object mapping, A4-3 tagged union, A4-4 lifetime boundary, A4-5 compatibility and A4-6 conformance.
- `ABIXConfigVersion.cmake` negative tests in `test/cmake_runtime_package_test.py`.

### Changed

- `docs/abix/std.md` / `std_zh.md` now document the v0.4.0 explicit layouts, the owner handle, owned views, the layout matrix and the conformance fixture.
- `ABI-SPEC.md` §9.1 records the explicit ABIX-STD layouts, the canonical owner handle and owned views, §14.2 records the STD layout revision and lockstep matrix, and §16 documents the ABIX-STD validity-sentinel exception to ordinary reserved fields.
- The migration and advanced-contract notes moved out of the published `docs/` tree into the local `memory/` working notes until the 1.0.0 documentation freeze; [`docs/abix/std.md`](docs/abix/std.md) keeps the normative layout summary.

### Compatibility

- Breaking for ABIX-STD consumers, generated bindings and `.abix` artifacts. Regenerate all artifacts with AMC v0.4.0+.
- No `.abix` format version change and no change to `TypeID`, `LayoutHash` or `ABIHash` rules.
- `abix_span`, `abix_callback` and `abix_callback_owned` keep their v0.3.x layouts.
- The owner handle keeps its 32-byte 64-bit layout; only its canonical C spelling (`abix_owner_handle`) and the owned-view containers are new. `skl::abix::owner::handle` remains source-compatible and is now an alias of the C type.
- `Model::Target` / `BinaryImage::Target` are unchanged; AMC `TargetFacts` remains the layout authority.

## v0.3.4 — v0.4 lockstep documentation fix (2026-11-08)

### Fixed

- Correct the v0.4 migration lockstep matrix: AMC v0.3.3 remains on ABIX 0.3.x; AMC v0.4.0+ is the breaking lockstep release.
- Update the regeneration checklist to AMC v0.4.0+.

### Compatibility

- Documentation-only patch. No header, layout, package-version or runtime behavior change.

## v0.3.3 — v0.3.x surface freeze and v0.4.0 migration (2026-11-08)

### Added

- `v0.4-migration.md` and `v0.4-migration_zh.md` (since moved to `memory/`): the breaking STD layout changes, the AMC/ABIX lockstep matrix and the regeneration checklist.
- Deprecation notes in `docs/abix/std.md` and `std_zh.md` for `abix_optional`, `abix_result` and `abix_str`.

### Compatibility

- No layout change in v0.3.3. This is the last additive 0.3.x release before the v0.4.0 breaking update.

## v0.3.2 — STD conformance and language adapter rules (2026-11-08)

### Added

- `docs/abix/std.md` and `std_zh.md`: explicit Rust/Zig adapter rules for `abix_optional`, `abix_result`, `abix_str`, `abix_span`, callbacks and `Owner<T>`.
- `test/std_contract_test.cpp`: conformance checks for `abix_result` status round-tripping, the single-byte optional discriminant and owned-callback defaults, plus invalid-discriminant and incomplete-callback checks that the ABI does not silently normalize.
- `docs/abix/safety.md` and `safety_zh.md`: the S1/S2 metadata decision, its AMC `.semantic` location and the `checked` versus `enforced` boundary.
- `ABI-SPEC.md` §10.2: `TargetFacts` is the layout authority; a missing target is `unverifiable`, never the host.

### Compatibility

- Additive: no existing STD layout, `ErrorCode` layout or `ModuleLease` semantics changed.

## v0.3.1 — Cross-language lease C ABI (2026-11-08)

### Added

- `SKL/ABIX/Interface/LeaseABI.h`: the C ABI contract for cross-language module leases, including the Host-owned function table, registry-scoped `uint64_t` tokens and `abix_status`.
- `abix::to_error_code(abix_status)` and `abix::is_success(abix_status)` mapping to the existing `ErrorCode` / `CallError` / `ModuleStatus` identity.
- `test/lease_abi_test.cpp`: contract tests with a mock Host registry covering token lifetime, duplicate release, registry CLOSING/destroy, draining/busy, independent registries and token overflow.
- `docs/abix/lease.md` and `lease_zh.md`: the normative contract and the ABIX / AMC / plugin responsibility boundary.
- Additive `abix_callback_owned` with an explicit `release`, `flags` and `reserved` field; the borrowed `abix_callback` layout is unchanged.
- `test/std_contract_test.cpp`: portable layout audit for `abix_optional`, `abix_result`, `abix_str`, `abix_span`, `abix_callback`, `abix_callback_owned` and the `Owner<T>` projection across pointer widths and alignments.
- Additive `abix_std::Owner<T>` as a source-level projection of `owner::handle`; it adds no ABI type or layout field.

### Compatibility

- Additive: no existing STD layout, `ErrorCode` layout or `ModuleLease` semantics changed.
- ABIX still does not export a global lease registry; the Host owns the registry and function table.

## v0.3.0 — AMC split, runtime-only ABIX and error ABI

AMC is now an independent repository and package. ABIX keeps the header-only runtime and no longer contains or builds the AMC toolchain, its artifact libraries, Aue, or the LLDB plugin. v0.3.0 also adds the ABIX-side error identity contract.

### Added

- `SKL/ABIX/Error/` with the fixed-layout `skl::abix::ErrorCode` (`{uint64 domain, int32 value, uint32 reserved}`, 16 bytes / 8-byte aligned) and the `success()`, `is_success()`, `is_canonical()` contract.
- ABIX domain allocation under the SKL owner mask: module `0x002`, local domains `0x0001..0x0005` for `call`, `interface`, `module`, `manifest` and `lifecycle`; the allocation table is recorded in `memory/error-abi.md`.
- `constexpr`, allocation-free `to_error_code()` mappings for `CallError`, `InterfaceStatus`, `ModuleStatus` and `ManifestStatus`. Success maps to canonical `{0, 0, 0}`; unknown values keep their numeric identity.
- A frozen Error ABI specification (`memory/error-abi.md` and `memory/error-abi_zh.md`, plus `ABI-SPEC.md` §6.2) covering layout, success, domain allocation, unknown/reserved values, unload lifetime, language mapping and EPCD interoperability.
- `test/error_code_test.cpp` for layout, canonicality, domain values and known/unknown mappings; the v0.2.1 test-only prototype header is removed.
- Explicit error outputs for setup-time DLL operations: `Object::load(path, error)`, `Object::reload(path, error)`, `Object::unload(error)` and `Function::try_resolve()`. They report a `CallError` without reading or writing `last_error()`; the original signatures remain as compatibility wrappers.
- `test/error_flow_test.cpp` for explicit success/failure reporting, unchanged `last_error()` behavior, unload refusal through `stale_handle`, and a saved `ErrorCode` remaining valid after the module object is destroyed.
- Lifecycle coverage in `test/interface_module_test.cpp`: a saved `ErrorCode` survives `ModuleRegistry` unload, a `ModuleLease` blocks unload, and lease acquisition after unload fails closed instead of touching a retired module.
- Optional `test/epcd_compat_test.cpp`, built only when EPCD headers are available: verifies `ErrorCode` size/alignment/offsets, shared domain masks and field-by-field conversion without making EPCD a runtime dependency.

### Changed

- Removed the `AMC/` directory, including its targets, tests, headers, tools, Aue and artifact-library sources. AMC is developed in <https://github.com/Sukanle/AMC> and consumes an installed runtime-only ABIX package; when no package is found it fetches ABIX into its own `dependencies/` directory.
- `AMC::format`, `AMC::abi`, `AMC::metadata` and `AMC::tools` now belong to the AMC package. ABIX no longer exports `ABIX::format/abi/metadata/tools`.
- ABIX package configuration is runtime-only: `ABIX::runtime`, `ABIX::ABIX` and `MICS`; fmt is not a runtime dependency.
- Removed the monorepo-only AMC integration tests. AMC now has standalone core, e2e, package, feature, Rust provider/panic, cancellation, interop and capability-generation tests, plus an independent CMake integration test.
- Aue moved to the AMC repository; AMC-specific tooling is no longer required to build the ABIX runtime and its tests.

### Compatibility

- This is a package-layout breaking change: consumers that need `ABIX::format/abi/metadata/tools` should depend on the AMC package.
- The error ABI is additive: existing `CallError` / `InterfaceStatus` / `ModuleStatus` / `ManifestStatus` values are unchanged, and `last_error()` keeps its thread-local, per-DSO semantics.
- No `.abix` wire-format change.
- ABIX Runtime does not depend on EPCD; interoperability is a field-by-field conversion on the EPCD side.
- ABIX runtime remains no-exception/no-RTTI; AMC keeps its own exception/RTTI policy.

## v0.2.4 — No-exception/no-RTTI runtime build

Build-contract release: ABIX runtime targets are compiled as an explicit-control-flow layer, without C++ exception handling or RTTI metadata.

### Added

- `ABIX_RUNTIME_NO_EXCEPTIONS=ON` and `ABIX_RUNTIME_NO_RTTI=ON` (both default) apply `-fno-exceptions -fno-rtti` (MSVC `/EHs-c- /GR-`) to ABIX runtime tests and DLL plugins.
- The README documents the runtime contract and the AMC exception/RTTI boundary.

### Changed

- ABIX runtime tests/plugins no longer carry exception landing pads or RTTI metadata in their generated objects.
- AMC keeps the compiler defaults: C++ exceptions and RTTI remain enabled for its LLVM/Clang/fmt/toml++ and `try/catch` paths.

### Compatibility

- No public ABI or `.abix` format change.
- `ABIX::runtime` remains header-only and does not force flags on consumers.
- AMC is not part of the no-exception/no-RTTI contract.

## v0.2.3 — Runtime-only package and diagnostics

Packaging and diagnostics release: the header-only runtime and its CMake package are now installable without AMC, LLVM, Clang, fmt or the static ABIX libraries.

### Added

- `ABIX::runtime` and the ABIX package metadata are created and installed outside the AMC/LLVM branch.
- `ABIX_BUILD_RUNTIME_ONLY=ON` skips AMC, tests, benchmarks and DLLs for runtime-only builds.
- `ABIX_INSTALL` controls ABIX install rules; it defaults to ON only for a top-level build.
- `abix-cmake-runtime-package-test` installs a runtime-only package and consumes it with `ABIX::runtime` while fmt is disabled.
- Logging now has an explicit `LogState`, ABIX-atomic sink/level registration, a runtime threshold, and a length-aware `LogRecord` callback with truncation.
- `ABIX_LOG_*_TO(state, ...)` macros route diagnostics through an explicit state for host-owned logging.
- Log configuration uses `SKL/ABIX/Util/Atomic.h` over plain function-pointer and `uint8_t` storage instead of `std::atomic`, avoiding standard-library object-layout coupling across DLL/CRT boundaries.
- `ABIX_LOG_THREAD_SAFE` selects thread-safe configuration (default) or the low-overhead single-threaded/fixed-sink policy; `ThreadSafeLogState` and `SingleThreadedLogState` are also available explicitly.
- Added an optional Quill integration test (`ABIX_ENABLE_QUILL_TEST`).

### Changed

- `ABIXConfig.cmake` requires MICS for the runtime, but only requires fmt when the format/ABI/metadata/tools components or the full aggregate are used.
- `ABIX::ABIX` is always available: it is runtime-only in a runtime-only package and gains the optional libraries in a full build.
- Subproject defaults for tests, benchmarks, AMC, DLL variants and bundled MICS installation are now OFF. `MICS_INSTALL` is no longer forced.
- Global build-type, compiler-option and compile-command settings are limited to top-level builds.
- The logging default threshold is `Warning`; callers that need `Info`/`Debug` must lower it. The legacy `log_sink_t` callback remains source-compatible.
- Log macros no longer rely on the GNU `##__VA_ARGS__` extension.

### Compatibility

- Additive packaging change; no `TypeID`, `.abix` format or public ABI change.
- `find_package(ABIX 0.2 CONFIG REQUIRED)` and `ABIX::runtime` work from the installed prefix without a LLVM/AMC installation.

## v0.2.2 — Built-in type IDs

Additive release: predefined canonical `TypeId` constants for the primitive ABI descriptors. No existing identity or wire format changes.

### Added

- `SKL/ABIX/Model/ABI.h` exposes predefined IDs for void/bool, signed and unsigned integers, pointer-sized `isize`/`usize`, IEEE and extended floating formats, `long double`, and the character kinds.
- `wint_t` resolves to its underlying integer `TypeId`; ABIX does not invent a `prim:wint` descriptor because `wint_t` is a typedef, not a distinct ABI kind.
- `test/builtin_types_test.cpp` checks every predefined ID against `canonical_type_id()` and the host `long double` / character signedness.

### Changed

- Normalised the internal bfloat16 format spelling to `bf16`, matching the canonical `prim:fbf16` descriptor.

### Compatibility

- Additive only: existing `TypeID` / `LayoutHash` / `ABIHash` values are unchanged.
- No `.abix` format or public ABI change.

## v0.2.1 — Maintenance

This is a maintenance release: it fixes existing behavior and adds tests and documentation. It does not change the public ABI or the error protocol.

### Fixed

- `last_error()` is now genuinely thread-local. Its documentation now states that it is per-thread and per-DSO host diagnostic state, not a cross-module error channel.

### Changed

- Existing error-status enums now carry explicit numeric values and stable `to_string()` spellings. Existing numbers are unchanged; this is additive.
- `docs/development/roadmap.md` and the README list the maintenance release.

### Tests

- Added thread-isolation and stable error-string tests.
- Added cross-language error-propagation coverage for success, known failure and an unknown status code.
- Added `[[error]]` negative tests: no contract, unknown function, out-of-range parameters/fields, and unknown status preservation.
- Added a provider-side Rust panic boundary test (caught panic and uncaught panic abort).

### Documentation

- Documented the per-thread / per-DSO scope of `last_error()`.
- Documented that Zig does not project error sets / error unions yet.

### Compatibility

- No `.abix` format change.
- No `TypeID` / `LayoutHash` / `ABIHash` change.
- `[[error]]` v1 and `abix.error/1` are unchanged.
- No public ABI or generated-code semantic change.
