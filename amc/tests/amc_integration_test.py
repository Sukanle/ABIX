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
        """Return a section listing using canonical ABIX section names.

        Linux uses readelf. macOS is Mach-O, so otool is used and the native
        `__abix_*` section names are rewritten to the canonical `.abix.*`
        spelling so callers stay container-agnostic.
        """
        if sys.platform == "darwin" and shutil.which("otool"):
            result = self.run(["otool", "-l", path])
            if result.returncode != 0:
                return None
            return (result.stdout
                    .replace("__abix_names", ".abix.names")
                    .replace("__abix_metadata", ".abix.metadata"))
        tool = shutil.which("readelf") or shutil.which("llvm-readelf")
        if tool is None:
            return None
        result = self.run([tool, "-S", path])
        return result.stdout if result.returncode == 0 else None

    def section_strip_cmd(self, stripper: str, path: str, canonical: str) -> list[str]:
        """Build the ELF command that removes `canonical` from `path`."""
        return [stripper, "--remove-section=" + canonical, path]

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
        if sys.platform == "darwin":
            # Mach-O `strip` removes symbols, not sections; there is no direct
            # equivalent of `--remove-section` for macOS binaries.
            info("Mach-O has no direct section strip, skipping section removal")
            return True
        if self.run(self.section_strip_cmd(stripper, binary, ".abix.names")).returncode != 0:
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
        if len(responses.get(2, {}).get("result", {}).get("tools", [])) != 12:
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

    def step31_abi_diagnostics(self) -> bool:
        """Step 31: amc verify --format diagnostics emits editor-style lines."""
        build = os.path.join(self.build_dir, "diagnostics")
        os.makedirs(build, exist_ok=True)
        config = os.path.join(SOURCE_ROOT, "amc/tests/fixtures/amc_test.abic.toml")
        if self.run([self.bin_path("amc"), "build", "-c", config, "-B", build]).returncode != 0:
            self.fail("amc build failed for the diagnostics test")
            return False
        contract = os.path.join(build, "build", "amc_test.abix")
        implementation = os.path.join(self.build_dir, "m8.abix")
        if not os.path.isfile(implementation):
            self.fail("diagnostics prerequisite (m8.abix) is missing")
            return False

        result = self.run([self.bin_path("amc"), "verify", contract, implementation,
                           "--format", "diagnostics"])
        if result.returncode != 1:
            self.fail("amc verify --format diagnostics should exit 1 on drift")
            return False
        if "amc_test_types.hpp:" not in result.stdout or "error: ABI " not in result.stdout:
            self.fail("diagnostics output is missing file:line:column / error lines")
            return False
        self.pass_("amc verify --format diagnostics emits file:line: error lines")

        lines = [line for line in result.stdout.splitlines() if line.strip()]
        if len(lines) < 2 or not any("amc_test_types.hpp:" not in line for line in lines):
            self.fail("diagnostics did not report source-less drift changes")
            return False
        self.pass_("diagnostics report every drift change, with or without a source")
        return True

    def step32_go_to_definition(self) -> bool:
        """Step 32: LLDB go-to-definition via BuildID -> symbol server -> .abix."""
        if not (self.has_binary("clang++") and self.has_binary("lldb")
                and self.has_binary("readelf")):
            info("clang++/lldb/readelf not found, skipping go-to-definition test")
            return True
        # A binary carrying the embedded (source-free) Metadata Region.
        consumer_src = os.path.join(SCRIPT_DIR, "strip_consumer.cpp")
        binary = os.path.join(self.build_dir, "gotodef_consumer")
        compiled = self.run([
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}", f"-I{self.gen_separate}",
            consumer_src, "-o", binary, "-Wl,--build-id=sha1",
        ])
        if compiled.returncode != 0:
            self.fail("go-to-definition consumer failed to compile")
            return False

        notes = self.run(["readelf", "-n", binary])
        match = re.search(r"BuildID:\s*([0-9a-f]+)", notes.stdout.replace("Build ID:", "BuildID:"))
        if match is None:
            self.fail("go-to-definition binary has no GNU build id")
            return False
        build_id = match.group(1)

        # The debug .abix (with Source Origin) published under the binary's BuildID.
        build = os.path.join(self.build_dir, "gotodef-abi")
        os.makedirs(build, exist_ok=True)
        config = os.path.join(SOURCE_ROOT, "amc/tests/fixtures/amc_test.abic.toml")
        if self.run([self.bin_path("amc"), "build", "-c", config, "-B", build]).returncode != 0:
            self.fail("go-to-definition .abix build failed")
            return False
        debug_abix = os.path.join(build, "build", "amc_test.abix")
        store = os.path.join(self.build_dir, "gotodef-store")
        if self.run([self.bin_path("amc"), "publish", debug_abix,
                     "--root", store, "--build-id", build_id]).returncode != 0:
            self.fail("could not publish the debug .abix to the symbol server")
            return False

        module = os.path.join(SOURCE_ROOT, "tools", "lldb_abix.py")
        environment = dict(os.environ,
                           ABIX_AMC=self.bin_path("amc"),
                           ABIX_SYMBOL_STORE=store)
        session = self.run([
            shutil.which("lldb"), "-b",
            "-o", f"command script import {module}",
            "-o", "abix source AmcTestFoo",
            "-o", "quit",
            binary,
        ], env=environment)
        if "amc_test_types.hpp:" not in session.stdout:
            self.fail("LLDB `abix source` did not resolve go-to-definition")
            return False
        self.pass_("LLDB `abix source` resolves TypeID -> declaration site")
        return True

    def step33_lldb_cast(self) -> bool:
        """Step 33: LLDB `abix cast` interprets memory via AMC layout data."""
        if not (self.has_binary("clang++") and self.has_binary("lldb")):
            info("clang++/lldb not found, skipping the LLDB cast test")
            return True
        source = os.path.join(self.build_dir, "cast_prog.cpp")
        with open(source, "w") as handle:
            handle.write(
                '#include "amc_generated.hpp"\n'
                '#include "amc_test_types.hpp"\n'
                'AmcTestFoo g_foo{7, 2.5};\n'
                'int main() { return g_foo.x == 7 ? 0 : 1; }\n')
        binary = os.path.join(self.build_dir, "cast_prog")
        fixtures = os.path.join(SOURCE_ROOT, "amc/tests/fixtures")
        compiled = self.run([
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}", f"-I{self.gen_separate}", f"-I{fixtures}",
            source, "-o", binary,
        ])
        if compiled.returncode != 0:
            self.fail("LLDB cast program failed to compile")
            return False

        module = os.path.join(SOURCE_ROOT, "tools", "lldb_abix.py")
        environment = dict(os.environ, ABIX_AMC=self.bin_path("amc"))
        session = self.run([
            shutil.which("lldb"), "-b",
            "-o", f"command script import {module}",
            "-o", "breakpoint set -n main",
            "-o", "run",
            "-o", "abix cast &g_foo AmcTestFoo",
            "-o", "quit",
            binary,
        ], env=environment)
        output = session.stdout
        if "abix cast: AmcTestFoo @" not in output:
            self.fail("LLDB `abix cast` did not resolve the layout")
            return False
        if "2.5" not in output or "7" not in output:
            self.fail("LLDB `abix cast` did not decode field values")
            return False
        self.pass_("LLDB `abix cast` interprets memory via ABIX layout")
        return True

    def step34_abi_adapter(self) -> bool:
        """Step 34: `amc adapter` generates a working ABI conversion."""
        build = os.path.join(self.build_dir, "adapter")
        os.makedirs(build, exist_ok=True)
        fixtures = os.path.join(SOURCE_ROOT, "amc/tests/fixtures")
        for name in ("map_v1", "map_v2"):
            config = os.path.join(fixtures, f"{name}.abic.toml")
            if self.run([self.bin_path("amc"), "build", "-c", config,
                         "-B", build]).returncode != 0:
                self.fail(f"amc build {name} failed for the adapter test")
                return False
        v1 = os.path.join(build, "build", "map_v1.abix")
        v2 = os.path.join(build, "build", "map_v2.abix")

        adapter = os.path.join(build, "adapter.hpp")
        generated = self.run([self.bin_path("amc"), "adapter", v1, v2, "-o", adapter])
        if generated.returncode != 0 or not os.path.isfile(adapter):
            self.fail("amc adapter did not generate a file")
            return False
        with open(adapter) as handle:
            text = handle.read()
        if ("copy_field" not in text or "add_default" not in text
                or "abix_adapter" not in text):
            self.fail("generated adapter is missing the mapping")
            return False
        self.pass_("amc adapter generates a mapping from two artifacts")

        if not self.has_binary("clang++"):
            info("clang++ not found, skipping the adapter compile check")
            return True
        source = os.path.join(build, "adapter_check.cpp")
        with open(source, "w") as handle:
            handle.write(
                '#include "adapter.hpp"\n'
                'struct V1 { int value; };\n'
                'struct V2 { int value; int added; };\n'
                'int main() {\n'
                '    V1 a{42}; V2 b{9, 9};\n'
                '    const auto *e = abix_adapter::find("amc_map::MapRecord",'
                ' "amc_map::MapRecord");\n'
                '    if (!e) return 1;\n'
                '    e->apply(&a, &b);\n'
                '    return (b.value == 42 && b.added == 0) ? 0 : 2;\n'
                '}\n')
        binary = os.path.join(build, "adapter_check")
        compiled = self.run(["clang++", "-std=c++17", f"-I{build}",
                             source, "-o", binary])
        if compiled.returncode != 0 or self.run([binary]).returncode != 0:
            self.fail("generated adapter failed to compile or apply")
            return False
        self.pass_("generated adapter applies the field mapping correctly")

        # A conversion map: int -> long and float -> double.
        for name in ("map_conv_v1", "map_conv_v2"):
            config = os.path.join(fixtures, f"{name}.abic.toml")
            if self.run([self.bin_path("amc"), "build", "-c", config,
                         "-B", build]).returncode != 0:
                self.fail(f"amc build {name} failed for the conversion test")
                return False
        conv_adapter = os.path.join(build, "convert.hpp")
        if self.run([self.bin_path("amc"), "adapter",
                     os.path.join(build, "build", "map_conv_v1.abix"),
                     os.path.join(build, "build", "map_conv_v2.abix"),
                     "-o", conv_adapter]).returncode != 0:
            self.fail("amc adapter failed for the conversion fixture")
            return False
        with open(conv_adapter) as handle:
            converted = handle.read()
        if "convert_int" not in converted or "convert_float" not in converted:
            self.fail("conversion adapter is missing convert_int / convert_float")
            return False
        self.pass_("amc adapter generates integer and float conversions")

        conv_source = os.path.join(build, "convert_check.cpp")
        with open(conv_source, "w") as handle:
            handle.write(
                '#include "convert.hpp"\n'
                'struct V1 { int a; float b; };\n'
                'struct V2 { long a; double b; };\n'
                'int main() {\n'
                '    V1 x{7, 1.5f}; V2 y{};\n'
                '    const auto *e = abix_adapter::find("amc_map::MapConv",'
                ' "amc_map::MapConv");\n'
                '    if (!e) return 1;\n'
                '    e->apply(&x, &y);\n'
                '    return (y.a == 7 && y.b == 1.5) ? 0 : 2;\n'
                '}\n')
        conv_binary = os.path.join(build, "convert_check")
        compiled = self.run(["clang++", "-std=c++17", f"-I{build}",
                             conv_source, "-o", conv_binary])
        if compiled.returncode != 0 or self.run([conv_binary]).returncode != 0:
            self.fail("generated conversion adapter failed to compile or convert")
            return False
        self.pass_("generated conversion adapter widens int and float correctly")

        # Typed specialization over the C++ projection.
        typed_header = os.path.join(build, "typed.hpp")
        if self.run([self.bin_path("amc"), "adapter", v1, v2,
                     "--typed", "-o", typed_header]).returncode != 0:
            self.fail("amc adapter --typed failed")
            return False
        with open(typed_header) as handle:
            typed_text = handle.read()
        if "::abix::adapter<" not in typed_text or "ABIX_ADAPTER_TYPED" not in typed_text:
            self.fail("typed adapter is missing the abix::adapter specialization")
            return False
        self.pass_("amc adapter --typed emits an abix::adapter specialization")

        projection = os.path.join(build, "projection")
        if self.run([self.bin_path("amc"), "generate", v1, "-l", "cpp",
                     "-o", projection]).returncode != 0:
            self.fail("could not generate the C++ projection for the typed adapter")
            return False
        typed_source = os.path.join(build, "typed_check.cpp")
        with open(typed_source, "w") as handle:
            handle.write(
                '#define ABIX_ADAPTER_TYPED 1\n'
                '#include "projection/amc_generated.hpp"\n'
                '#include "typed.hpp"\n'
                'struct V1 { int value; };\n'
                'struct V2 { int value; int added; };\n'
                'int main() {\n'
                '    V1 a{42}; V2 b{9, 9};\n'
                '    using ad = abix::adapter<amc_generated::amc_map_MapRecord_ABIX,\n'
                '                             amc_generated::amc_map_MapRecord_ABIX>;\n'
                '    if (!ad::apply(&b, &a)) return 1;\n'
                '    return (b.value == 42 && b.added == 0) ? 0 : 2;\n'
                '}\n')
        typed_binary = os.path.join(build, "typed_check")
        compiled = self.run(["clang++", "-std=c++17", f"-I{SOURCE_ROOT}", f"-I{build}",
                             typed_source, "-o", typed_binary])
        if compiled.returncode != 0 or self.run([typed_binary]).returncode != 0:
            self.fail("typed adapter failed to compile or apply")
            return False
        self.pass_("typed abix::adapter specialization applies the mapping")
        return True

    def step35_abi_shim(self) -> bool:
        """Step 35: `amc adapter --shim` builds a loadable shim library."""
        if not (self.has_binary("clang++") and self.has_binary("clang")):
            info("clang++/clang not found, skipping the ABI shim test")
            return True
        build = os.path.join(self.build_dir, "shim")
        os.makedirs(build, exist_ok=True)
        fixtures = os.path.join(SOURCE_ROOT, "amc/tests/fixtures")
        for name in ("map_v1", "map_v2"):
            config = os.path.join(fixtures, f"{name}.abic.toml")
            if self.run([self.bin_path("amc"), "build", "-c", config,
                         "-B", build]).returncode != 0:
                self.fail(f"amc build {name} failed for the shim test")
                return False
        v1 = os.path.join(build, "build", "map_v1.abix")
        v2 = os.path.join(build, "build", "map_v2.abix")

        shim_cpp = os.path.join(build, "shim.cpp")
        if self.run([self.bin_path("amc"), "adapter", v1, v2, "--shim",
                     "-o", shim_cpp]).returncode != 0:
            self.fail("amc adapter --shim did not generate a source file")
            return False
        with open(shim_cpp) as handle:
            text = handle.read()
        if "abix_adapter_apply" not in text or "#pragma once" in text:
            self.fail("shim source is not a translation unit exposing a C ABI")
            return False
        self.pass_("amc adapter --shim emits a C ABI translation unit")

        library = os.path.join(build, "libabix_shim.so")
        compiled = self.run(["clang++", "-std=c++17", "-shared", "-fPIC",
                             f"-I{SOURCE_ROOT}", shim_cpp, "-o", library])
        if compiled.returncode != 0:
            self.fail("shim library failed to compile")
            return False

        host_source = os.path.join(build, "host.c")
        with open(host_source, "w") as handle:
            handle.write(
                '#include <dlfcn.h>\n'
                'struct V1 { int value; };\n'
                'struct V2 { int value; int added; };\n'
                'typedef int (*count_fn)(void);\n'
                'typedef int (*apply_fn)(const char*, const char*, const void*, void*);\n'
                'int main(void) {\n'
                '    void *h = dlopen("./libabix_shim.so", RTLD_NOW);\n'
                '    if (!h) return 1;\n'
                '    count_fn count = (count_fn)dlsym(h, "abix_adapter_count");\n'
                '    apply_fn apply = (apply_fn)dlsym(h, "abix_adapter_apply");\n'
                '    if (!count || !apply || count() != 1) return 2;\n'
                '    struct V1 a = {42}; struct V2 b = {9, 9};\n'
                '    if (!apply("amc_map::MapRecord", "amc_map::MapRecord", &a, &b)) return 3;\n'
                '    return (b.value == 42 && b.added == 0) ? 0 : 4;\n'
                '}\n')
        host = os.path.join(build, "host")
        if self.run(["clang", host_source, "-o", host, "-ldl"]).returncode != 0:
            self.fail("shim host failed to compile")
            return False
        if self.run([host], cwd=build).returncode != 0:
            self.fail("shim library did not apply the mapping across the C ABI")
            return False
        self.pass_("shim library applies the mapping across the C ABI")
        return True

    def step36_abi_check_header(self) -> bool:
        """Step 36: the generated clangd-visible ABI check header."""
        check_header = os.path.join(self.gen_separate, "amc_abi_check.hpp")
        if not os.path.isfile(check_header):
            self.fail("amc generate did not emit amc_abi_check.hpp")
            return False
        with open(check_header) as handle:
            text = handle.read()
        if ("ABIX ABI check" not in text
                or "sizeof(AmcTestFoo)" not in text
                or "offsetof(AmcTestFoo, y)" not in text
                or "field width mismatch for AmcTestFoo::x" not in text):
            self.fail("ABI check header is missing the type/field/width assertions")
            return False
        self.pass_("amc generate emits a clangd-visible ABI check header")

        if not self.has_binary("clang++"):
            info("clang++ not found, skipping the ABI check compile test")
            return True

        check_dir = os.path.join(self.build_dir, "abi-check")
        os.makedirs(check_dir, exist_ok=True)
        include = ["-I" + SOURCE_ROOT, "-I" + self.gen_separate,
                   "-I" + self.fixtures_dir]

        # A native declaration that matches the contract must compile cleanly.
        ok_source = os.path.join(check_dir, "abi_check_ok.cpp")
        with open(ok_source, "w") as handle:
            handle.write('#include "amc_test_types.hpp"\n'
                         '#include "amc_abi_check.hpp"\n'
                         'int main() { return 0; }\n')
        compiled = self.run(["clang++", "-std=c++17"] + include
                            + [ok_source, "-o", os.path.join(check_dir, "abi_check_ok")])
        if compiled.returncode != 0:
            self.fail("a matching native declaration failed the ABI check header")
            return False
        self.pass_("the ABI check header accepts a matching native declaration")

        # A drifted declaration (extra field) must be rejected at compile time,
        # which is exactly what clangd surfaces while editing.
        drift_header = os.path.join(check_dir, "abi_check_drift.hpp")
        with open(drift_header, "w") as handle:
            handle.write('#pragma once\n#include <cstdint>\n'
                         'struct AmcTestFoo { int32_t x; double y; int32_t z; };\n'
                         'struct AmcTestBar { int32_t a; double b; int32_t c; };\n'
                         'enum class AmcTestColor : int32_t { Red = 0, Green = 1, Blue = 2 };\n')
        drift_source = os.path.join(check_dir, "abi_check_drift.cpp")
        with open(drift_source, "w") as handle:
            handle.write('#include "abi_check_drift.hpp"\n'
                         '#include "amc_abi_check.hpp"\n'
                         'int main() { return 0; }\n')
        drifted = self.run(["clang++", "-std=c++17"] + include
                           + [drift_source, "-o", os.path.join(check_dir, "abi_check_drift")])
        if drifted.returncode == 0 or "ABIX ABI check: size mismatch for AmcTestFoo" not in drifted.stderr:
            self.fail("a drifted native layout was not rejected by the ABI check header")
            return False
        self.pass_("the ABI check header rejects a drifted native layout")

        # A field widened to a larger type moves neither the following offsets
        # nor the total size, so only the width assertion can catch it -- this
        # is the part of the LayoutHash comparison the offset checks miss.
        width_header = os.path.join(check_dir, "abi_check_width.hpp")
        with open(width_header, "w") as handle:
            handle.write('#pragma once\n#include <cstdint>\n'
                         'struct AmcTestFoo { long x; double y; };\n'
                         'struct AmcTestBar { int32_t a; double b; int32_t c; };\n'
                         'enum class AmcTestColor : int32_t { Red = 0, Green = 1, Blue = 2 };\n')
        width_source = os.path.join(check_dir, "abi_check_width.cpp")
        with open(width_source, "w") as handle:
            handle.write('#include "abi_check_width.hpp"\n'
                         '#include "amc_abi_check.hpp"\n'
                         'int main() { return 0; }\n')
        widened = self.run(["clang++", "-std=c++17"] + include
                           + [width_source, "-o", os.path.join(check_dir, "abi_check_width")])
        if (widened.returncode == 0
                or "ABIX ABI check: field width mismatch for AmcTestFoo::x" not in widened.stderr
                or "ABIX ABI check: size mismatch for AmcTestFoo" in widened.stderr):
            self.fail("a widened field was not rejected by the field-width assertion")
            return False
        self.pass_("the ABI check header rejects a widened field by width")
        return True

    def step37_mcp_knowledge_base(self) -> bool:
        """Step 37: amc-mcp indexes multiple artifacts as a knowledge base."""
        mcp = self.bin_path("amc-mcp")
        if not os.path.isfile(mcp):
            info("amc-mcp not built, skipping the knowledge-base test")
            return True
        first = os.path.join(self.gen_separate, "test.abix")
        kb = os.path.join(self.build_dir, "kb")
        if self.run([self.bin_path("amc"), "build", "-c", self.fixture_path("amc_test.abic.toml"),
                     "-B", kb]).returncode != 0:
            self.fail("amc build failed for the knowledge-base test")
            return False
        second = os.path.join(kb, "build", "amc_test.abix")
        if not (os.path.isfile(first) and os.path.isfile(second)):
            self.fail("knowledge-base artifacts were not produced")
            return False

        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
             "params": {"name": "abix.list_modules", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
             "params": {"name": "abix.search_type", "arguments": {"name": "AmcTestFoo"}}},
            {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
             "params": {"name": "abix.find_compatible", "arguments": {"name": "AmcTestFoo"}}},
        ]
        payload = "\n".join(json.dumps(request) for request in requests) + "\n"
        session = self.run([mcp, first, second], input=payload)
        if session.returncode != 0:
            self.fail("amc-mcp exited non-zero for the knowledge-base test")
            return False
        responses = {}
        for line in session.stdout.splitlines():
            try:
                document = json.loads(line)
            except ValueError:
                continue
            if isinstance(document, dict) and "id" in document:
                responses[document["id"]] = document

        modules = responses.get(1, {}).get("result", {}).get("structuredContent", {})
        if modules.get("module_count") != 2:
            self.fail("abix.list_modules did not report both indexed modules")
            return False
        search = responses.get(2, {}).get("result", {}).get("structuredContent", {})
        groups = {group.get("name"): group for group in search.get("groups", [])}
        foo = groups.get("AmcTestFoo")
        if search.get("group_count", 0) < 1 or foo is None or not foo.get("consistent"):
            self.fail("abix.search_type did not find a consistent AmcTestFoo group")
            return False
        compatible = responses.get(3, {}).get("result", {}).get("structuredContent", {})
        if not compatible.get("found") or not compatible.get("compatible"):
            self.fail("abix.find_compatible did not confirm knowledge-base compatibility")
            return False
        self.pass_("amc-mcp indexes multiple artifacts and answers cross-module queries")
        return True

    def step38_lldb_plugin(self) -> bool:
        """Step 38: the native C++ LLDB plugin linked against libabix-*."""
        plugin = self.bin_path("libabix_lldb.so")
        if not os.path.isfile(plugin):
            info("the native LLDB plugin was not built, skipping")
            return True
        if not (self.has_binary("clang++") and self.has_binary("lldb")):
            info("clang++/lldb not found, skipping the native LLDB plugin test")
            return True

        consumer_src = os.path.join(SCRIPT_DIR, "strip_consumer.cpp")
        binary = os.path.join(self.build_dir, "lldb_plugin_consumer")
        compiled = self.run([
            "clang++", "-std=c++17",
            f"-I{SOURCE_ROOT}", f"-I{self.gen_separate}",
            consumer_src, "-o", binary,
        ])
        if compiled.returncode != 0:
            self.fail("native LLDB plugin consumer failed to compile")
            return False

        abix = os.path.join(self.gen_separate, "test.abix")
        session = self.run([
            shutil.which("lldb"), "-b",
            "-o", f"plugin load {plugin}",
            "-o", "abix info",
            "-o", "abix type AmcTestFoo",
            "-o", f"abix verify {abix}",
            "-o", "quit",
            binary,
        ])
        output = session.stdout
        if "abix info:" not in output or "AmcTestFoo" not in output:
            self.fail("the native LLDB plugin did not answer info/type")
            return False
        if "compatible=true" not in output:
            self.fail("the native LLDB plugin did not confirm compatibility")
            return False
        self.pass_("the native C++ LLDB plugin answers info/type/verify")
        return True

    def step39_mcp_protocol(self) -> bool:
        """Step 39: frozen MCP protocol — version negotiation, tool schemas,
        target/function name fields, TypeID resolution, notifications, the CLI
        surface and cross-module consistency flags."""
        mcp = self.bin_path("amc-mcp")
        if not os.path.isfile(mcp):
            info("amc-mcp not built, skipping the MCP protocol test")
            return True
        abix = os.path.join(self.gen_separate, "test.abix")

        problems: list[str] = []

        def require(condition: bool, message: str) -> None:
            if not condition:
                problems.append(message)

        def parse_documents(output: str) -> tuple[list, dict]:
            documents = []
            for line in output.splitlines():
                line = line.strip()
                if not line:
                    continue
                try:
                    document = json.loads(line)
                except ValueError:
                    continue
                documents.append(document)
            responses = {
                document["id"]: document
                for document in documents
                if isinstance(document, dict) and document.get("id") is not None
            }
            return documents, responses

        # --- session 1: negotiation, tool schemas, target/function names ---
        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {"protocolVersion": "2025-06-18"}},
            {"jsonrpc": "2.0", "id": 2, "method": "initialize",
             "params": {"protocolVersion": "2024-11-05"}},
            {"jsonrpc": "2.0", "id": 3, "method": "initialize",
             "params": {"protocolVersion": "9999-99-99"}},
            {"jsonrpc": "2.0", "id": 4, "method": "tools/list"},
            {"jsonrpc": "2.0", "id": 5, "method": "tools/call",
             "params": {"name": "abix.get_module", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 6, "method": "tools/call",
             "params": {"name": "abix.list_types", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 7, "method": "tools/call",
             "params": {"name": "abix.get_type", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 8, "method": "tools/call",
             "params": {"name": "abix.get_layout", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 9, "method": "tools/call",
             "params": {"name": "abix.list_functions", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 10, "method": "tools/call",
             "params": {"name": "abix.find_compatible",
                        "arguments": {"name": "AmcTestFoo"}}},
        ]
        lines = [json.dumps(request) for request in requests]
        lines.append('{"jsonrpc": "2.0", "id": 30, "method": "tools/list"')  # malformed
        lines.append(json.dumps({"jsonrpc": "2.0",
                                 "method": "notifications/initialized"}))
        session = self.run([mcp, abix], input="\n".join(lines) + "\n")
        if session.returncode != 0:
            self.fail("amc-mcp exited non-zero for the protocol session")
            return False
        documents, responses = parse_documents(session.stdout)

        init = responses.get(1, {}).get("result", {})
        require(init.get("protocolVersion") == "2025-06-18",
                "initialize did not echo the supported 2025-06-18 protocol version")
        require(init.get("serverInfo") == {"name": "amc-mcp", "version": "1.0.0"},
                "initialize did not report serverInfo name=amc-mcp version=1.0.0")
        require(responses.get(2, {}).get("result", {}).get("protocolVersion") == "2024-11-05",
                "initialize did not echo the supported 2024-11-05 protocol version")
        require(responses.get(3, {}).get("result", {}).get("protocolVersion") == "2025-06-18",
                "initialize did not fall back to 2025-06-18 for an unknown protocol version")

        tools = responses.get(4, {}).get("result", {}).get("tools", [])
        schemas = {tool.get("name"): tool.get("inputSchema", {})
                   for tool in tools if isinstance(tool, dict)}
        name_or_id = [{"required": ["name"]}, {"required": ["id"]}]
        for tool_name in ("abix.get_type", "abix.get_layout",
                          "abix.resolve_type", "abix.search_type"):
            require(schemas.get(tool_name, {}).get("anyOf") == name_or_id,
                    f"{tool_name} schema does not accept name-or-id via anyOf")
        require(schemas.get("abix.get_function", {}).get("required") == ["name"],
                "abix.get_function schema does not require 'name'")

        target = (responses.get(5, {}).get("result", {})
                  .get("structuredContent", {}).get("target", {}))
        require(target.get("arch") == 0 and target.get("arch_name") == "x86_64",
                "abix.get_module target does not name arch 0 as x86_64")
        require(target.get("os") == 0 and target.get("os_name") == "linux",
                "abix.get_module target does not name os 0 as linux")
        require(target.get("compiler") == 1 and target.get("compiler_name") == "clang",
                "abix.get_module target does not name compiler 1 as clang")
        require(target.get("calling_convention") == 0
                and target.get("calling_convention_name") == "sysv_abi",
                "abix.get_module target does not name calling convention 0 as sysv_abi")
        require("abi_name" not in target,
                "abix.get_module target exposes an abi_name field that must not exist")

        require(responses.get(7, {}).get("result", {}).get("isError") is True,
                "abix.get_type with empty arguments did not report isError")
        require(responses.get(8, {}).get("result", {}).get("isError") is True,
                "abix.get_layout with empty arguments did not report isError")

        functions = (responses.get(9, {}).get("result", {})
                     .get("structuredContent", {}).get("functions", []))
        require(len(functions) >= 1, "abix.list_functions returned no functions")
        require(all(isinstance(item, dict)
                    and isinstance(item.get("calling_convention_name"), str)
                    for item in functions),
                "abix.list_functions items are missing calling_convention_name")

        compatible = (responses.get(10, {}).get("result", {})
                      .get("structuredContent", {}))
        require(isinstance(compatible.get("reason"), str),
                "abix.find_compatible on a single-module server did not report a reason")

        error_codes = [document.get("error", {}).get("code") for document in documents
                       if isinstance(document, dict) and "error" in document]
        require(-32700 in error_codes,
                "malformed JSON did not produce a -32700 parse error response")
        require(len(documents) == 11,
                "a notification produced a response line (expected none)")
        for identifier in range(1, 11):
            require(identifier in responses, f"no response for request id {identifier}")

        types = (responses.get(6, {}).get("result", {})
                 .get("structuredContent", {}).get("types", []))
        full_id = ""
        if types and isinstance(types[0], dict):
            candidate = types[0].get("id")
            if isinstance(candidate, str) and len(candidate) >= 34 \
                    and candidate.startswith("0x"):
                full_id = candidate
        require(bool(full_id), "abix.list_types did not expose a reusable full TypeID")

        # --- session 2: full and partial TypeID resolution ---
        partial_id = full_id[:-4]
        id_requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
             "params": {"name": "abix.get_type", "arguments": {"id": full_id}}},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
             "params": {"name": "abix.resolve_type", "arguments": {"id": partial_id}}},
        ]
        id_session = self.run([mcp, abix],
                              input="\n".join(json.dumps(r) for r in id_requests) + "\n")
        if id_session.returncode != 0:
            self.fail("amc-mcp exited non-zero for the TypeID session")
            return False
        _, id_responses = parse_documents(id_session.stdout)
        require(id_responses.get(1, {}).get("result", {}).get("structuredContent", {})
                .get("match_count", 0) >= 1,
                "abix.get_type did not resolve a full TypeID")
        require(id_responses.get(2, {}).get("result", {}).get("structuredContent", {})
                .get("match_count", 0) >= 1,
                "abix.resolve_type did not resolve a partial TypeID")

        # --- CLI surface ---
        listed = self.run([mcp, "--list-tools"])
        require(listed.returncode == 0 and self.grep(listed.stdout, "abix.get_module"),
                "amc-mcp --list-tools did not exit 0 with abix.get_module listed")
        indexed = self.run(
            [mcp, "--index", abix],
            input=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                              "params": {"name": "abix.list_modules", "arguments": {}}})
            + "\n")
        _, index_responses = parse_documents(indexed.stdout)
        require(indexed.returncode == 0
                and index_responses.get(1, {}).get("result", {})
                .get("structuredContent", {}).get("module_count") == 1,
                "amc-mcp --index did not index exactly one module")
        bogus = self.run([mcp, "--bogus"])
        require(bogus.returncode == 2, "amc-mcp --bogus did not exit with code 2")

        # --- cross-module search: map_v1 and map_v2 disagree on MapRecord ---
        map_build = os.path.join(self.build_dir, "mcp-protocol-map")
        for name in ("map_v1", "map_v2"):
            config = self.fixture_path(f"{name}.abic.toml")
            if self.run([self.bin_path("amc"), "build", "-c", config,
                         "-B", map_build]).returncode != 0:
                self.fail(f"amc build {name} failed for the cross-module protocol test")
                return False
        v1 = os.path.join(map_build, "build", "map_v1.abix")
        v2 = os.path.join(map_build, "build", "map_v2.abix")
        if not (os.path.isfile(v1) and os.path.isfile(v2)):
            self.fail("cross-module artifacts were not produced")
            return False
        search_session = self.run(
            [mcp, v1, v2],
            input=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                              "params": {"name": "abix.search_type",
                                         "arguments": {"name": "MapRecord"}}})
            + "\n")
        if search_session.returncode != 0:
            self.fail("amc-mcp exited non-zero for the cross-module search")
            return False
        _, search_responses = parse_documents(search_session.stdout)
        groups = (search_responses.get(1, {}).get("result", {})
                  .get("structuredContent", {}).get("groups", []))
        map_groups = [group for group in groups
                      if isinstance(group, dict)
                      and isinstance(group.get("name"), str)
                      and group.get("name").endswith("MapRecord")]
        require(len(map_groups) >= 1,
                "abix.search_type did not return a MapRecord group")
        require(any(group.get("consistent") is False for group in map_groups),
                "abix.search_type did not flag the MapRecord group as inconsistent")

        if problems:
            for message in problems:
                self.fail(message)
            return False
        self.pass_("amc-mcp honours the frozen MCP protocol and CLI surface")
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
        self.step31_abi_diagnostics()

        print()
        self.step32_go_to_definition()

        print()
        self.step33_lldb_cast()

        print()
        self.step34_abi_adapter()

        print()
        self.step35_abi_shim()

        print()
        self.step36_abi_check_header()

        print()
        self.step37_mcp_knowledge_base()

        print()
        self.step38_lldb_plugin()

        print()
        self.step39_mcp_protocol()

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