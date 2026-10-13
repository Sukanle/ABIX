#!/usr/bin/env python3
"""Shared provider conformance harness for AMC language providers.

A run exercises the producer/consumer contract from `LANGUAGE-PLUGIN.md` §10
against one `amc-<language>` provider:

    * provider discovery (`--list-languages` / `--describe-language`);
    * source-level extraction -> `.abix` (`amc build`) and layout query;
    * projection generation (`amc generate -l <language>`) and, when the target
      toolchain is available, compilation of the projection;
    * artifact determinism (byte-identical builds).

Everything language-specific is supplied by a `ProviderCase`; the harness adds
no language-specific knowledge of its own. Adding a provider means adding a
case (fixtures + expected ABI facts), not copying this file.
"""

import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from typing import Callable, Optional, Sequence, Tuple


# (query type name, human description, substrings required in `amc query` output)
LayoutCheck = Tuple[str, str, Sequence[str]]
# (human description, substrings required in the generated projection)
ProjectionCheck = Tuple[str, Sequence[str]]
# (toolchain path, projection path, output dir) -> completed compilation
ProjectionCompiler = Callable[[str, str, str], "subprocess.CompletedProcess[str]"]


@dataclass
class ProviderCase:
    """The data a provider contributes to a conformance run."""

    language: str
    config: str
    artifact_name: str
    expected_functions: int
    layout_checks: Sequence[LayoutCheck] = ()
    projection_name: str = ""
    projection_checks: Sequence[ProjectionCheck] = ()
    generate_language: Optional[str] = None
    compile_tool: Optional[str] = None
    compile: Optional[ProjectionCompiler] = None
    determinism: bool = True
    work_dir: Optional[str] = None

    def language_title(self) -> str:
        return self.language[:1].upper() + self.language[1:]

    def generate_lang(self) -> str:
        return self.generate_language or self.language


@dataclass
class _Result:
    passed: int = 0
    failed: int = 0


class ProviderConformance:
    """Drives one `ProviderCase` and scores each step PASS/FAIL."""

    def __init__(self, amc_bin_dir: str, case: ProviderCase) -> None:
        self.case = case
        self.amc = os.path.join(os.path.abspath(amc_bin_dir), "amc")
        self.work = case.work_dir or tempfile.mkdtemp(prefix=f"amc-{case.language}.")
        self.result = _Result()

    # -- output helpers ----------------------------------------------------

    def info(self, message: str) -> None:
        print(f"  [INFO] {message}")

    def pass_(self, message: str) -> None:
        self.result.passed += 1
        print(f"  [PASS] {message}")

    def fail(self, message: str) -> None:
        self.result.failed += 1
        print(f"  [FAIL] {message}")

    def check(self, condition: bool, message: str) -> None:
        self.pass_(message) if condition else self.fail(message)

    def run(self, args) -> "subprocess.CompletedProcess[str]":
        return subprocess.run(args, capture_output=True, text=True)

    # -- steps -------------------------------------------------------------

    def test_describe(self) -> None:
        lang = self.case.language
        result = self.run([self.amc, "--describe-language", lang])
        self.check(result.returncode == 0 and "protocol=jsonl-v1" in result.stdout,
                   f"amc --describe-language {lang} advertises the JSONL protocol")
        listed = self.run([self.amc, "--list-languages"])
        self.check(lang in listed.stdout.split(),
                   f"amc --list-languages advertises {lang}")

    def test_build_and_query(self) -> str:
        case = self.case
        build = os.path.join(self.work, "build")
        result = self.run([self.amc, "build", "-c", case.config, "-B", build])
        artifact = os.path.join(build, case.artifact_name)
        if result.returncode != 0 or not os.path.isfile(artifact):
            self.fail(f"amc build produced the {case.language_title()} artifact "
                      f"({result.stderr.strip()})")
            return artifact
        self.pass_(f"amc build produced the {case.language_title()} artifact")

        inspect = self.run([self.amc, "inspect", artifact])
        self.check(inspect.returncode == 0 and
                   f"functions={case.expected_functions}" in inspect.stdout,
                   f"the artifact records the {case.expected_functions} extern functions")

        layouts = {}
        for type_name, description, substrings in case.layout_checks:
            if type_name not in layouts:
                query = self.run([self.amc, "query", artifact,
                                  "--type", type_name, "--layout"])
                layouts[type_name] = query.stdout
            self.check(all(s in layouts[type_name] for s in substrings), description)
        return artifact

    def test_generate(self, artifact: str) -> None:
        case = self.case
        lang = case.generate_lang()
        gen_dir = os.path.join(self.work, "gen")
        result = self.run([self.amc, "generate", artifact, "-l", lang, "-o", gen_dir])
        projection = os.path.join(gen_dir, case.projection_name)
        if result.returncode != 0 or not os.path.isfile(projection):
            self.fail(f"amc generate -l {lang} produced a projection "
                      f"({result.stderr.strip()})")
            return
        self.pass_(f"amc generate -l {lang} produced a projection")

        with open(projection, "r", encoding="utf-8") as handle:
            text = handle.read()
        for description, substrings in case.projection_checks:
            self.check(all(s in text for s in substrings), description)

        if case.compile_tool and case.compile:
            tool = shutil.which(case.compile_tool)
            if tool is None:
                self.info(f"{case.compile_tool} not found: skipping projection compilation")
                return
            compile_result = case.compile(tool, projection, os.path.dirname(projection))
            self.check(compile_result.returncode == 0,
                       f"generated {case.language_title()} projection compiles "
                       f"({compile_result.stderr.strip()[:200]})")

    def test_determinism(self) -> None:
        case = self.case
        first = os.path.join(self.work, "det-a")
        second = os.path.join(self.work, "det-b")
        self.run([self.amc, "build", "-c", case.config, "-B", first])
        self.run([self.amc, "build", "-c", case.config, "-B", second])
        a = os.path.join(first, case.artifact_name)
        b = os.path.join(second, case.artifact_name)
        if not (os.path.isfile(a) and os.path.isfile(b)):
            self.fail("determinism: both builds produced an artifact")
            return
        with open(a, "rb") as fa, open(b, "rb") as fb:
            self.check(fa.read() == fb.read(),
                       f"identical {case.language_title()} inputs produce "
                       f"byte-identical artifacts")

    # -- orchestration -----------------------------------------------------

    def run_all(self) -> int:
        self.test_describe()
        artifact = self.test_build_and_query()
        if os.path.isfile(artifact):
            self.test_generate(artifact)
        if self.case.determinism:
            self.test_determinism()
        return self.finish()

    def finish(self) -> int:
        print(f"\n{self.result.passed} passed, {self.result.failed} failed")
        if self.case.work_dir is None:
            shutil.rmtree(self.work, ignore_errors=True)
        return 0 if self.result.failed == 0 else 1
