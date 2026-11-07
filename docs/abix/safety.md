# Safety metadata (v0.3.x decision)

<p align="center">
  English · <a href="safety_zh.md">中文</a>
</p>

> **Status: v0.3.x decision.** Safety metadata is advisory. ABIX records where it lives and which claims are allowed; it does not enforce S1/S2 at runtime.

## Where the metadata lives

- **AMC `.semantic`** is the authoritative document for S1/S2 annotations.
- **ABIX `.abix`** adds no fields in v0.3.x. A reader must accept an artifact without safety metadata, and the annotations never participate in `TypeID`, `LayoutHash` or `ABIHash`.
- **Runtime** does not enforce aliasing, `Send` or `Sync`. Generated language wrappers may use the annotations to choose a checked path.

## Allowed levels

| Level | Covers                            | Allowed claim in v0.3.x             |
| ----- | --------------------------------- | ----------------------------------- |
| S1    | aliasing / exclusivity            | `checked` at most; never `enforced` |
| S2    | `Send` / `Sync`                   | `checked` at most; never `enforced` |
| S3    | region / `Pin` / interface safety | deferred to v0.4.0                  |

`checked` means a frontend or fixture verified the property for a specific interaction. `enforced` means the ABI or runtime mechanically prevents a violation.

The strong constraints in v0.3.x are the release protocol:

- release happens exactly once;
- release goes through the creating module and its allocator;
- the release entry must not unwind.

These are contract requirements. A fixture may check them, but the core ABI does not turn them into a generic runtime safety system.

## Missing metadata

- Absent metadata means `unspecified`.
- A consumer must not infer `borrowed`, `owned`, `Send`, `Sync`, no-alias or thread-safety from the absence of an annotation.
- Adding metadata is additive: it does not change an existing record or a compatibility result.

## Compatibility

Safety metadata is not part of the ABI identity. `amc verify` may report a `safety_changed` drift separately, but it does not fold it into a single "ABI compatible" answer.
