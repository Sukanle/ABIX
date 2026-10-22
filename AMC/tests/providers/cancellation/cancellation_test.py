#!/usr/bin/env python3
"""Cooperative-cancellation IPC test for the abix.plugin/1 protocol.

Usage:
    cancellation_test.py <amc_bin_dir>

The test-only provider runs a slow backend that polls the cancellation token.
The driver sends PROJECT, then CANCEL for that request, and expects:
    EVENT started (id=2)
    CANCELLED status=requested (id=3, target=2)
    CANCELLED status=cancelled (id=2)
    DONE (id=4)
without a RESULT for the cancelled request.
"""

import json
import os
import subprocess
import sys


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    provider = os.path.join(os.path.abspath(sys.argv[1]), exe_name("amc-cancel-test-provider"))
    if not os.path.isfile(provider):
        print(f"  [SKIP] {provider} not found")
        return 0

    process = subprocess.Popen(
        [provider, "--ipc"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    def send(message: dict) -> None:
        process.stdin.write(json.dumps(message) + "\n")
        process.stdin.flush()

    def read() -> dict:
        line = process.stdout.readline()
        if not line:
            raise RuntimeError("provider closed stdout")
        return json.loads(line)

    try:
        send({"schema": "abix.plugin/1", "id": 1, "type": "HELLO", "protocol": {"major": 1, "minor": 0}})
        ready = read()
        if ready.get("type") != "READY":
            raise RuntimeError(f"expected READY, got {ready}")

        send({"schema": "abix.plugin/1", "id": 2, "type": "PROJECT", "input": "in.abix", "output": "out"})
        started = read()
        if started.get("type") != "EVENT" or started.get("event", {}).get("phase") != "started":
            raise RuntimeError(f"expected started EVENT, got {started}")

        send({"schema": "abix.plugin/1", "id": 3, "type": "CANCEL", "target": 2})
        requested = read()
        if requested.get("type") != "CANCELLED" or requested.get("status") != "requested":
            raise RuntimeError(f"expected CANCEL acknowledgement, got {requested}")

        finished = read()
        if finished.get("type") != "CANCELLED" or finished.get("id") != 2:
            raise RuntimeError(f"expected cancelled result for id 2, got {finished}")

        send({"schema": "abix.plugin/1", "id": 4, "type": "DONE"})
        done = read()
        if done.get("type") != "DONE":
            raise RuntimeError(f"expected DONE, got {done}")

        print("  [PASS] provider observed CANCEL and returned CANCELLED before RESULT")
        return 0
    finally:
        try:
            process.stdin.close()
        except Exception:
            pass
        process.wait(timeout=10)


if __name__ == "__main__":
    sys.exit(main())
