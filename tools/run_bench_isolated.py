#!/usr/bin/env python3
"""Deprecated: this runner moved to `tools/run_bench.py`.

Kept so existing commands and docs keep working; every argument is forwarded
unchanged. One behaviour difference: the default is now *every* family discovered in
`bench_all` plus the standalone Google Benchmark executables, not only the EBR/RCU
set. Pass explicit family names to narrow it, or `--no-extra` to run only the
`bench_all` families, e.g.:

    python tools/run_bench_isolated.py BM_EBR_SyncPhase BM_Reload_ --no-extra
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    runner = Path(__file__).resolve().parent / "run_bench.py"
    print("run_bench_isolated.py is deprecated; forwarding to run_bench.py", file=sys.stderr)
    return subprocess.run([sys.executable, str(runner), *sys.argv[1:]]).returncode


if __name__ == "__main__":
    sys.exit(main())
