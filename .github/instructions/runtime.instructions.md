---
applyTo: "abix/**"
---

# Runtime instructions

Source of truth: [`ARCHITECTURE.md`](../../.agents/ARCHITECTURE.md) and
[`docs/runtime.md`](../../docs/runtime.md).

* The runtime **consumes** ABI; it must never become a second ABI authority.
  Canonical records come from ABIX IR / the `.abix` artifact.
* Preserve the native fast path. Discovery, verification, binding and
  adaptation happen at initialisation; compatible calls do not re-dispatch.
* Runtime metadata is a **projection** of the ABI model. Do not reconstruct ABI
  semantics independently.
* Do not introduce a VM, generic dispatcher, universal object model, GC, or
  ownership runtime.
* Do not leak synchronization / RCU internals through the public ABI.
* A shared `TypeID` must resolve to the same `LayoutHash`; compatible
  duplicates are deduplicated, conflicts are rejected.
* Name lookup is diagnostic-only; `TypeID` is the identity. Do not make runtime
  behaviour depend on names.
* Performance-sensitive changes (registry lookup, binding, call paths) require a
  benchmark before/after.
