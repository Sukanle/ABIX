#!/usr/bin/env python3
"""Answer "is `plugin` ABI-compatible with `host`?" through the ABIX MCP server.

This is the plan's MCP demo with the transport and tool protocol kept real: it
speaks JSON-RPC over stdio to `amc-mcp`, so the exact same exchange works when
an AI agent (e.g. Claude) drives the tools. The script just removes the LLM from
the loop so the flow can run in CI.

Usage:
    abix_mcp_compat.py --host host.abix --plugin plugin.abix [--amc-mcp PATH]
"""

import argparse
import json
import subprocess
import sys


def request(process, payload):
    process.stdin.write(json.dumps(payload) + "\n")
    process.stdin.flush()
    line = process.stdout.readline()
    if not line:
        raise RuntimeError("amc-mcp closed the connection")
    return json.loads(line)


def call(process, identifier, tool, arguments):
    response = request(process, {
        "jsonrpc": "2.0", "id": identifier, "method": "tools/call",
        "params": {"name": tool, "arguments": arguments},
    })
    if "error" in response:
        raise RuntimeError(f"{tool}: {response['error']}")
    result = response["result"]
    if result.get("isError"):
        raise RuntimeError(f"{tool}: {result['content'][0]['text']}")
    return result.get("structuredContent", {})


def main() -> int:
    parser = argparse.ArgumentParser(description="ABIX MCP compatibility demo")
    parser.add_argument("--host", required=True, help="host .abix or binary")
    parser.add_argument("--plugin", required=True, help="plugin .abix or binary")
    parser.add_argument("--amc-mcp", default="amc-mcp", help="path to amc-mcp")
    args = parser.parse_args()

    process = subprocess.Popen(
        [args.amc_mcp, args.host],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        request(process, {
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {"protocolVersion": "2024-11-05"},
        })
        host = call(process, 2, "abix.get_module", {})
        plugin = call(process, 3, "abix.get_module", {"module": args.plugin})
        verdict = call(process, 4, "abix.compare_abi",
                       {"module": args.host, "other": args.plugin})
    finally:
        process.stdin.close()
        process.wait()

    report = {
        "host": host.get("package", ""),
        "plugin": plugin.get("package", ""),
        "compatible": verdict.get("compatible", False),
        "change_count": len(verdict.get("changes", [])),
        "changes": verdict.get("changes", []),
    }
    print(json.dumps(report, indent=2))
    if not report["compatible"]:
        print(f"INCOMPATIBLE: {report['change_count']} difference(s)", file=sys.stderr)
        return 1
    print("COMPATIBLE", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
