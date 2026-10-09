---
applyTo: "{abix/model/*.h,abix/Metadata/*.h,amc/include/AMC/Core/*.h,amc/src/Core/*.cpp}"
---

# ABI-semantic instructions

These files define ABIX's ABI representation: the IR record definitions and
hashing in `abix/model/`, the metadata projection in `abix/Metadata/`, and the
`.abix` reader/writer plus metadata region in `amc/src/Core/` and
`amc/include/AMC/Core/`. Changes here are **ABI semantic changes**.

Globs use the lowercase `abix/` / `amc/` prefixes recorded in the git index,
which is what a fresh clone sees. The working tree displays `ABIX/` / `AMC/` on
case-insensitive filesystems; match the index spelling so the rules also apply on
case-sensitive systems.

Read first: [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) and
[`ARCHITECTURE.md`](../../.agents/ARCHITECTURE.md).

- One source of ABI truth. Do not introduce a parallel identity or layout
  representation.
- Keep the four identities distinct: `TypeID` (semantic), `LayoutHash`
  (physical layout), `BuildID` (binary build), `MetadataID` (content).
- Names are **not** ABI identity; they are diagnostic. Never make compatibility
  depend on a name.
- Serialized metadata must stay **pointer-free** (offsets/indices, not
  addresses) and relocation-free.
- `LayoutHash` must be computed from size, alignment, and field identity/offsets
  — not from source spelling.
- Adding/removing/renaming a field, or changing its type, offset, size or
  alignment, is a compatibility event: update the spec and add/adjust a
  regression test.
- Keep hash domains and canonical encoding stable; changing them invalidates
  every existing artifact.
- Update [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) in the same change.
