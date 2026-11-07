# Cross-language lease C ABI (v0.3.x contract)

<p align="center">
  English · <a href="lease_zh.md">中文</a>
</p>

> **Status: contract frozen for ABIX v0.3.x; ABIX header + mock-Host test implemented; AMC reference Host and C++/Rust/Zig interop verified on Linux (test matrix 1–10).** This document defines the ABIX-side C ABI contract for cross-language module leases. ABIX ships the C types and status mapping in `include/SKL/ABIX/Interface/LeaseABI.h` and a mock-Host contract test in `test/lease_abi_test.cpp`. The Host (for example AMC) provides the function-table implementation and the registry. ABIX does not export global lease symbols and does not depend on AMC.
>
> The plan and decision log are in [`memory/plan-v0.3.x.md`](../../memory/plan-v0.3.x.md) A3X-0 #2.

## Purpose

A Rust or Zig consumer can receive an object created by an ABIX module. The object's `destroy` entry point lives in that module. If the module unloads while the consumer still owns the object, calling `destroy` becomes a use-after-unload.

The lease C ABI solves this by giving every cross-language owned object a module-lifecycle credential. It is not a second ownership system: it is the C ABI entry point to the existing `ModuleControl` / `ModuleLease` lifecycle.

## Invariants

- The registry is owned by the Host, not by ABIX and not by a plugin.
- ABIX defines the C types and the contract; the Host provides the function table.
- A lease is an opaque `uint64_t` token scoped to one registry; `0` is invalid.
- The token is not a pointer and must never be interpreted as one.
- The lease reuses the existing `ModuleControl` lease count; ABIX does not add a second refcount.
- The module's `destroy` code must remain mapped until the lease is released.
- The lifetime order is `acquire -> create -> destroy -> release`; releasing before destroy is forbidden.
- A token copy does not create a new lease. Rust wrappers must not implement `Clone`; Zig wrappers use a struct with `deinit()`.
- ABIX Runtime does not expose `ModuleControl` or `ModuleLease` memory layouts to Rust or Zig.
- All callbacks are C ABI and must not unwind: a C++ exception or Rust panic must not cross this boundary.

## Types

```c
typedef struct abix_lease_registry abix_lease_registry;
typedef uint64_t abix_module_instance;
typedef uint64_t abix_lease;

#define ABIX_LEASE_INVALID UINT64_C(0)

typedef enum abix_status {
    ABIX_SUCCESS = 0,
    ABIX_ERROR_INVALID = 1,
    ABIX_ERROR_NOT_FOUND = 2,
    ABIX_ERROR_STALE_HANDLE = 3,
    ABIX_ERROR_BUSY = 4,
    ABIX_ERROR_UNLOADING = 5
} abix_status;
```

The numeric values are part of the C ABI and must not be renumbered.

## Host function table

```c
typedef struct abix_lease_api {
    uint32_t abi_version;   // currently 1
    uint32_t struct_size;   // sizeof(abix_lease_api)

    void *host_context;

    abix_status (*registry_create)(
        void *host_context,
        abix_lease_registry **out_registry);

    abix_status (*registry_destroy)(
        void *host_context,
        abix_lease_registry *registry);

    abix_status (*acquire)(
        void *host_context,
        abix_lease_registry *registry,
        abix_module_instance module,
        abix_lease *out_lease);

    void (*release)(
        void *host_context,
        abix_lease_registry *registry,
        abix_lease lease);
} abix_lease_api;
```

Rules:

- ABIX headers define this structure; the Host fills it and owns the implementation.
- Rust/Zig receive `abix_lease_api *` and `abix_lease_registry *` from the Host; they do not create or destroy the Host's registry.
- `abi_version` must be checked; an unsupported version must be rejected.
- `struct_size` allows additive fields in a later ABI version. A smaller table must be rejected, not partially read.
- Every function pointer is non-null for a valid table.
- The Host API object must outlive every registry and every owner created through it.
- `registry_create` / `registry_destroy` are Host operations. Rust/Zig only use `acquire` / `release`.

## Status mapping

`abix_status` is the C ABI result; it maps explicitly to the existing ABIX error identity. It is not a new error domain.

| C status | ABIX mapping |
| --- | --- |
| `ABIX_SUCCESS` | canonical `success()` |
| `ABIX_ERROR_INVALID` | `CallError::invalid` (`call_domain`) |
| `ABIX_ERROR_NOT_FOUND` | `ModuleStatus::not_registered` (`module_domain`) |
| `ABIX_ERROR_STALE_HANDLE` | `CallError::stale_handle` (`call_domain`) |
| `ABIX_ERROR_BUSY` | `ModuleStatus::busy` (`module_domain`) |
| `ABIX_ERROR_UNLOADING` | `CallError::unloading` (`call_domain`) |

Unknown C status values are failures. A mapping helper must preserve the numeric value with a valid failure domain; it must not fold an unknown status into success.

The C ABI status values are stable. Adding a status requires a new `abi_version` or a documented additive value that older readers treat as failure.

## Registry lifecycle

```text
ACTIVE
  |
  | destroy
  v
CLOSING
  +-- outstanding leases > 0  -> ABIX_ERROR_BUSY; registry stays CLOSING
  +-- outstanding leases == 0 -> DESTROYED
```

Rules:

- `registry_create` returns an opaque registry handle; `0`/null is invalid.
- `acquire` and `release` are thread-safe.
- `CLOSING` rejects new `acquire` calls with `ABIX_ERROR_UNLOADING` or `ABIX_ERROR_BUSY`; existing leases may still be released.
- `registry_destroy` returns `ABIX_ERROR_BUSY` while leases are outstanding. The registry must not be freed in that case.
- After successful destroy, the registry handle is invalid and must not be used.
- A registry-scoped token may have the same numeric value in two different registries; the registry is part of the lease identity.

## Acquire and release

`acquire`:

1. Validate the registry and the module instance.
2. Confirm the module is still in a state that allows a new lease.
3. Increase the existing `ModuleControl` lease count through the existing lease-acquisition path.
4. Allocate a new token for this registry.
5. Store `token -> lease record`.
6. Write the token to `out_lease` and return `ABIX_SUCCESS`.

On failure, `out_lease` is `ABIX_LEASE_INVALID`; no partial lease is left behind.

`release`:

- `ABIX_LEASE_INVALID`, unknown token and already-released token are no-ops.
- A concurrent release of the same token decrements the count at most once.
- The token is not reused within the registry lifetime; token overflow rejects new acquires rather than wrapping.
- Releasing a token from a different registry is a no-op.
- The registry is required to be valid; calling `release` after `registry_destroy` is a contract violation.

## Module lifecycle and unregister

The existing state machine is preserved:

```text
REGISTERED
  |
  | unregister_module()
  v
DRAINING
  +-- new lease: rejected
  +-- existing lease: valid
  +-- existing owner: destroy allowed
  |
  | outstanding leases > 0
  v
BUSY (module stays DRAINING)
  |
  | destroy object, release lease
  v
DRAINING
  |
  | unregister_module() retry
  v
UNLOADING -> UNLOADED
```

Rules:

- `unregister_module` begins draining before returning `busy`; this is the existing ABIX behavior and is intentionally retained.
- `busy` is not side-effect free: it closes the door to new leases and lets existing leases drain.
- A failed `unregister_module` must be retried after the last lease is released.
- Acquiring and unloading must use the same synchronization protocol. The check of module state and the increment of the lease count must be one atomic decision; a successful acquire must never be followed by an unload.
- Draining does not block `destroy` for an existing owner; the destructor is part of draining, not a new call that acquires a lease.

## Owner lifecycle

```text
Acquire Lease
  -> Create Object
  -> Rust / Zig owns object
  -> Destroy Object
  -> Release Lease
```

- Acquire must happen before the object is handed to the consumer; there must be no create/acquire window.
- If creation fails, release the lease immediately.
- `owner::handle` remains the module/ownership identity used for diagnosis and mapping.
- `owner::ref` (C++) holds the same semantic lease internally.
- Rust/Zig wrappers hold `object + destroy + lease` and release in `Drop` / `deinit()`.
- `destroy` must have a no-unwind contract.

## Responsibility boundary

| Layer | Responsibility |
| --- | --- |
| ABIX | C ABI types, status mapping, C++ owner integration, contract tests |
| Host (AMC) | `abix_lease_api` implementation, registry, module table, Rust/Zig fixture |
| Plugin | Provides `destroy`; never implements lease release |
| Rust/Zig wrapper | Holds object and lease; destroy then release; no `Clone` on the lease |

ABIX must not depend on AMC implementation details. AMC must not expose `ModuleControl` or `ModuleLease` layouts to Rust/Zig.

## Test matrix

1. Rust creates an object; Host `unregister_module` returns `busy`.
2. After draining, a new `acquire` fails.
3. Rust `Drop` can still call `destroy` while draining.
4. After `Drop` releases the lease, `unregister_module` succeeds.
5. Zig-created object can be taken over by Rust with correct destroy/release order.
6. Two DSOs acquire/release through one registry.
7. Two registries with the same numeric token do not affect each other.
8. Concurrent duplicate release of one token decrements at most once.
9. `registry_destroy` with live leases returns `busy`.
10. Acquire/unregister stress never produces a successful acquire after unload.

## Implementation status

| Layer | Status |
| --- | --- |
| ABIX C ABI header | `include/SKL/ABIX/Interface/LeaseABI.h` |
| ABIX status mapping | `abix::to_error_code(abix_status)` |
| ABIX contract test | `test/lease_abi_test.cpp` with a mock Host registry |
| Host registry / function table | AMC reference implementation in `tests/interop/lease/`; Linux interop verified |
| Rust/Zig owned-object wrappers | Implemented; Linux interop verified |
| Plugin `destroy` integration | Implemented in the reference fixture; follows the existing owner ABI |
| Cross-platform verification | Pending macOS/Windows |
| Zig→Rust takeover fixture | Implemented; Linux interop verified |

The ABIX test uses a mock Host to validate the contract. It does not become a
production registry and does not export global symbols.

## Versioning

- The contract is additive in v0.3.x; it does not change existing STD layouts.
- `abi_version = 1` for the initial table; `struct_size` guards additive fields.
- Unknown status values fail closed.
- ABIX v0.4.0 STD layout changes are unrelated to this contract and are documented in [`plan-v0.4.0.md`](../../memory/plan-v0.4.0.md).

## Non-goals

- A global process-wide lease registry owned by ABIX.
- Exposing `ModuleControl` / `ModuleLease` layouts to other languages.
- A second reference-counting system.
- Cross-layout compatibility with the old STD types.
- A lease implementation inside the plugin being unloaded.
