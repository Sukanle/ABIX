#!/usr/bin/env python3
"""`amc-rust` provider conformance run.

Usage:
    conformance_test.py <amc_bin_dir> [source_root]

The generic provider-contract steps live in `conformance/provider_harness.py`;
this file only declares the Rust provider's fixture and expected ABI facts.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "conformance")))

from provider_harness import ProviderCase, ProviderConformance


def rust_compile(tool: str, projection: str, out_dir: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [tool, "--edition", "2021", "--crate-type=lib", "--emit=metadata",
         "--out-dir", out_dir, projection],
        capture_output=True, text=True,
    )


def make_case(source_root: str) -> ProviderCase:
    fixtures = os.path.join(source_root, "AMC", "tests", "fixtures")
    return ProviderCase(
        language="rust",
        config=os.path.join(fixtures, "rust_demo.abic.toml"),
        artifact_name="rust_demo.abix",
        expected_functions=4,
        layout_checks=[
            ("RustRect", "RustRect::origin offset = 0", ["origin", "offset=0"]),
            ("RustRect", "RustRect::width offset = 8", ["width", "offset=8"]),
            ("RustRect", "RustRect::height offset = 16", ["height", "offset=16"]),
            ("RustAligned", "repr(C, align(16)) overrides the alignment",
             ["size=16", "align=16"]),
        ],
        projection_name="amc_generated.rs",
        projection_checks=[
            ("projection uses repr(C)", ["#[repr(C)]"]),
            ("C int projects to core::ffi::c_int", ["core::ffi::c_int"]),
            ("C double projects to core::ffi::c_double", ["core::ffi::c_double"]),
            ("projection emits offset assertions", ["offset_of!(RustRect, width)"]),
            ("projection embeds the ABIX metadata region", ["AMC_METADATA_REGION"]),
        ],
        compile_tool="rustc",
        compile=rust_compile,
    )


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    bin_dir = sys.argv[1]
    source_root = sys.argv[2] if len(sys.argv) > 2 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", ".."))
    return ProviderConformance(bin_dir, make_case(source_root)).run_all()


if __name__ == "__main__":
    sys.exit(main())
