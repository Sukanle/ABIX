#!/usr/bin/env python3
"""Run every Google Benchmark in its own process and summarise the results.

Why one process per family: `bench_all` links every benchmark into a single process
and the EBR/RCU families share the process-wide `rcu::Domain` singleton. A family
measured after the others have run sees a populated domain (registered thread slots,
pending retired nodes), so the same family reads several times slower than when it
is filtered - `BM_EBR_SyncPhase/1T` measured 1004 ns after a full run and 134 ns
when filtered, a 7.5x spread that has nothing to do with the code under test.
Giving each family its own process makes the numbers comparable across families,
machines and commits, because the domain starts empty for every one of them.

Coverage:

  * every family discovered in `bench_all` via `--benchmark_list_tests`, so a newly
    added benchmark is picked up without maintaining a list here.

Every Google Benchmark suite is linked into `bench_all`, so there are no default
standalone extras. `ebr_bench`, `ebr_profile_bench`, `interface_dispatch_bench`,
`interface_lifecycle_bench`, `abix_resolve_bench` and `abix_lookup_cross_bench`
still ship as standalone executables and can be run directly; add one with
`--extra-exe <name>` to run it as a separate process instead. The plain chrono
probes (`hashindex_build_bench`, and the dev-only `rcu_advance_probe`) are not
Google Benchmark programs and are out of scope here.

Two environment problems make short EBR runs unrepeatable, and both are handled here
instead of being left to each child process:

  * CPU frequency scaling. Google Benchmark reads `cpu0`'s cpufreq governor and warns
    unless it is `performance`. The governor is checked up front (so the message is
    printed once, not once per child, where `run_gb` captures it) and the fix command
    is printed with it.
  * Hybrid P-core/E-core scheduling. `topology/topology.cpp` reports 0 P-cores on
    Linux, so a thread is free to migrate between a 5.5 GHz P-core and a 4.3 GHz
    E-core mid-measurement. `--pin auto` restricts every child process to the
    fastest cores, `--pin <list>` restricts it to an explicit set and `--pin off`
    leaves the scheduler alone. Pinning a family with more threads than the pool
    oversubscribes it, so read the printed pool before comparing thread counts.

Usage:
    python tools/run_bench.py                                   # everything
    python tools/run_bench.py BM_EBR_SyncPhase BM_Reload_       # only these filters
    python tools/run_bench.py --list                            # what would run
    python tools/run_bench.py --no-extra                        # bench_all families only
    python tools/run_bench.py --extra-exe ebr_bench             # add one back
    python tools/run_bench.py --bin build/Release/bin --out build/bench --repetitions 5
    python tools/run_bench.py --pin off                         # keep every CPU available
    python tools/run_bench.py --pin 0-7                         # pin to an explicit CPU list
"""

from __future__ import annotations

import argparse
import functools
import json
import os
import subprocess
import sys
from pathlib import Path

# Standalone Google Benchmark executables that are not linked into bench_all.
# Empty by default: every suite is now part of bench_all; pass `--extra-exe` to
# run one of the still-existing standalone targets as a separate process.
DEFAULT_EXTRA_EXES: list[str] = []


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


def _read_int(path: Path) -> int | None:
    """Contents of a sysfs integer file, or None when it cannot be read."""
    try:
        return int(path.read_text(encoding="ascii").strip())
    except (OSError, ValueError):
        return None


def cpu_scaling_advice() -> str | None:
    """A warning plus the fix commands when CPU frequency scaling is active.

    Google Benchmark prints `***WARNING*** CPU scaling is enabled` unless cpu0's
    governor is `performance`; that same condition is what makes the EBR numbers
    noisy, so the run is annotated once here instead of in every child's stderr,
    which `run_gb` captures and only prints on failure. Returns None when the file
    is absent (non-Linux, or cpufreq not exposed) or already `performance`.
    """
    try:
        governor = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor").read_text(encoding="ascii").strip()
    except OSError:
        return None
    if governor == "performance":
        return None
    return (
        f"CPU scaling is enabled (cpu0 governor: {governor}) - real-time numbers will be noisy.\n"
        "  fix:  sudo cpupower frequency-set -g performance\n"
        "  or:   echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor\n"
        "  undo: sudo cpupower frequency-set -g powersave"
    )


def performance_cpu_pool() -> list[int]:
    """Online CPUs reporting the highest `cpuinfo_max_freq`, i.e. the P-cores.

    The frequency split is the portable hybrid signal on Linux (this class of Intel
    parts reports 5.5 GHz for P-cores and 4.3 GHz for E-cores). An empty list means
    the machine is homogeneous, or cpufreq is unavailable, and pinning is skipped
    rather than guessed.
    """
    freqs: dict[int, int] = {}
    for path in Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/cpuinfo_max_freq"):
        freq = _read_int(path)
        if freq:
            freqs[int(path.parent.parent.name[3:])] = freq
    if not freqs:
        return []
    top = max(freqs.values())
    pool = sorted(cpu for cpu, freq in freqs.items() if freq >= top * 0.95)
    return pool if len(pool) < len(freqs) else []


def parse_cpu_list(spec: str) -> list[int]:
    """`0-3,8,10-11` -> [0, 1, 2, 3, 8, 10, 11]."""
    cpus: list[int] = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        first, sep, last = part.partition("-")
        cpus.extend(range(int(first), int(last) + 1) if sep else [int(first)])
    return cpus


def format_cpu_list(cpus: list[int]) -> str:
    """[0, 1, 2, 3, 8] -> `0-3,8` (the inverse of `parse_cpu_list`)."""
    spans: list[str] = []
    start = prev = cpus[0]
    for cpu in cpus[1:]:
        if cpu == prev + 1:
            prev = cpu
            continue
        spans.append(f"{start}-{prev}" if start != prev else str(start))
        start = prev = cpu
    spans.append(f"{start}-{prev}" if start != prev else str(start))
    return ",".join(spans)


def resolve_pin(pin: str) -> list[int]:
    """The CPU set the children are restricted to; empty means no restriction.

    `topology/topology.cpp` leaves `performance_cpus` at 0 on Linux, so the P-core
    pool is derived here. `os.sched_setaffinity` is Unix-only, so a request it cannot
    honour on this platform is reported and then ignored.
    """
    if pin == "off":
        return []
    cpus = performance_cpu_pool() if pin == "auto" else parse_cpu_list(pin)
    if not cpus:
        return []
    if not hasattr(os, "sched_setaffinity"):
        print("  [warn] --pin is not supported on this platform, running unpinned", file=sys.stderr)
        return []
    return cpus


def pin_spec(value: str) -> str:
    """argparse `type` for `--pin`: a typo fails before the first benchmark runs."""
    if value in ("auto", "off"):
        return value
    try:
        cpus = parse_cpu_list(value)
    except ValueError:
        cpus = []
    if not cpus:
        raise argparse.ArgumentTypeError(f"expected `auto`, `off` or a CPU list like `0-7`, got {value!r}")
    return value


def strip_child_noise(text: str) -> str:
    """Drop the environment banners Google Benchmark repeats in every child.

    They would otherwise fill the excerpt printed for a failing executable and hide
    the actual error.
    """
    noise = ("***WARNING*** CPU scaling", "***WARNING*** Failed to set thread affinity")
    return "\n".join(line for line in text.splitlines() if not line.startswith(noise)).strip()


def discover_families(bench: Path) -> list[str]:
    """Ask bench_all for its benchmark names and group them by family.

    The family is everything before the first '/', e.g. `BM_EBR_SyncPhase/1T` and
    `BM_EBR_SyncPhase/2T` both belong to `BM_EBR_SyncPhase`. Some benches print a
    banner before the listing (the EBR harness prints its domain layout), so lines
    that are indented or contain a space are not benchmark names.
    """
    result = subprocess.run(
        [str(bench), "--benchmark_list_tests=true"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise SystemExit(f"`{bench} --benchmark_list_tests` failed (exit {result.returncode})\n{result.stderr[-800:]}")
    families: set[str] = set()
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if not stripped or stripped != line or " " in stripped:
            continue
        families.add(stripped.split("/")[0])
    return sorted(families)


def run_gb(
    exe: Path, out: Path, filter_: str | None, repetitions: int, min_time: str, allowed_cpus: list[int], cwd: Path
) -> bool:
    """Run one Google Benchmark executable (or one filtered family) into `out`.

    Returns whether the executable exited cleanly. A benchmark that skips itself
    (Google Benchmark's SkipWithError, e.g. a plugin that was not staged) is not a
    failure; a non-zero exit is. `allowed_cpus` restricts the child, and every thread
    it spawns, to that CPU set; an empty list leaves the scheduler alone.
    """
    cmd = [str(exe)]
    if filter_:
        cmd.append(f"--benchmark_filter={filter_}")
    cmd += [
        f"--benchmark_repetitions={max(repetitions, 1)}",
        "--benchmark_format=json",
        f"--benchmark_out={out}",
        "--benchmark_out_format=json",
        "--benchmark_color=false",
    ]
    if min_time:
        cmd.append(f"--benchmark_min_time={min_time}")
    # Decode as UTF-8 regardless of the console locale (Windows is often GBK here).
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        cwd=cwd,
        preexec_fn=functools.partial(os.sched_setaffinity, 0, set(allowed_cpus)) if allowed_cpus else None,
    )
    if result.returncode != 0:
        excerpt = strip_child_noise(result.stderr[-800:])
        print(f"  [FAIL] {filter_ or exe.name}: exit {result.returncode}\n{excerpt}", file=sys.stderr)
        return False
    return True


def summarize(path: Path) -> list[tuple[str, int, float, float]]:
    """(benchmark, threads, mean ns, stddev ns) from a Google Benchmark JSON file.

    With repetitions > 1 the aggregate rows carry the answer; only `mean` and
    `stddev` are read (reading every non-mean aggregate into the stddev column would
    report `cv` there instead). With repetitions == 1 there are no aggregate rows,
    so the iteration samples are averaged.
    """
    try:
        data = json.load(open(path, encoding="utf-8"))
    except (OSError, ValueError):
        return []
    mean: dict[str, float] = {}
    sd: dict[str, float] = {}
    threads: dict[str, int] = {}
    iteration_times: dict[str, list[float]] = {}
    for row in data.get("benchmarks", []):
        run_type = row.get("run_type")
        name = row.get("name", "")
        if run_type == "aggregate":
            agg = row.get("aggregate_name")
            base = name[: -len("_" + agg)] if agg and name.endswith("_" + agg) else name
            threads[base] = row.get("threads", 1)
            if agg == "mean":
                mean[base] = row["cpu_time"]
            elif agg == "stddev":
                sd[base] = row["cpu_time"]
        elif run_type == "iteration":
            iteration_times.setdefault(name, []).append(row["cpu_time"])
            threads.setdefault(name, row.get("threads", 1))
    if not mean:
        for name, times in iteration_times.items():
            mean[name] = sum(times) / len(times)
            sd[name] = 0.0
    return [(k, threads.get(k, 1), mean[k], sd.get(k, 0.0)) for k in sorted(mean)]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run every Google Benchmark in its own process")
    parser.add_argument("families", nargs="*", help="benchmark filters to run (default: every family in bench_all)")
    parser.add_argument("--bin", default="build/Release/bin", help="directory holding the benchmark executables")
    parser.add_argument("--out", default="build/bench", help="directory for the per-family JSON")
    parser.add_argument("--repetitions", type=int, default=5, help="Google Benchmark repetitions")
    parser.add_argument("--min-time", default="", help="optionally bound each run, e.g. 0.2s")
    parser.add_argument(
        "--pin",
        type=pin_spec,
        default="auto",
        help="restrict the children to a CPU set: `auto` (fastest cores, the default), `off`, or a list like `0-7`",
    )
    parser.add_argument(
        "--extra-exe",
        action="append",
        default=None,
        dest="extra_exes",
        help=f"standalone executable to run (repeatable); default: {' '.join(DEFAULT_EXTRA_EXES)}",
    )
    parser.add_argument("--no-extra", action="store_true", help="run only the bench_all families")
    parser.add_argument("--no-families", action="store_true", help="run only the standalone executables")
    parser.add_argument("--list", action="store_true", help="list what would run and exit")
    args = parser.parse_args()

    # Absolute, because the children run with cwd set to the bin directory: a
    # relative --benchmark_out would then be created inside it (or fail).
    bin_dir = Path(args.bin).resolve()
    bench = bin_dir / exe_name("bench_all")

    if args.no_families:
        families: list[str] = []
    else:
        if not bench.is_file():
            print(f"bench_all not found at {bench}", file=sys.stderr)
            return 2
        families = args.families or discover_families(bench)

    if args.no_extra:
        extra_exes: list[str] = []
    elif args.extra_exes is not None:
        extra_exes = args.extra_exes
    else:
        extra_exes = list(DEFAULT_EXTRA_EXES)

    missing = [e for e in extra_exes if not (bin_dir / exe_name(e)).is_file()]
    for name in missing:
        print(f"  [skip] {name} not found in {bin_dir}", file=sys.stderr)
    extra_exes = [e for e in extra_exes if e not in missing]

    if args.list:
        print(f"bench_all families ({len(families)}):")
        for family in families:
            print(f"  {family}")
        print(f"standalone executables ({len(extra_exes)}):")
        for name in extra_exes:
            print(f"  {name}")
        return 0

    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    allowed_cpus = resolve_pin(args.pin)

    print(
        f"running {len(families)} bench_all families + {len(extra_exes)} standalone executables, "
        f"one process each ({args.repetitions} repetitions)",
        flush=True,
    )
    print(
        "note: EBR/RCU numbers are only comparable because each family starts from an empty rcu::Domain",
        file=sys.stderr,
    )
    scaling = cpu_scaling_advice()
    if scaling:
        print(f"  [warn] {scaling}", file=sys.stderr)
    if allowed_cpus:
        print(f"  [pin] restricting every process to CPUs {format_cpu_list(allowed_cpus)}", file=sys.stderr)
    elif args.pin != "off":
        print("  [pin] no P-core pool detected, running unpinned", file=sys.stderr)

    # label -> rows, so the summary table can mix families and standalone executables.
    summary: dict[str, list[tuple[str, int, float, float]]] = {}
    order: list[str] = []
    failed: list[str] = []

    for family in families:
        path = out_dir / (family.replace("/", "_") + ".json")
        if not run_gb(bench, path, family, args.repetitions, args.min_time, allowed_cpus=allowed_cpus, cwd=bin_dir):
            failed.append(family)
        rows = summarize(path)
        summary[family] = rows
        order.append(family)
        print(f"  {family:28s} {len(rows):3d} benchmarks -> {path}")

    for exe in extra_exes:
        path = out_dir / (exe + ".json")
        if not run_gb(
            bin_dir / exe_name(exe), path, None, args.repetitions, args.min_time, allowed_cpus=allowed_cpus, cwd=bin_dir
        ):
            failed.append(exe)
        rows = summarize(path)
        summary[exe] = rows
        order.append(exe)
        print(f"  {exe:28s} {len(rows):3d} benchmarks -> {path} (standalone)")

    print()
    print("| family | benchmark | threads | mean ns | sd |")
    print("|---|---|---:|---:|---:|")
    for label in order:
        for name, threads, mean, sd in summary.get(label, []):
            print(f"| `{label}` | `{name}` | {threads} | {mean:,.3f} | {sd:,.3f} |")

    if failed:
        print(f"\n{len(failed)} run(s) exited non-zero: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
