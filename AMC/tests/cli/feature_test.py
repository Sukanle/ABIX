#!/usr/bin/env python3
"""AMC feature-set test: parsing, IR generation, native binding, IR reuse and
front/back separation.

Usage:
    feature_test.py <amc_bin_dir> [source_root]

Unlike the interop suites this does not require an installed Rust/Zig native
toolchain: the frontends (`amc-cpp`, `amc-rust`) parse their language directly
and every backend projects the same `.abix`. It is cross-platform and also runs
on Windows (where it uses `amc.exe`).

Aspects:
    A. front/back separation  - language providers, --describe-language, IPC
    B. code parsing           - amc build (C++ and Rust frontends) -> .abix
    C. IR generation          - validate / inspect / query / dump the artifact
    D. native language binding- generate cpp/zig/rust/lua; compile the C++ one
    E. IR reuse               - one artifact drives every backend
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# `--keep-artifacts`, or KEEP_ARTIFACTS=1 in the environment (set by the CMake
# option SKL_ABIX_KEEP_TEST_ARTIFACTS), retains the temporary working directory
# instead of deleting it at the end of the run.
if "--keep-artifacts" in sys.argv:
    os.environ["KEEP_ARTIFACTS"] = "1"
KEEP_ARTIFACTS = os.environ.get("KEEP_ARTIFACTS", "") == "1"


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


class Report:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.errors: list[str] = []

    def ok(self, msg: str) -> None:
        self.passed += 1
        print(f"  [PASS] {msg}")

    def fail(self, msg: str, detail: str = "") -> None:
        self.failed += 1
        self.errors.append(msg)
        print(f"  [FAIL] {msg}")
        if detail:
            print(f"         {detail.strip()[:400]}")

    def skip(self, msg: str) -> None:
        self.skipped += 1
        print(f"  [SKIP] {msg}")


def main() -> int:
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    if not argv:
        print("usage: feature_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    fixtures = source_root / "AMC" / "tests" / "fixtures"
    amc = bin_dir / exe_name("amc")

    def bin_path(name: str) -> str:
        return str(bin_dir / exe_name(name))

    def run(args, **kwargs):
        return subprocess.run(args, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", **kwargs)

    report = Report()
    if not amc.is_file():
        print(f"amc not found at {amc}", file=sys.stderr)
        return 2

    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    zig = shutil.which("zig")
    rustc = shutil.which("rustc")
    work = Path(tempfile.mkdtemp(prefix="amc-feature."))

    try:
        # --- A. front/back separation ------------------------------------
        langs = run([str(amc), "--list-languages"])
        advertised = langs.stdout.split() if langs.returncode == 0 else []
        if all(lang in advertised for lang in ("cpp", "rust", "zig", "lua")):
            report.ok(f"amc --list-languages advertises the providers ({', '.join(advertised)})")
        else:
            report.fail("amc --list-languages advertises cpp/rust/zig/lua", langs.stderr)

        for lang, needs_frontend in (("cpp", True), ("rust", True), ("zig", False)):
            desc = run([str(amc), "--describe-language", lang])
            text = desc.stdout
            if desc.returncode != 0 or "protocol=jsonl-v1" not in text:
                report.fail(f"amc --describe-language {lang} shows the JSONL protocol", desc.stderr)
                continue
            if needs_frontend and "frontend" not in text:
                report.fail(f"amc --describe-language {lang} advertises a frontend")
            elif not needs_frontend and "backend" not in text:
                report.fail(f"amc --describe-language {lang} advertises a backend")
            else:
                report.ok(f"amc --describe-language {lang}: {text.strip()}")

        ipc_input = '{"type":"INIT","protocol":1}\n{"type":"QUERY_CAPABILITIES"}\n{"type":"DONE"}\n'
        for provider in ("amc-cpp", "amc-rust"):
            proc = run([bin_path(provider), "--ipc"], input=ipc_input)
            if '"type":"READY"' in proc.stdout and '"type":"CAPABILITIES"' in proc.stdout:
                report.ok(f"{provider} --ipc handshake: READY + CAPABILITIES")
            else:
                report.fail(f"{provider} --ipc handshake", proc.stdout + proc.stderr)

        # --- B. code parsing -> .abix ------------------------------------
        artifacts: dict[str, Path] = {}
        for name in ("amc_test", "rust_demo"):
            out = work / name
            built = run([str(amc), "build", "-c", str(fixtures / f"{name}.abic.toml"), "-B", str(out)])
            found = next(iter(out.rglob("*.abix")), None)
            if built.returncode == 0 and found is not None:
                artifacts[name] = found
                report.ok(f"{name}: frontend parsed the source into {found.name}")
            else:
                report.fail(f"{name}: amc build produced a .abix", built.stderr + built.stdout)

        # --- C. IR generation / inspection -------------------------------
        cxx_artifact = artifacts.get("amc_test")
        if cxx_artifact is not None:
            valid = run([str(amc), "validate", str(cxx_artifact)])
            if valid.returncode == 0:
                report.ok("amc validate accepts the C++ artifact")
            else:
                report.fail("amc validate accepts the C++ artifact", valid.stderr)

            inspect = run([str(amc), "inspect", str(cxx_artifact)])
            if inspect.returncode == 0 and "type" in inspect.stdout.lower():
                report.ok("amc inspect reads the IR")
            else:
                report.fail("amc inspect reads the IR", inspect.stdout + inspect.stderr)

            query = run([str(amc), "query", str(cxx_artifact), "--type", "AmcTestFoo", "--layout"])
            if query.returncode == 0:
                report.ok("amc query resolves a type from the IR")
            else:
                report.fail("amc query resolves a type from the IR", query.stderr)

            context = run([str(amc), "context", str(cxx_artifact), "--format", "json"])
            if context.returncode == 0 and "{\"" in context.stdout:
                report.ok("amc context renders the IR as JSON")
            else:
                report.fail("amc context renders the IR as JSON", context.stdout + context.stderr)

        # --- D. native language binding ----------------------------------
        generated: dict[str, Path] = {}
        if cxx_artifact is not None:
            expected = {"cpp": "amc_generated.hpp", "zig": "amc_generated.zig",
                        "rust": "amc_generated.rs", "lua": "amc_lua_contract.hpp"}
            for lang, filename in expected.items():
                out = work / f"gen-{lang}"
                gen = run([str(amc), "generate", str(cxx_artifact), "-l", lang, "-o", str(out)])
                produced = next(iter(out.rglob(filename)), None)
                if gen.returncode == 0 and produced is not None:
                    generated[lang] = produced
                    report.ok(f"amc generate -l {lang} produced {filename}")
                else:
                    report.fail(f"amc generate -l {lang} produced {filename}", gen.stderr)

            # The C++ binding must be usable: compile a TU that includes it.
            header = generated.get("cpp")
            if header is None:
                report.skip("C++ binding compile check (no generated header)")
            elif not cxx:
                report.skip("C++ binding compile check (no C++ compiler)")
            else:
                src = work / "binding_check.cpp"
                src.write_text(f'#include "{header.name}"\nint main() {{ return 0; }}\n', encoding="utf-8")
                compile_result = run([cxx, "-std=c++17", f"-I{header.parent}", f"-I{source_root}",
                                      str(src), "-o", str(work / "binding_check")])
                if compile_result.returncode == 0:
                    report.ok("the generated C++ binding compiles")
                else:
                    report.fail("the generated C++ binding compiles", compile_result.stderr)

            # The Rust binding must be valid Rust: compile the projection.
            rust_proj = generated.get("rust")
            if rust_proj is None:
                report.skip("Rust binding compile check (no generated projection)")
            elif not rustc:
                report.skip("Rust binding compile check (no rustc)")
            else:
                compile_result = run([rustc, "--edition", "2021", "--crate-type", "lib",
                                      str(rust_proj), "-o", str(work / "amc_generated.rlib")])
                if compile_result.returncode == 0:
                    report.ok("the generated Rust binding compiles")
                else:
                    report.fail("the generated Rust binding compiles", compile_result.stderr)

        # --- E. IR reuse -------------------------------------------------
        if cxx_artifact is not None:
            if len(generated) == 4:
                report.ok("one .abix drives all four backends (cpp/zig/rust/lua)")
            else:
                report.fail(
                    f"one .abix drives all four backends (got {sorted(generated)})")

        print(f"\nAMC feature set: {report.passed} passed, {report.failed} failed, "
              f"{report.skipped} skipped")
        return 0 if report.failed == 0 else 1
    finally:
        if KEEP_ARTIFACTS:
            print(f"  [KEEP] retained artifacts: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
