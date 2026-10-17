#!/usr/bin/env python3
"""Run benchmark families in separate processes.

Why: `bench_all` links every benchmark into one process, and the EBR/RCU families
share the process-wide `rcu::Domain` singleton. A family measured after the
others have run sees a populated domain (registered thread slots, pending retired
nodes), so the same family reads several times slower than when it is filtered —
`BM_EBR_SyncPhase/1T` measured 1004 ns after a full run and 134 ns when filtered,
a 7.5x spread that has nothing to do with the code under test.

This runner gives each family its own process, so:

  * numbers from different families are comparable with each other, and
  * numbers are comparable across machines and commits (the domain starts empty
    for every family).

Usage:
    python tools/run_bench_isolated.py                       # all default families
    python tools/run_bench_isolated.py BM_EBR_SyncPhase ...  # only these
    python tools/run_bench_isolated.py --list                # show the families
    python tools/run_bench_isolated.py --bin build/Release/bin --out build/bench \
        --repetitions 5
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

# Families whose numbers are sensitive to the shared rcu::Domain state.
DEFAULT_FAMILIES = [
    "BM_EBR_SyncPhase",
    "BM_EBR_ReaderScalability",
    "BM_EBR_WriterScalability",
    "BM_EBR_ReadWriteRatio",
    "BM_EBR_ReadWriteOps",
    "BM_EBR_GracePeriod",
    "BM_EBR_ReclaimBatch",
    "BM_EBR_MixedWorkload",
    "BM_EBR_Workload",
    "BM_EBR_EnterExit",
    "BM_EBR_ProtectedLoad",
    "BM_Reload_",
]


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


def run_family(bench: Path, out_dir: Path, family: str, repetitions: int, min_time: str) -> Path:
    out = out_dir / (family.replace("/", "_") + ".json")
    cmd = [
        str(bench),
        f"--benchmark_filter={family}",
        f"--benchmark_repetitions={repetitions}",
        "--benchmark_format=json",
        f"--benchmark_out={out}",
        "--benchmark_out_format=json",
        "--benchmark_color=false",
    ]
    if min_time:
        cmd.append(f"--benchmark_min_time={min_time}")
    # Each family runs in its own process: that is the whole point. Decode as UTF-8
    # regardless of the console locale (Windows is often GBK here).
    result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(f"  [FAIL] {family}: exit {result.returncode}\n{result.stderr[-800:]}", file=sys.stderr)
    return out


def summarize(path: Path) -> list[tuple[str, int, float, float]]:
    try:
        data = json.load(open(path, encoding="utf-8"))
    except (OSError, ValueError):
        return []
    mean: dict[str, float] = {}
    sd: dict[str, float] = {}
    threads: dict[str, int] = {}
    for row in data.get("benchmarks", []):
        if row.get("run_type") != "aggregate":
            continue
        name = row["name"]
        base = name[: -len("_" + row["aggregate_name"])]
        threads[base] = row.get("threads", 1)
        (mean if row["aggregate_name"] == "mean" else sd)[base] = row["cpu_time"]
    return [(k, threads.get(k, 1), mean[k], sd.get(k, 0.0)) for k in sorted(mean)]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run benchmark families in separate processes")
    parser.add_argument("families", nargs="*", help="families to run (default: the EBR/RCU set)")
    parser.add_argument("--bin", default="build/Release/bin", help="directory holding bench_all")
    parser.add_argument("--out", default="build/bench", help="directory for the per-family JSON")
    parser.add_argument("--repetitions", type=int, default=5, help="Google Benchmark repetitions")
    parser.add_argument("--min-time", default="", help="optionally bound each run, e.g. 0.2s")
    parser.add_argument("--list", action="store_true", help="list the default families and exit")
    args = parser.parse_args()

    if args.list:
        for family in DEFAULT_FAMILIES:
            print(family)
        return 0

    bench = Path(args.bin) / exe_name("bench_all")
    if not bench.is_file():
        print(f"bench_all not found at {bench}", file=sys.stderr)
        return 2
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    families = args.families or DEFAULT_FAMILIES
    print(f"running {len(families)} families, one process each ({args.repetitions} repetitions)")
    summary: dict[str, list[tuple[str, int, float, float]]] = {}
    for family in families:
        path = run_family(bench, out_dir, family, args.repetitions, args.min_time)
        rows = summarize(path)
        summary[family] = rows
        print(f"  {family:26s} {len(rows):3d} benchmarks -> {path}")

    print()
    print("| family | benchmark | threads | mean ns | sd |")
    print("|---|---|---:|---:|---:|")
    for family in families:
        for name, threads, mean, sd in summary.get(family, []):
            print(f"| `{family}` | `{name}` | {threads} | {mean:,.3f} | {sd:,.3f} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
