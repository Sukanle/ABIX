---
applyTo: "**/*.{cpp,h,hpp,cc,cxx}"
---

# C++ instructions

* Target **C++17** (see `CMakeLists.txt`); do not raise the standard as a side
  effect of a feature.
* Match the surrounding style; run `clang-format` on changed files
  (`.clang-format` at the repository root).
* Keep headers self-contained and minimal; prefer forward declarations over new
  includes where practical.
* `noexcept` on hot paths and small value types, matching existing code.
* Do not introduce exceptions into expected-failure paths; follow the
  surrounding subsystem's error model.
* Avoid new third-party dependencies unless justified in the change
  description.
* Fixed-width integers (`stdint.h` / `cstdint`) for anything that crosses an ABI
  or serialization boundary.
* Preserve the native fast path: do not add per-call work that could be done at
  binding time or generated at build time.
