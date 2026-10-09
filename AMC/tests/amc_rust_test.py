#!/usr/bin/env python3
"""AMC Rust provider tests.

Usage:
    amc_rust_test.py <amc_bin_dir> [source_root]

Exercises the `amc-rust` provider end to end:

    * source-level extraction of `#[repr(C)]` types and `extern "C"` functions;
    * C layout (offsets / size / alignment) computed from Rust source;
    * `.abix` round-trip through the shared reader (`amc inspect` / `amc query`);
    * Rust projection generation (`amc generate -l rust`) and, when a Rust
      toolchain is available, compilation of the projection;
    * determinism of the produced artifact.
"""

import os
import shutil
import subprocess
import sys
import tempfile


def info(message: str) -> None:
    print(f"  [INFO] {message}")


class RustTest:
    def __init__(self, bin_dir: str, source_root: str) -> None:
        self.bin_dir = os.path.abspath(bin_dir)
        self.amc = os.path.join(self.bin_dir, "amc")
        self.source_root = source_root
        self.fixtures = os.path.join(source_root, "AMC", "tests", "fixtures")
        self.work = tempfile.mkdtemp(prefix="amc-rust.")
        self.passed = 0
        self.failed = 0

    def cleanup(self) -> None:
        shutil.rmtree(self.work, ignore_errors=True)

    def pass_(self, message: str) -> None:
        self.passed += 1
        print(f"  [PASS] {message}")

    def fail(self, message: str) -> None:
        self.failed += 1
        print(f"  [FAIL] {message}")

    def run(self, args) -> subprocess.CompletedProcess:
        return subprocess.run(args, capture_output=True, text=True)

    def check(self, condition: bool, message: str) -> None:
        if condition:
            self.pass_(message)
        else:
            self.fail(message)

    def config(self) -> str:
        return os.path.join(self.fixtures, "rust_demo.abic.toml")

    def test_describe(self) -> None:
        result = self.run([self.amc, "--describe-language", "rust"])
        self.check(result.returncode == 0 and "protocol=jsonl-v1" in result.stdout,
                   "amc --describe-language rust advertises the JSONL protocol")
        listed = self.run([self.amc, "--list-languages"])
        self.check("rust" in listed.stdout.split(), "amc --list-languages advertises rust")

    def test_build_and_query(self) -> str:
        build = os.path.join(self.work, "build")
        result = self.run([self.amc, "build", "-c", self.config(), "-B", build])
        artifact = os.path.join(build, "rust_demo.abix")
        if result.returncode != 0 or not os.path.isfile(artifact):
            self.fail(f"amc build produced the Rust artifact ({result.stderr.strip()})")
            return artifact
        self.pass_("amc build produced the Rust artifact")

        inspect = self.run([self.amc, "inspect", artifact])
        self.check(inspect.returncode == 0 and "functions=4" in inspect.stdout,
                   "the artifact records the four extern functions")

        query = self.run([self.amc, "query", artifact, "--type", "RustRect", "--layout"])
        out = query.stdout
        self.check("origin" in out and "offset=0" in out, "RustRect::origin offset = 0")
        self.check("width" in out and "offset=8" in out, "RustRect::width offset = 8")
        self.check("height" in out and "offset=16" in out, "RustRect::height offset = 16")

        aligned = self.run([self.amc, "query", artifact, "--type", "RustAligned", "--layout"])
        self.check("size=16" in aligned.stdout and "align=16" in aligned.stdout,
                   "repr(C, align(16)) overrides the alignment")
        return artifact

    def test_generate(self, artifact: str) -> None:
        gen_dir = os.path.join(self.work, "gen")
        result = self.run([self.amc, "generate", artifact, "-l", "rust", "-o", gen_dir])
        projection = os.path.join(gen_dir, "amc_generated.rs")
        if result.returncode != 0 or not os.path.isfile(projection):
            self.fail(f"amc generate -l rust produced a projection ({result.stderr.strip()})")
            return
        self.pass_("amc generate -l rust produced a projection")
        with open(projection, "r", encoding="utf-8") as handle:
            text = handle.read()
        self.check("#[repr(C)]" in text, "projection uses repr(C)")
        self.check("core::ffi::c_int" in text, "C int projects to core::ffi::c_int")
        self.check("core::ffi::c_double" in text, "C double projects to core::ffi::c_double")
        self.check("offset_of!(RustRect, width)" in text, "projection emits offset assertions")
        self.check("AMC_METADATA_REGION" in text, "projection embeds the ABIX metadata region")

        rustc = shutil.which("rustc")
        if rustc is None:
            info("rustc not found: skipping projection compilation")
            return
        compile_result = self.run([rustc, "--edition", "2021", "--crate-type=lib",
                                   "--emit=metadata", "--out-dir", os.path.dirname(projection), projection])
        self.check(compile_result.returncode == 0,
                   f"generated Rust projection compiles ({compile_result.stderr.strip()[:200]})")

    def test_determinism(self) -> None:
        first = os.path.join(self.work, "det-a")
        second = os.path.join(self.work, "det-b")
        self.run([self.amc, "build", "-c", self.config(), "-B", first])
        self.run([self.amc, "build", "-c", self.config(), "-B", second])
        a = os.path.join(first, "rust_demo.abix")
        b = os.path.join(second, "rust_demo.abix")
        if not (os.path.isfile(a) and os.path.isfile(b)):
            self.fail("determinism: both builds produced an artifact")
            return
        with open(a, "rb") as fa, open(b, "rb") as fb:
            self.check(fa.read() == fb.read(), "identical Rust inputs produce byte-identical artifacts")

    def finish(self) -> int:
        print(f"\n{self.passed} passed, {self.failed} failed")
        self.cleanup()
        return 0 if self.failed == 0 else 1


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    bin_dir = sys.argv[1]
    source_root = sys.argv[2] if len(sys.argv) > 2 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    test = RustTest(bin_dir, source_root)
    test.test_describe()
    artifact = test.test_build_and_query()
    if os.path.isfile(artifact):
        test.test_generate(artifact)
    test.test_determinism()
    return test.finish()


if __name__ == "__main__":
    sys.exit(main())
