#!/usr/bin/env python3
"""AMC integration test suite.

Usage:
    amc_integration_test.py <amc_bin_dir> [--keep-artifacts]

Behaviour:
    Exercises the amc CLI toolchain end-to-end: frontend, validate, inspect,
    generate, build, diff, compatibility, self-description bootstrap, and IPC.
    Each step is independently scored as PASS or FAIL.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import shutil


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOURCE_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "../.."))


def info(msg: str) -> None:
    print(f"  [INFO] {msg}")


class TestRunner:
    """Collects pass/fail counts and manages a build directory."""

    def __init__(self, amc_bin_dir: str, keep_artifacts: bool = False) -> None:
        self.amc_bin = os.path.abspath(amc_bin_dir)
        self.fixtures_dir = os.path.join(SCRIPT_DIR, "fixtures")
        self.keep_artifacts = keep_artifacts

        bd = tempfile.mkdtemp(prefix="amc-integration.", dir=os.environ.get("TMPDIR", "/tmp"))
        self.build_dir = bd
        self.gen_separate = os.path.join(bd, "autogen-separate")
        self.gen_full = os.path.join(bd, "autogen-full")
        os.makedirs(self.gen_separate, exist_ok=True)
        os.makedirs(self.gen_full, exist_ok=True)

        self.passed = 0
        self.failed = 0

    def cleanup(self) -> None:
        if not self.keep_artifacts:
            shutil.rmtree(self.build_dir, ignore_errors=True)

    def pass_(self, msg: str) -> None:
        self.passed += 1
        print(f"  [PASS] {msg}")

    def fail(self, msg: str) -> None:
        self.failed += 1
        print(f"  [FAIL] {msg}")

    # ------------------------------------------------------------------
    # helpers
    # ------------------------------------------------------------------
    def run(self, cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
        """Run a command and return the CompletedProcess instance."""
        return subprocess.run(cmd, capture_output=True, text=True, **kwargs)

    def check_output(self, msg: str, cmd: list[str]) -> bool:
        """Run a command; print PASS on success, FAIL on failure."""
        result = self.run(cmd)
        if result.returncode == 0:
            self.pass_(msg)
            return True
        else:
            self.fail(msg)
            return False

    def grep(self, content: str, pattern: str) -> bool:
        """Simple grep: return True if pattern appears in content."""
        return re.search(pattern, content) is not None

    def has_binary(self, name: str) -> bool:
        """Return True if the named binary is on PATH (shutil.which)."""
        return shutil.which(name) is not None

    def section_names(self, path: str):
        """Return readelf section listing text, or None if no reader exists."""
        tool = shutil.which("readelf") or shutil.which("llvm-readelf")
        if tool is None:
            return None
        result = self.run([tool, "-S", path])
        return result.stdout if result.returncode == 0 else None

    def stripper(self):
        """Return the first available strip tool, or None."""
        for name in ("strip", "llvm-strip"):
            path = shutil.which(name)
            if path is not None:
                return path
        return None

    def fixture_path(self, name: str) -> str:
        return os.path.join(self.fixtures_dir, name)

    def bin_path(self, name: str) -> str:
        return os.path.join(self.amc_bin, name)

    # ==================================================================
    # Steps
    # ==================================================================
    def step01_frontend(self) -> bool:
        """Step 1: amc frontend -l cpp -c <config> -o <output>"""
        cmd = [
            self.bin_path("amc"), "frontend",
            "-l", "cpp",
            "-c", self.fixture_path("amc_test.abic.toml"),
            "-o", os.path.join(self.gen_separate, "test.abix"),
        ]
        ok = self.check_output("frontend succeeded", cmd)
        if not ok:
            print("  Skipping remaining steps due to frontend failure")
        return ok

    def step02_validate(self) -> bool:
        """Step 2: amc validate <abix>"""
        cmd = [self.bin_path("amc"), "validate",
               os.path.join(self.gen_separate, "test.abix")]
        return self.check_output("validate succeeded", cmd)

    def step03_inspect(self) -> bool:
        """Step 3: amc inspect <abix>"""
        abix = os.path.join(self.gen_separate, "test.abix")
        result = self.run([self.bin_path("amc"), "inspect", abix])
        info(f"inspect output: {result.stdout.strip()}")
        if self.grep(result.stdout, r"types="):
            self.pass_("inspect shows types")
            return True
        else:
            self.fail("inspect missing types")
            return False

    def step03b_dump_json(self) -> bool:
        """Step 3b: amc-dump JSON metadata check."""
        abix = os.path.join(self.gen_separate, "test.abix")
        dump_json = os.path.join(self.build_dir, "dump.json")
        cmd = [self.bin_path("amc-dump"), abix, "--json", dump_json]
        if not self.check_output("amc-dump JSON succeeded", cmd):
            return False

        with open(dump_json) as f:
            content = f.read()

        patterns = [
            '"format_version": 4',
            '"layout_hash"',
            '"runtime_descriptor"',
            '"type_id_bits": 128',
            '"registry": "RuntimeRegistry"',
        ]
        all_ok = True
        for pat in patterns:
            if self.grep(content, pat):
                self.pass_(f"amc-dump JSON contains: {pat}")
            else:
                self.fail(f"amc-dump JSON missing: {pat}")
                all_ok = False
        return all_ok

    def step04_generate(self) -> bool:
        """Step 4: amc generate <abix> -l cpp -o <dir>"""
        abix = os.path.join(self.gen_separate, "test.abix")
        cmd = [
            self.bin_path("amc"), "generate", abix,
            "-l", "cpp", "-o", self.gen_separate,
        ]
        return self.check_output("generate succeeded", cmd)

    def step05_verify_header_content(self) -> bool:
        """Step 5: verify generated header content."""
        header = os.path.join(self.gen_separate, "amc_generated.hpp")
        if not os.path.isfile(header):
            self.fail("generated header does not exist")
            return False
        self.pass_("generated header exists")

        with open(header) as f:
            content = f.read()

        patterns = [
            "AmcTestFoo_ABIX", "AmcTestBar_ABIX", "AmcTestColor_ABIX",
            "type_id_lo", "type_id_hi", "x_offset", "y_offset",
            "a_offset", "b_offset", "c_offset",
            "TypeTraits", "LayoutInfo", "FunctionInfo", "ModuleInfo",
            "namespace amc_generated", "#pragma once",
        ]
        all_ok = True
        for pat in patterns:
            if self.grep(content, pat):
                self.pass_(f"generated header contains: {pat}")
            else:
                self.fail(f"generated header missing: {pat}")
                all_ok = False
        return all_ok

    def step06_compile_check(self) -> None:
        """Step 6: verify generated header compiles as C++17 (if clang++ available)."""
        if not self.has_binary("clang++"):
            info("clang++ not found, skipping C++17 syntax check")
            return
        header = os.path.join(self.gen_separate, "amc_generated.hpp")
        cmd = [
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}",
            "-fsyntax-only",
            header,
        ]
        self.check_output("generated header compiles as C++17", cmd)

    def step07_build_pipeline(self) -> bool:
        """Step 7: amc build -B <build_dir> <config> (full pipeline)."""
        config = self.fixture_path("amc_test.abic.toml")
        cmd = [self.bin_path("amc"), "build", "-B", self.gen_full, config]
        if not self.check_output("amc build succeeded", cmd):
            return False

        abix_artifact = os.path.join(self.gen_full, "build/amc_test.abix")
        gen_hpp = os.path.join(self.gen_full, "generated/amc_generated.hpp")

        all_ok = True
        if os.path.isfile(abix_artifact):
            self.pass_("amc build produced configured .abix artifact")
        else:
            self.fail("amc build did not produce configured .abix artifact")
            all_ok = False

        if not os.path.isfile(gen_hpp):
            self.pass_("amc build does not generate language projection")
        else:
            self.fail("amc build unexpectedly generated language projection")
            all_ok = False

        return all_ok

    def step08_m8_metadata(self) -> bool:
        """Step 8: AMC-M8 semantic metadata."""
        m8_abix = os.path.join(self.build_dir, "m8.abix")
        m8_dump = os.path.join(self.build_dir, "m8.dump")

        # frontend
        fe_cmd = [
            self.bin_path("amc"), "frontend",
            "-l", "cpp",
            "-c", self.fixture_path("amc_m8.abic.toml"),
            "-o", m8_abix,
        ]
        result_fe = self.run(fe_cmd)
        if result_fe.returncode != 0:
            self.fail("M8 fixture frontend failed")
            return False

        # dump
        with open(m8_dump, "w") as f:
            result_dump = self.run([self.bin_path("amc-dump"), m8_abix])
            f.write(result_dump.stdout)

        if result_dump.returncode != 0:
            self.fail("M8 fixture dump failed")
            return False

        self.pass_("M8 fixture frontend and dump succeeded")

        with open(m8_dump) as f:
            content = f.read()

        patterns = [
            r"amc_m8::IntBox kind=record",
            r"amc_m8::Count kind=alias",
            r"bits type=",
            r"flags=50331649",
            r"Base type=",
            r"calling_convention=1",
        ]
        all_ok = True
        for pat in patterns:
            if self.grep(content, pat):
                self.pass_(f"M8 metadata contains: {pat}")
            else:
                self.fail(f"M8 metadata missing: {pat}")
                all_ok = False
        return all_ok

    def step08b_map_private(self) -> bool:
        """Step 8b: M8 static MapPrivate."""
        map_v1 = os.path.join(self.build_dir, "map-v1.abix")
        map_v2 = os.path.join(self.build_dir, "map-v2.abix")
        map_report = os.path.join(self.build_dir, "map-report.abix")
        map_hpp = os.path.join(self.build_dir, "amc_map.hpp")
        map_dump = os.path.join(self.build_dir, "map-diff.dump")
        map_dump_json = os.path.join(self.build_dir, "map-diff.json")

        ok = True

        # frontend v1
        r = self.run([self.bin_path("amc"), "frontend", "-l", "cpp",
                       "-c", self.fixture_path("map_v1.abic.toml"),
                       "-o", map_v1])
        if r.returncode != 0:
            ok = False

        # frontend v2
        r = self.run([self.bin_path("amc"), "frontend", "-l", "cpp",
                       "-c", self.fixture_path("map_v2.abic.toml"),
                       "-o", map_v2])
        if r.returncode != 0:
            ok = False

        # diff
        r = self.run([self.bin_path("amc"), "diff", map_v1, map_v2, "-o", map_report])
        if r.returncode != 0:
            ok = False

        # generate
        r = self.run([self.bin_path("amc"), "generate", map_report,
                       "-l", "cpp", "-o", map_hpp])
        if r.returncode != 0:
            ok = False

        # compile and run consumer
        consumer_src = os.path.join(SCRIPT_DIR, "map_private_consumer.cpp")
        consumer_bin = os.path.join(self.build_dir, "map_private_consumer")
        if self.has_binary("clang++"):
            r = self.run([
                "clang++", "-std=c++17",
                f"-I{SOURCE_ROOT}", f"-I{self.build_dir}",
                consumer_src, "-o", consumer_bin,
            ])
            if r.returncode == 0:
                r = self.run([consumer_bin])
                if r.returncode != 0:
                    ok = False
            else:
                ok = False
        else:
            info("clang++ not found, skipping MapPrivate consumer compilation")
            ok = False

        # amc-dump diff (text)
        with open(map_dump, "w") as f:
            r = self.run([self.bin_path("amc-dump"), "diff", map_v1, map_v2])
            f.write(r.stdout)
        if r.returncode != 0:
            ok = False

        # amc-dump diff (json)
        r = self.run([self.bin_path("amc-dump"), "diff", map_v1, map_v2,
                       "--json", map_dump_json])
        if r.returncode != 0:
            ok = False

        # check patterns
        with open(map_dump) as f:
            dump_content = f.read()
        if not self.grep(dump_content, r"map_compatible"):
            ok = False

        with open(map_dump_json) as f:
            json_content = f.read()
        if not self.grep(json_content, r'"compatibility"'):
            ok = False

        if ok:
            self.pass_("MapPrivate matches Runtime Map copy/default semantics")
            self.pass_("amc-dump diff reports compatibility and JSON")
        else:
            self.fail("MapPrivate generation, Runtime Map equivalence, or amc-dump diff failed")
        return ok

    def step09_diff_compatibility(self) -> bool:
        """Step 9: AMC-M9 diff and compatibility commands."""
        test_abix = os.path.join(self.gen_separate, "test.abix")
        m9_report = os.path.join(self.build_dir, "m9-report.abix")

        r1 = self.run([self.bin_path("amc"), "diff", test_abix, test_abix, "-o", m9_report])
        r2 = self.run([self.bin_path("amc"), "validate", m9_report])
        r3 = self.run([self.bin_path("amc"), "compatibility", test_abix, test_abix])

        if r1.returncode == 0 and r2.returncode == 0 and r3.returncode == 0:
            self.pass_("M9 diff report and compatibility command succeeded")
            return True
        else:
            self.fail("M9 diff report or compatibility command failed")
            return False

    def step10_self_description(self) -> bool:
        """Step 10: ABIX self-description bootstrap artifact."""
        self_build = os.path.join(self.build_dir, "self-build")
        self_abix = os.path.join(self_build, "build/abix_self.abix")
        self_hpp = os.path.join(self.build_dir, "abix_self_metadata.hpp")

        # build
        r = self.run([self.bin_path("amc"), "build",
                       "-c", os.path.join(SOURCE_ROOT, "abix/self/abix_self.abic.toml"),
                       "-B", self_build])
        if r.returncode != 0:
            self.fail("ABIX self-description generation failed")
            return False

        # generate
        r = self.run([self.bin_path("amc"), "generate", self_abix,
                       "-l", "cpp", "-o", self_hpp])
        if r.returncode != 0:
            self.fail("ABIX self-description generation (generate) failed")
            return False

        # validate
        r = self.run([self.bin_path("amc"), "validate", self_abix])
        if r.returncode != 0:
            self.fail("ABIX self-description validation failed")
            return False

        self.pass_("ABIX self-description artifact generated and validated")

        # inspect
        r_inspect = self.run([self.bin_path("amc"), "inspect", self_abix])

        # check header patterns
        with open(self_hpp) as f:
            hpp_content = f.read()

        all_ok = True
        for pat in ["skl_abix_runtime_TypeDescriptor_ABIX", "skl_abix_model_TypeDesc_ABIX"]:
            if self.grep(hpp_content, pat):
                self.pass_(f"self-description contains {pat}")
            else:
                self.fail(f"self-description is missing {pat}")
                all_ok = False

        # compile and run consumer
        if self.has_binary("clang++"):
            consumer_src = os.path.join(SCRIPT_DIR, "self_type_of_consumer.cpp")
            consumer_bin = os.path.join(self.build_dir, "self_type_of_consumer")
            r = self.run([
                "clang++", "-std=c++17",
                f"-I{SOURCE_ROOT}", f"-I{self.build_dir}",
                consumer_src, "-o", consumer_bin,
            ])
            if r.returncode == 0:
                r = self.run([consumer_bin])
                if r.returncode == 0:
                    self.pass_("native type_of, Registry lookup, and registered EBR metadata self-hosting succeeded")
                else:
                    self.fail("native type_of, Registry lookup, or registered EBR metadata self-hosting failed")
                    all_ok = False
            else:
                self.fail("compilation of self_type_of_consumer failed")
                all_ok = False
        else:
            info("clang++ not found, skipping self-description consumer compilation")

        return all_ok

    def step11_amc_self_description(self) -> bool:
        """Step 11: AMC core self-description bootstrap artifact."""
        amc_self_build = os.path.join(self.build_dir, "amc-self-build")
        amc_self_abix = os.path.join(amc_self_build, "build/amc_core.abix")
        amc_self_hpp = os.path.join(self.build_dir, "amc_core.hpp")

        # build
        r = self.run([self.bin_path("amc"), "build",
                       "-c", os.path.join(SOURCE_ROOT, "amc/self.abic.toml"),
                       "-B", amc_self_build])
        if r.returncode != 0:
            self.fail("AMC core self-description generation failed")
            return False

        # validate
        r = self.run([self.bin_path("amc"), "validate", amc_self_abix])
        if r.returncode != 0:
            self.fail("AMC core self-description validation failed")
            return False

        # generate
        r = self.run([self.bin_path("amc"), "generate", amc_self_abix,
                       "-l", "cpp", "-o", amc_self_hpp])
        if r.returncode != 0:
            self.fail("AMC core self-description generation (generate) failed")
            return False

        self.pass_("AMC core self-description artifact generated and validated")

        with open(amc_self_hpp) as f:
            hpp_content = f.read()

        all_ok = True
        for pat in ["amc_AbiModule_ABIX", "amc_MapOperation_ABIX"]:
            if self.grep(hpp_content, pat):
                self.pass_(f"AMC self-description contains {pat}")
            else:
                self.fail(f"AMC self-description is missing {pat}")
                all_ok = False

        # compile and run consumer
        if self.has_binary("clang++"):
            consumer_src = os.path.join(SCRIPT_DIR, "amc_self_consumer.cpp")
            consumer_bin = os.path.join(self.build_dir, "amc_self_consumer")
            r = self.run([
                "clang++", "-std=c++17",
                f"-I{SOURCE_ROOT}", f"-I{self.build_dir}",
                consumer_src, "-o", consumer_bin,
            ])
            if r.returncode == 0:
                r = self.run([consumer_bin])
                if r.returncode == 0:
                    self.pass_("AMC native type_of self-description consumer succeeded")
                else:
                    self.fail("AMC native type_of self-description consumer failed")
                    all_ok = False
            else:
                self.fail("compilation of amc_self_consumer failed")
                all_ok = False
        else:
            info("clang++ not found, skipping AMC self-description consumer compilation")

        return all_ok

    def step12_ipc_capabilities(self) -> bool:
        """Step 12: AMC-M10 provider IPC and capabilities."""
        # JSON lines handshake via stdin to amc-cpp --ipc
        ipc_input = (
            '{"type":"INIT","protocol":1}\n'
            '{"type":"QUERY_CAPABILITIES"}\n'
            '{"type":"DONE"}\n'
        )
        r = self.run(
            [self.bin_path("amc-cpp"), "--ipc"],
            input=ipc_input,
        )
        ipc_out = r.stdout

        # --list-languages and --describe-language
        r_langs = self.run([self.bin_path("amc"), "--list-languages"])
        r_desc = self.run([self.bin_path("amc"), "--describe-language", "cpp"])

        all_ok = True

        if self.grep(ipc_out, r'"type":"READY"'):
            self.pass_('IPC output contains "type":"READY"')
        else:
            self.fail('IPC output missing "type":"READY"')
            all_ok = False

        if self.grep(ipc_out, r'"type":"CAPABILITIES"'):
            self.pass_('IPC output contains "type":"CAPABILITIES"')
        else:
            self.fail('IPC output missing "type":"CAPABILITIES"')
            all_ok = False

        if r_langs.returncode == 0 and "cpp" in r_langs.stdout.split():
            self.pass_("amc --list-languages includes cpp")
        else:
            self.fail("amc --list-languages missing cpp")
            all_ok = False

        if r_desc.returncode == 0 and self.grep(r_desc.stdout, r"protocol=jsonl-v1"):
            self.pass_("amc --describe-language cpp shows protocol=jsonl-v1")
        else:
            self.fail("amc --describe-language cpp missing protocol=jsonl-v1")
            all_ok = False

        if all_ok:
            self.pass_("M10 IPC handshake and capability discovery succeeded")
        else:
            self.fail("M10 IPC handshake or capability discovery failed")
        return all_ok

    # ==================================================================
    # AI-P1: LLM context, contract verification, structured errors
    # ==================================================================
    def step13_llm_context(self) -> bool:
        """Step 13: amc context --format llm|json renders an AI-friendly view."""
        abix = os.path.join(self.gen_separate, "test.abix")

        llm = self.run([self.bin_path("amc"), "context", abix, "--format", "llm"])
        if llm.returncode == 0 and self.grep(llm.stdout, r"# ABIX ABI context") \
                and self.grep(llm.stdout, r"counts: types=") and self.grep(llm.stdout, r"T0 "):
            self.pass_("context --format llm renders the ABI outline")
        else:
            self.fail("context --format llm output is invalid")
            return False

        structured = self.run([self.bin_path("amc"), "context", abix, "--format", "json"])
        try:
            document = json.loads(structured.stdout)
        except ValueError:
            document = None
        if (structured.returncode == 0 and document
                and document.get("schema") == "abix.context/1"
                and document.get("counts", {}).get("types", 0) > 0):
            self.pass_("context --format json is structured")
            return True
        self.fail("context --format json is not structured")
        return False

    def step14_verify_consistent(self) -> bool:
        """Step 14: amc verify accepts a runtime implementation that matches."""
        cmd = [
            self.bin_path("amc"), "verify",
            "-c", self.fixture_path("amc_test.abic.toml"),
            "-B", self.gen_full,
            "--format", "json",
        ]
        result = self.run(cmd)
        try:
            document = json.loads(result.stdout)
        except ValueError:
            document = None
        if (result.returncode == 0 and document
                and document.get("schema") == "abix.verify/1"
                and document.get("consistent") is True):
            self.pass_("verify reports contract and implementation as consistent")
            return True
        self.fail(f"verify consistent run failed (rc={result.returncode})")
        return False

    def step15_verify_drift(self) -> bool:
        """Step 15: amc verify detects ABI drift between two artifacts."""
        contract = os.path.join(self.gen_full, "build/amc_test.abix")
        other = os.path.join(self.build_dir, "m8.abix")
        if not os.path.isfile(contract) or not os.path.isfile(other):
            self.fail("verify drift prerequisites are missing")
            return False
        result = self.run([self.bin_path("amc"), "verify", contract, other, "--format", "json"])
        try:
            document = json.loads(result.stdout)
        except ValueError:
            document = None
        if (result.returncode == 1 and document
                and document.get("consistent") is False
                and document.get("modules", [{}])[0].get("changes")):
            self.pass_("verify reports drift for incompatible metadata")
            return True
        self.fail("verify did not report drift")
        return False

    def step16_error_schema(self) -> bool:
        """Step 16: every AMC error can be emitted as one JSON envelope."""
        missing = os.path.join(self.build_dir, "does-not-exist.abix")
        result = self.run([self.bin_path("amc"), "validate", missing, "--error-format", "json"])
        try:
            document = json.loads(result.stderr)
        except ValueError:
            document = None
        if (result.returncode == 1 and document
                and document.get("schema") == "abix.error/1"
                and document.get("error", {}).get("category") == "io"
                and document.get("error", {}).get("code") == "AMC-IO"):
            self.pass_("AMC errors carry the unified JSON envelope")
            return True
        self.fail("structured error envelope is missing or invalid")
        return False

    def step17_metadata_region(self) -> bool:
        """Step 17: the self-describing Metadata Region round-trips + verifies."""
        abix = os.path.join(self.gen_separate, "test.abix")
        region = os.path.join(self.build_dir, "test.abixmeta")
        build_id = "00112233445566778899aabbccddeeff"
        emit = self.run([self.bin_path("amc"), "metadata", abix, "-o", region,
                         "--build-id", build_id])
        if emit.returncode != 0 or not os.path.isfile(region):
            self.fail("metadata region emission failed")
            return False

        verify = self.run([self.bin_path("amc"), "metadata", "--verify", region, "--format", "json"])
        try:
            document = json.loads(verify.stdout)
        except ValueError:
            document = None
        if not (verify.returncode == 0 and document
                and document.get("schema") == "abix.metadata/1"
                and document.get("counts", {}).get("types", 0) > 0
                and document.get("build_id") == "0x" + build_id):
            self.fail("metadata region verification failed")
            return False
        self.pass_("metadata region round-trips and verifies")
        self.pass_("metadata region preserves BuildID")

        # Tamper with the desc section: MetadataID must no longer match.
        with open(region, "r+b") as handle:
            handle.seek(300)
            original = handle.read(1)
            handle.seek(300)
            handle.write(bytes([original[0] ^ 0xFF]))
        tampered = self.run([self.bin_path("amc"), "metadata", "--verify", region,
                             "--error-format", "json"])
        if tampered.returncode == 1 and self.grep(tampered.stderr, r"AMC-VALIDATION"):
            self.pass_("metadata region detects content tampering")
            return True
        self.fail("metadata region did not detect tampering")
        return False

    def step18_build_meta_sidecar(self) -> bool:
        """Step 18: amc build emits the `.abix.meta` indexing sidecar."""
        meta = os.path.join(self.gen_full, "build/amc_test.abix.meta")
        if not os.path.isfile(meta):
            self.fail("amc build did not emit the .abix.meta sidecar")
            return False
        with open(meta) as handle:
            content = handle.read()
        if ("[metadata]" in content and "metadata_id = " in content
                and "build_id = " in content and "type_count = " in content):
            self.pass_("amc build emits the .abix.meta sidecar")
            return True
        self.fail(".abix.meta sidecar is missing fields")
        return False

    def step19_strip_consistency(self) -> bool:
        """Step 19: `.abix.names` is emitted and safely strippable."""
        if not self.has_binary("clang++"):
            info("clang++ not found, skipping strip consistency test")
            return True
        consumer_src = os.path.join(SCRIPT_DIR, "strip_consumer.cpp")
        binary = os.path.join(self.build_dir, "strip_consumer")
        compile_result = self.run([
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}", f"-I{self.gen_separate}",
            consumer_src, "-o", binary,
        ])
        if compile_result.returncode != 0:
            self.fail("strip consumer failed to compile")
            return False
        if self.run([binary]).returncode != 0:
            self.fail("strip consumer failed before stripping")
            return False

        listing = self.section_names(binary)
        if listing is None:
            info("readelf not found, skipping section inspection")
            return True
        if ".abix.names" not in listing:
            self.fail("generated binary does not contain a .abix.names section")
            return False
        self.pass_("generated binary contains a .abix.names section")

        stripper = self.stripper()
        if stripper is None:
            info("strip tool not found, skipping section removal")
            return True
        if self.run([stripper, "--remove-section=.abix.names", binary]).returncode != 0:
            self.fail("strip --remove-section=.abix.names failed")
            return False
        if ".abix.names" in (self.section_names(binary) or ""):
            self.fail(".abix.names survived stripping")
            return False
        if self.run([binary]).returncode != 0:
            self.fail("runtime ABI lookup broke after stripping .abix.names")
            return False
        self.pass_("type_id lookup survives stripping .abix.names; name lookup stays null")
        return True

    def step20_metadata_region_elf(self) -> bool:
        """Step 20: the compiled binary embeds a scannable Metadata Region."""
        if not self.has_binary("clang++"):
            info("clang++ not found, skipping the ELF metadata scan")
            return True
        consumer_src = os.path.join(SCRIPT_DIR, "strip_consumer.cpp")
        binary = os.path.join(self.build_dir, "elf_consumer")
        compiled = self.run([
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}", f"-I{self.gen_separate}",
            consumer_src, "-o", binary,
        ])
        if compiled.returncode != 0:
            self.fail("ELF metadata consumer failed to compile")
            return False

        listing = self.section_names(binary)
        if listing is None:
            info("readelf not found, skipping the ELF metadata scan")
            return True
        if ".abix.metadata" not in listing:
            self.fail("generated binary is missing the .abix.metadata section")
            return False
        self.pass_("generated binary contains a .abix.metadata section")

        scan = self.run([self.bin_path("amc"), "metadata", "--from-elf", binary, "--format", "json"])
        try:
            document = json.loads(scan.stdout)
        except ValueError:
            document = None
        if not (scan.returncode == 0 and document
                and document.get("counts", {}).get("types", 0) > 0):
            self.fail("amc metadata --from-elf failed to scan the binary")
            return False
        self.pass_("amc metadata scans the embedded region offline")

        # The region embedded in the binary must be byte-identical in identity
        # to the one derived from the standalone .abix artifact.
        standalone = self.run([self.bin_path("amc"), "metadata",
                               os.path.join(self.gen_separate, "test.abix"), "--format", "json"])
        try:
            reference = json.loads(standalone.stdout)
        except ValueError:
            reference = None
        if not (reference and reference.get("metadata_id") == document.get("metadata_id")):
            self.fail("in-binary MetadataID differs from the .abix-derived one")
            return False
        self.pass_("in-binary MetadataID matches the standalone artifact")
        return True

    def step21_query_engine(self) -> bool:
        """Step 21: amc query resolves types/functions and compares modules."""
        abix = os.path.join(self.gen_separate, "test.abix")
        m8 = os.path.join(self.build_dir, "m8.abix")

        type_result = self.run([self.bin_path("amc"), "query", abix,
                                "--type", "AmcTestFoo", "--layout", "--format", "json"])
        try:
            type_doc = json.loads(type_result.stdout)
        except ValueError:
            type_doc = None
        if not (type_result.returncode == 0 and type_doc
                and type_doc.get("match_count") == 1
                and type_doc["matches"][0]["name"] == "AmcTestFoo"
                and len(type_doc["matches"][0].get("fields", [])) == 2):
            self.fail("amc query --type failed")
            return False
        self.pass_("amc query resolves a type and its layout")

        fn_result = self.run([self.bin_path("amc"), "query", abix,
                              "--function", "amc_test_compute", "--format", "json"])
        try:
            fn_doc = json.loads(fn_result.stdout)
        except ValueError:
            fn_doc = None
        if not (fn_result.returncode == 0 and fn_doc
                and fn_doc.get("match_count") == 1
                and fn_doc["matches"][0]["parameters"][1]["type_name"] == "double"):
            self.fail("amc query --function failed")
            return False
        self.pass_("amc query resolves a function signature")

        missing = self.run([self.bin_path("amc"), "query", abix,
                            "--type", "NoSuchType", "--format", "json"])
        if missing.returncode != 1:
            self.fail("amc query should exit 1 when nothing matches")
            return False
        self.pass_("amc query reports an empty match with exit 1")

        if not os.path.isfile(m8):
            self.fail("query compatibility prerequisite is missing")
            return False
        compat = self.run([self.bin_path("amc"), "query", abix,
                           "--compatible", m8, "--format", "json"])
        try:
            compat_doc = json.loads(compat.stdout)
        except ValueError:
            compat_doc = None
        if not (compat.returncode == 1 and compat_doc
                and compat_doc.get("compatible") is False and compat_doc.get("changes")):
            self.fail("amc query --compatible failed to detect drift")
            return False
        self.pass_("amc query compares two modules strictly")
        return True

    def step22_symbol_server(self) -> bool:
        """Step 22: local symbol server publishes/fetches by BuildID."""
        if not self.has_binary("clang++") or not self.has_binary("readelf"):
            info("clang++/readelf not found, skipping the symbol server test")
            return True
        consumer_src = os.path.join(SCRIPT_DIR, "strip_consumer.cpp")
        binary = os.path.join(self.build_dir, "store_consumer")
        compiled = self.run([
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}", f"-I{self.gen_separate}",
            consumer_src, "-o", binary, "-Wl,--build-id=sha1",
        ])
        if compiled.returncode != 0:
            self.fail("symbol server consumer failed to compile")
            return False
        notes = self.run(["readelf", "-n", binary])
        match = re.search(r"Build ID:\s*([0-9a-f]+)", notes.stdout)
        if match is None:
            self.fail("test binary has no GNU build id")
            return False
        build_id = match.group(1)

        store = os.path.join(self.build_dir, "symbol-store")
        publish = self.run([self.bin_path("amc"), "publish", binary, "--root", store])
        if publish.returncode != 0 or build_id not in publish.stdout:
            self.fail("amc publish failed to index the binary by BuildID")
            return False
        self.pass_("amc publish indexes a binary by its GNU BuildID")

        region = os.path.join(self.build_dir, "fetched.abixmeta")
        fetched = self.run([self.bin_path("amc"), "fetch", "--build-id", build_id,
                            "--root", store, "-o", region])
        if fetched.returncode != 0 or not os.path.isfile(region):
            self.fail("amc fetch --build-id failed")
            return False
        verified = self.run([self.bin_path("amc"), "metadata", "--verify", region, "--format", "json"])
        try:
            document = json.loads(verified.stdout)
        except ValueError:
            document = None
        if not (verified.returncode == 0 and document
                and document.get("counts", {}).get("types", 0) > 0):
            self.fail("fetched region did not verify")
            return False
        self.pass_("amc fetch returns a valid Metadata Region for the BuildID")

        abix = os.path.join(self.gen_separate, "test.abix")
        key = "0011223344556677"
        if self.run([self.bin_path("amc"), "publish", abix, "--root", store,
                     "--build-id", key]).returncode != 0:
            self.fail("amc publish of a .abix artifact failed")
            return False
        roundtrip = os.path.join(self.build_dir, "roundtrip.abix")
        if self.run([self.bin_path("amc"), "fetch", "--build-id", key,
                     "--root", store, "-o", roundtrip]).returncode != 0:
            self.fail("amc fetch of the published .abix failed")
            return False
        if self.run([self.bin_path("amc"), "inspect", roundtrip]).returncode != 0:
            self.fail("fetched .abix is not readable")
            return False
        self.pass_("publish/fetch round-trips a standalone .abix")

        missing = self.run([self.bin_path("amc"), "fetch", "--build-id", "deadbeef",
                            "--root", store, "--error-format", "json"])
        if missing.returncode == 1 and self.grep(missing.stderr, r"AMC-IO"):
            self.pass_("amc fetch reports a missing BuildID as a structured error")
            return True
        self.fail("amc fetch did not report a missing BuildID correctly")
        return False

    def step23_mcp_server(self) -> bool:
        """Step 23: amc-mcp answers JSON-RPC ABIX tool calls."""
        abix = os.path.join(self.gen_separate, "test.abix")
        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {"protocolVersion": "2025-06-18"}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
            {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
             "params": {"name": "abix.get_module", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 4, "method": "tools/call",
             "params": {"name": "abix.get_type",
                        "arguments": {"name": "AmcTestFoo", "layout": True}}},
            {"jsonrpc": "2.0", "id": 5, "method": "tools/call",
             "params": {"name": "abix.get_function",
                        "arguments": {"name": "amc_test_compute"}}},
            {"jsonrpc": "2.0", "id": 6, "method": "tools/call",
             "params": {"name": "abix.compare_abi", "arguments": {"other": abix}}},
            {"jsonrpc": "2.0", "id": 9, "method": "tools/call",
             "params": {"name": "abix.list_functions", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 10, "method": "tools/call",
             "params": {"name": "abix.get_layout", "arguments": {"name": "AmcTestFoo"}}},
            {"jsonrpc": "2.0", "id": 11, "method": "tools/call",
             "params": {"name": "abix.resolve_type", "arguments": {"name": "Amc"}}},
            {"jsonrpc": "2.0", "id": 7, "method": "tools/call",
             "params": {"name": "unknown.tool", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 8, "method": "bogus"},
        ]
        payload = "".join(json.dumps(request) + "\n" for request in requests)
        result = self.run([self.bin_path("amc-mcp"), abix], input=payload)
        if result.returncode != 0:
            self.fail("amc-mcp exited non-zero")
            return False

        responses = {}
        for line in result.stdout.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                document = json.loads(line)
            except ValueError:
                self.fail(f"amc-mcp emitted non-JSON output: {line[:60]}")
                return False
            if "id" in document:
                responses[document["id"]] = document

        def structured(identifier):
            return responses.get(identifier, {}).get("result", {}).get("structuredContent", {})

        ok = True
        if len(responses) != 11:
            ok = False  # notifications must not produce a response
        if responses.get(1, {}).get("result", {}).get("serverInfo", {}).get("name") != "amc-mcp":
            ok = False
        if len(responses.get(2, {}).get("result", {}).get("tools", [])) != 10:
            ok = False
        if structured(3).get("counts", {}).get("types", 0) == 0:
            ok = False
        type_matches = structured(4).get("matches", [])
        if not type_matches or len(type_matches[0].get("fields", [])) != 2:
            ok = False
        function_matches = structured(5).get("matches", [])
        if not function_matches or len(function_matches[0].get("parameters", [])) != 2:
            ok = False
        if structured(6).get("compatible") is not True:
            ok = False
        if structured(9).get("function_count", 0) == 0:
            ok = False
        if structured(10).get("size", 0) == 0 or len(structured(10).get("fields", [])) != 2:
            ok = False
        if structured(11).get("match_count", 0) == 0:
            ok = False
        if responses.get(7, {}).get("result", {}).get("isError") is not True:
            ok = False
        if responses.get(8, {}).get("error", {}).get("code") != -32601:
            ok = False
        if not ok:
            self.fail("amc-mcp protocol/tool responses were invalid")
            return False
        self.pass_("amc-mcp initializes, lists tools and answers ABIX queries")
        return True

    def step24_region_to_module(self) -> bool:
        """Step 24: AMC reads the embedded region back as an AbiModule."""
        if not self.has_binary("clang++"):
            info("clang++ not found, skipping region decoding test")
            return True
        consumer_src = os.path.join(SCRIPT_DIR, "strip_consumer.cpp")
        binary = os.path.join(self.build_dir, "decode_consumer")
        compiled = self.run([
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}", f"-I{self.gen_separate}",
            consumer_src, "-o", binary,
        ])
        if compiled.returncode != 0:
            self.fail("region decoding consumer failed to compile")
            return False

        inspect = self.run([self.bin_path("amc"), "inspect", binary])
        if inspect.returncode != 0 or "types=0" in inspect.stdout or "types=" not in inspect.stdout:
            self.fail("amc inspect could not read the embedded region")
            return False
        self.pass_("amc inspect decodes a binary's Metadata Region")

        query = self.run([self.bin_path("amc"), "query", binary,
                          "--type", "AmcTestFoo", "--layout", "--format", "json"])
        try:
            document = json.loads(query.stdout)
        except ValueError:
            document = None
        fields = []
        if document and document.get("matches"):
            fields = [field["name"] for field in document["matches"][0].get("fields", [])]
        if query.returncode != 0 or fields != ["x", "y"]:
            self.fail("amc query could not resolve names from the embedded region")
            return False
        self.pass_("amc query resolves type + field names from the binary")

        abix = os.path.join(self.gen_separate, "test.abix")
        verify = self.run([self.bin_path("amc"), "verify", binary, abix, "--format", "json"])
        try:
            verdict = json.loads(verify.stdout)
        except ValueError:
            verdict = None
        if not (verify.returncode == 0 and verdict and verdict.get("consistent") is True):
            self.fail("the embedded region does not match the standalone .abix")
            return False
        self.pass_("embedded region and standalone .abix are byte-identical in ABI facts")

        mcp = self.run([self.bin_path("amc-mcp"), binary], input=json.dumps(
            {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
             "params": {"name": "abix.get_module", "arguments": {}}}) + "\n")
        try:
            response = json.loads(mcp.stdout.splitlines()[0])
        except (ValueError, IndexError):
            response = None
        counts = (response or {}).get("result", {}).get("structuredContent", {}).get("counts", {})
        if counts.get("types", 0) == 0:
            self.fail("amc-mcp could not serve a compiled binary")
            return False
        self.pass_("amc-mcp serves a compiled binary directly")
        return True

    def step25_lua_generator(self) -> bool:
        """Step 25: `amc generate -l lua` emits an Aue contract from metadata."""
        abix = os.path.join(self.gen_separate, "test.abix")
        out = os.path.join(self.build_dir, "lua-contract")
        os.makedirs(out, exist_ok=True)
        generated = self.run([self.bin_path("amc"), "generate", abix, "-l", "lua", "-o", out])
        header = os.path.join(out, "amc_lua_contract.hpp")
        if generated.returncode != 0 or not os.path.isfile(header):
            self.fail("amc generate -l lua did not emit a contract header")
            return False
        with open(header) as handle:
            content = handle.read()
        if "aue::Contract" not in content or "amc_test_compute" not in content:
            self.fail("generated Lua contract is missing the ABIX function set")
            return False
        self.pass_("amc generate -l lua emits an Aue contract")

        languages = self.run([self.bin_path("amc"), "--list-languages"])
        if "lua" not in languages.stdout.split():
            self.fail("amc --list-languages does not advertise lua")
            return False
        self.pass_("amc --list-languages advertises lua")

        if not self.has_binary("clang++"):
            info("clang++ not found, skipping the generated contract compile check")
            return True
        source = os.path.join(self.build_dir, "lua_contract_check.cpp")
        with open(source, "w") as handle:
            handle.write(
                '#include "amc_lua_contract.hpp"\n'
                'extern "C" int amc_test_create(lua_State*) { return 0; }\n'
                'extern "C" int amc_test_destroy(lua_State*) { return 0; }\n'
                'extern "C" int amc_test_compute(lua_State*) { return 0; }\n'
                'int main() { return amc_generated::lua_contract.entry_count == 3 ? 0 : 1; }\n')
        binary = os.path.join(self.build_dir, "lua_contract_check")
        compiled = self.run(["clang++", "-std=c++17", f"-I{SOURCE_ROOT}", f"-I{out}",
                             source, "-o", binary])
        if compiled.returncode != 0 or self.run([binary]).returncode != 0:
            self.fail("generated Lua contract failed to compile or validate")
            return False
        self.pass_("generated Lua contract compiles and reports the ABIX entry set")
        return True

    def step26_generated_contract_dispatch(self) -> bool:
        """Step 26: the generated Aue contract drives L0 and L1 identically."""
        if not self.has_binary("clang++"):
            info("clang++ not found, skipping generated-contract dispatch test")
            return True
        abix = os.path.join(self.gen_separate, "test.abix")
        gen = os.path.join(self.build_dir, "generated-contract")
        os.makedirs(gen, exist_ok=True)
        if self.run([self.bin_path("amc"), "generate", abix, "-l", "lua",
                     "-o", gen]).returncode != 0:
            self.fail("could not generate the Aue contract")
            return False

        source = os.path.join(self.build_dir, "generated_dispatch.cpp")
        with open(source, "w") as handle:
            handle.write('''
#include "amc_lua_contract.hpp"
#include "aue/aue.h"
#include <string>

extern "C" int amc_test_create(lua_State*) { return 0; }
extern "C" int amc_test_destroy(lua_State*) { return 0; }
extern "C" int amc_test_compute(lua_State* L) {
    lua_pushinteger(L, luaL_checkinteger(L, 1) + luaL_checkinteger(L, 2));
    return 1;
}

static bool run(bool meta, std::string& out) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    if (meta) aue::register_meta(L, amc_generated::lua_contract);
    else aue::register_direct(L, amc_generated::lua_contract);
    lua_setglobal(L, "mod");
    const char* script =
        "local a = mod.amc_test_create(1)\\n"
        "local b = mod.amc_test_compute(20, 22)\\n"
        "return tostring(a) .. ':' .. tostring(b)\\n";
    if (luaL_loadstring(L, script) != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_close(L);
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, -1, &length);
    out.assign(text != nullptr ? text : "", length);
    lua_close(L);
    return true;
}

int main() {
    std::string l0, l1;
    if (!run(false, l0) || !run(true, l1)) return 2;
    if (l0 != l1) return 3;
    return l0 == "nil:42" ? 0 : 4;
}
''')

        flags = self.run(["pkg-config", "--cflags", "--libs", "lua5.4"])
        lua_flags = flags.stdout.split() if flags.returncode == 0 else ["-llua5.4"]
        binary = os.path.join(self.build_dir, "generated_dispatch")
        compiled = self.run([
            "clang++", "-std=c++17", f"-I{SOURCE_ROOT}", f"-I{gen}", source,
            os.path.join(SOURCE_ROOT, "aue", "aue_layers.cpp"), "-o", binary,
        ] + lua_flags)
        if compiled.returncode != 0:
            self.fail("generated-contract dispatch test failed to compile")
            return False
        if self.run([binary]).returncode != 0:
            self.fail("generated contract did not dispatch identically under L0/L1")
            return False
        self.pass_("generated Aue contract drives L0 and L1 identically")
        return True

    def step27_mcp_demo(self) -> bool:
        """Step 27: run the MCP host/plugin compatibility demo and token harness."""
        tool = os.path.join(SOURCE_ROOT, "tools", "abix_mcp_compat.py")
        if not os.path.isfile(tool):
            self.fail("MCP compatibility demo script is missing")
            return False
        host = os.path.join(self.gen_separate, "test.abix")
        plugin = os.path.join(self.build_dir, "m8.abix")
        if not os.path.isfile(plugin):
            self.fail("MCP demo prerequisite (m8.abix) is missing")
            return False

        same = self.run([sys.executable, tool, "--host", host, "--plugin", host,
                         "--amc-mcp", self.bin_path("amc-mcp")])
        if same.returncode != 0:
            self.fail("MCP demo did not report identical artifacts as compatible")
            return False
        self.pass_("MCP demo reports a matching plugin as compatible")

        drift = self.run([sys.executable, tool, "--host", host, "--plugin", plugin,
                          "--amc-mcp", self.bin_path("amc-mcp")])
        try:
            report = json.loads(drift.stdout)
        except ValueError:
            report = None
        if not (drift.returncode == 1 and report
                and report.get("compatible") is False and report.get("changes")):
            self.fail("MCP demo did not report a mismatched plugin as incompatible")
            return False
        self.pass_("MCP demo reports a mismatched plugin with structured changes")

        token_tool = os.path.join(SOURCE_ROOT, "tools", "abix_token_cost.py")
        measured = self.run([sys.executable, token_tool, "--abix", host,
                             "--amc", self.bin_path("amc"), "--source",
                             os.path.join(SOURCE_ROOT, "amc/tests/fixtures/amc_test_types.hpp")])
        try:
            tokens = json.loads(measured.stdout)
        except ValueError:
            tokens = None
        if not (measured.returncode == 0 and tokens
                and tokens.get("metadata_tokens", 0) > 0 and tokens.get("source_tokens", 0) > 0):
            self.fail("token-cost harness did not measure both views")
            return False
        self.pass_("token-cost harness measures source and metadata views")
        return True

    def step28_generated_lua_conformance(self) -> bool:
        """Step 28: metadata -> generated contract + generated Lua case."""
        build = os.path.join(self.build_dir, "counter-aue")
        os.makedirs(build, exist_ok=True)
        config = os.path.join(SOURCE_ROOT, "aue", "counter.abic.toml")
        if self.run([self.bin_path("amc"), "build", "-c", config, "-B", build]).returncode != 0:
            self.fail("could not build the counter Aue module")
            return False
        counter_abix = os.path.join(build, "build", "counter.abix")
        if not os.path.isfile(counter_abix):
            self.fail("counter Aue module did not produce an .abix")
            return False

        contract = os.path.join(build, "counter_contract.hpp")
        if self.run([self.bin_path("amc"), "generate", counter_abix, "-l", "lua",
                     "-o", contract]).returncode != 0:
            self.fail("could not generate the counter contract")
            return False
        with open(contract) as handle:
            contract_text = handle.read()
        if ('"new", 1, counter_new' not in contract_text
                or '"add", 2, counter_add' not in contract_text):
            self.fail("generated contract did not strip the package prefix")
            return False
        self.pass_("generated contract exposes package-stripped Lua names")

        script = os.path.join(build, "generated_conformance.lua")
        if self.run([self.bin_path("amc"), "generate", counter_abix, "-l", "lua",
                     "-o", script]).returncode != 0 or not os.path.isfile(script):
            self.fail("could not generate the Lua conformance script")
            return False
        with open(script) as handle:
            script_text = handle.read()
        if "counter._abi_hash" not in script_text:
            self.fail("generated conformance script does not check the ABI hash")
            return False

        runner = self.bin_path("abix-conformance")
        if not os.path.isfile(runner):
            info("abix-conformance not built (no Lua), skipping L0/L1 execution")
            return True
        executed = self.run([runner, script])
        if executed.returncode != 0 or "aue-conformance" not in executed.stdout:
            self.fail("metadata-generated Lua case failed under L0/L1")
            return False
        self.pass_("metadata-generated Lua case passes under L0 and L1")
        return True

    def step29_lldb_abix(self) -> bool:
        """Step 29: the LLDB `abix` command queries the embedded region."""
        if not self.has_binary("clang++") or not self.has_binary("lldb"):
            info("clang++/lldb not found, skipping the LLDB integration test")
            return True
        consumer_src = os.path.join(SCRIPT_DIR, "strip_consumer.cpp")
        binary = os.path.join(self.build_dir, "lldb_consumer")
        compiled = self.run([
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}", f"-I{self.gen_separate}",
            consumer_src, "-o", binary,
        ])
        if compiled.returncode != 0:
            self.fail("LLDB integration consumer failed to compile")
            return False

        abix = os.path.join(self.gen_separate, "test.abix")
        module = os.path.join(SOURCE_ROOT, "tools", "lldb_abix.py")
        environment = dict(os.environ, ABIX_AMC=self.bin_path("amc"))
        session = self.run([
            shutil.which("lldb"), "-b",
            "-o", f"command script import {module}",
            "-o", "abix info",
            "-o", "abix type AmcTestFoo",
            "-o", f"abix verify {abix}",
            "-o", "quit",
            binary,
        ], env=environment)
        output = session.stdout
        if '"schema": "abix.metadata/1"' not in output:
            self.fail("LLDB `abix info` did not read the embedded region")
            return False
        if '"name": "AmcTestFoo"' not in output:
            self.fail("LLDB `abix type` did not resolve the type")
            return False
        if '"compatible": true' not in output:
            self.fail("LLDB `abix verify` did not confirm compatibility")
            return False
        self.pass_("LLDB `abix` info/type/verify read the embedded region")

        plugin = os.path.join(self.build_dir, "m8.abix")
        if os.path.isfile(plugin):
            drift = self.run([
                shutil.which("lldb"), "-b",
                "-o", f"command script import {module}",
                "-o", f"abix check AmcTestFoo {plugin}",
                "-o", "quit",
                binary,
            ], env=environment)
            if '"compatible": false' not in drift.stdout:
                self.fail("LLDB `abix check` did not report drift")
                return False
            self.pass_("LLDB `abix check` reports a mismatched plugin")
        return True

    def step30_source_origin(self) -> bool:
        """Step 30: type declaration sites survive build + query."""
        build = os.path.join(self.build_dir, "source-origin")
        os.makedirs(build, exist_ok=True)
        config = os.path.join(SOURCE_ROOT, "amc/tests/fixtures/amc_test.abic.toml")
        if self.run([self.bin_path("amc"), "build", "-c", config, "-B", build]).returncode != 0:
            self.fail("amc build failed for the source-origin test")
            return False
        abix = os.path.join(build, "build", "amc_test.abix")

        result = self.run([self.bin_path("amc"), "query", abix,
                           "--type", "AmcTestFoo", "--format", "json"])
        try:
            document = json.loads(result.stdout)
        except ValueError:
            document = None
        source = (document or {}).get("matches", [{}])[0].get("source", {})
        if not (result.returncode == 0 and "amc_test_types.hpp" in source.get("file", "")
                and source.get("line", 0) > 0 and source.get("column", 0) > 0):
            self.fail("source origin was not captured into the artifact")
            return False
        self.pass_("type declaration sites survive build + query")

        context = self.run([self.bin_path("amc"), "context", abix, "--format", "llm"])
        if "amc_test_types.hpp:" not in context.stdout:
            self.fail("amc context did not render the source origin")
            return False
        self.pass_("amc context renders the declaration site")
        return True

    # ==================================================================
    # Main runner
    # ==================================================================
    def run_all(self) -> int:
        print("=== AMC Integration Test ===")
        print(f"  AMC_BIN:      {self.amc_bin}")
        print(f"  FIXTURES_DIR: {self.fixtures_dir}")
        print(f"  BUILD_DIR:    {self.build_dir}")
        print(f"  GEN_SEPARATE: {self.gen_separate}")
        print(f"  GEN_FULL:     {self.gen_full}")
        print()

        ok = self.step01_frontend()
        if not ok:
            print(f"\n=== Results: {self.passed} passed, {self.failed} failed ===")
            return self.failed

        print()
        self.step02_validate()

        print()
        self.step03_inspect()

        print()
        self.step03b_dump_json()

        print()
        self.step04_generate()

        print()
        self.step05_verify_header_content()

        print()
        self.step06_compile_check()

        print()
        self.step07_build_pipeline()

        print()
        self.step08_m8_metadata()

        print()
        self.step08b_map_private()

        print()
        self.step09_diff_compatibility()

        print()
        self.step10_self_description()

        print()
        self.step11_amc_self_description()

        print()
        self.step12_ipc_capabilities()

        print()
        self.step13_llm_context()

        print()
        self.step14_verify_consistent()

        print()
        self.step15_verify_drift()

        print()
        self.step16_error_schema()

        print()
        self.step17_metadata_region()

        print()
        self.step18_build_meta_sidecar()

        print()
        self.step19_strip_consistency()

        print()
        self.step20_metadata_region_elf()

        print()
        self.step21_query_engine()

        print()
        self.step22_symbol_server()

        print()
        self.step23_mcp_server()

        print()
        self.step24_region_to_module()

        print()
        self.step25_lua_generator()

        print()
        self.step26_generated_contract_dispatch()

        print()
        self.step27_mcp_demo()

        print()
        self.step28_generated_lua_conformance()

        print()
        self.step29_lldb_abix()

        print()
        self.step30_source_origin()

        print()
        print(f"=== Results: {self.passed} passed, {self.failed} failed ===")
        if self.keep_artifacts:
            info(f"retained artifacts: {self.build_dir}")

        return self.failed


def main() -> None:
    parser = argparse.ArgumentParser(description="AMC integration test suite")
    parser.add_argument("amc_bin_dir", help="Directory containing amc executables")
    parser.add_argument("--keep-artifacts", action="store_true",
                        help="Retain temporary build directory on exit")
    args = parser.parse_args()

    runner = TestRunner(amc_bin_dir=args.amc_bin_dir,
                        keep_artifacts=args.keep_artifacts)
    try:
        exit_code = runner.run_all()
    finally:
        runner.cleanup()

    sys.exit(exit_code)


if __name__ == "__main__":
    main()