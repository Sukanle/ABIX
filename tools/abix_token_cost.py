#!/usr/bin/env python3
"""Measure the token cost of two ways to learn an ABI.

  * source view   — the C/C++ header(s) an agent would read to infer the ABI
  * metadata view — `amc context <file.abix> --format llm`

Token counts are estimated with a simple lexical tokenizer (identifier/number
runs plus single punctuation characters). This is a reproducible proxy, not a
model tokenizer; the point is the ratio between two views of the same module.

Note: the advantage depends on the module. The metadata view is dense and
complete but includes every type/field/function with full hashes, whereas the
source view omits transitive includes, the C++ standard library types, and the
implementations an agent often needs to inspect. Treat the ratio as an indicator,
not a benchmark.

Usage:
    abix_token_cost.py --abix module.abix --source a.hpp [b.hpp ...]
"""

import argparse
import json
import re
import subprocess
import sys

TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|\d+|[^\sA-Za-z0-9_]")


def estimate(text: str) -> int:
    return len(TOKEN.findall(text))


def main() -> int:
    parser = argparse.ArgumentParser(description="ABIX token-cost estimator")
    parser.add_argument("--abix", required=True, help=".abix artifact or binary")
    parser.add_argument("--source", nargs="+", required=True, help="source file(s)")
    parser.add_argument("--amc", default="amc", help="path to the amc executable")
    args = parser.parse_args()

    context = subprocess.run(
        [args.amc, "context", args.abix, "--format", "llm"],
        capture_output=True, text=True, check=True).stdout

    source = ""
    for path in args.source:
        with open(path, encoding="utf-8") as handle:
            source += handle.read()

    metadata_tokens = estimate(context)
    source_tokens = estimate(source)
    ratio = round(source_tokens / metadata_tokens, 3) if metadata_tokens else 0.0
    print(json.dumps({
        "source_tokens": source_tokens,
        "metadata_tokens": metadata_tokens,
        "source_over_metadata": ratio,
    }, indent=2))
    if metadata_tokens == 0 or source_tokens == 0:
        print("could not measure both views", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
