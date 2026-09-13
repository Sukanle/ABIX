---
applyTo: "{abix/abi_model.h,abix/runtime_descriptor.h,amc/core/amc_core.h,amc/core/amc_core.cpp,amc/core/amc_metadata.h,amc/core/amc_metadata.cpp}"
---

# ABI-semantic instructions

These files define ABIX's ABI representation. Changes here are **ABI semantic
changes**.

Read first: [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) and
[`ARCHITECTURE.md`](../../.agents/ARCHITECTURE.md).

* One source of ABI truth. Do not introduce a parallel identity or layout
  representation.
* Keep the four identities distinct: `TypeID` (semantic), `LayoutHash`
  (physical layout), `BuildID` (binary build), `MetadataID` (content).
* Names are **not** ABI identity; they are diagnostic. Never make compatibility
  depend on a name.
* Serialized metadata must stay **pointer-free** (offsets/indices, not
  addresses) and relocation-free.
* `LayoutHash` must be computed from size, alignment, and field identity/offsets
  — not from source spelling.
* Adding/removing/renaming a field, or changing its type, offset, size or
  alignment, is a compatibility event: update the spec and add/adjust a
  regression test.
* Keep hash domains and canonical encoding stable; changing them invalidates
  every existing artifact.
* Update [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) in the same change.
