# Self-Hosting

ABIX 1.0 is self-hosting for its own public ABI: it describes its public ABI
with its own model, and uses that description to build and verify later
versions.

```text
             ABIX 1.0
                 │
                 ▼
          Describe itself
                 │
                 ▼
          Verify / Bind
                 │
                 ▼
           Build next ABI
                 │
                 ▼
             ABIX 2.x ──► describes itself
```

## What self-hosting means here

Self-hosting does **not** mean the implementation is frozen. It separates:

```text
Bootstrap ABI
     ├── stable public contract
     └── ABI evolution rules

Internal Implementation
     ├── runtime internals
     ├── data structures
     ├── synchronization
     ├── caches
     └── implementation details
```

The public ABI is explicit and verifiable; internals remain free to evolve.

## Bootstrap artifacts

ABIX describes its own core IR through its own configuration and validates the
result:

* `abix/self/abix_self.abic.toml` — the ABIX runtime's public types
* `amc/self.abic.toml` — the AMC core IR
* `abix/self_types.cpp`, `amc/self_types.cpp` — the inputs described

The integration suite builds these artifacts, validates them, generates a
native projection and compiles a consumer that uses
`RuntimeRegistry::type_of<T>()` — proving the bootstrap loop is intact.

## Why it matters

Self-hosting shows that the model is expressive enough to describe the system
that implements it. It also gives the project a stable contract to evolve
against, rather than an implicit ABI that changes whenever internals change.

[`bootstrap.md`](bootstrap.md) documents the bootstrap model, its phases and
the milestone history.
