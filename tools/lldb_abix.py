"""LLDB integration for ABIX (AI-P2).

The `abix` command makes the ABI facts already embedded in the loaded binary
(the `.abix.metadata` region) queryable from the debugger. Parsing and querying
are delegated to the single `amc` implementation, so LLDB never grows a second
`.abix`/ELF parser — matching the plan's "LLDB does not re-parse `.abix`"
principle.

Usage from an LLDB session:
    command script import /path/to/tools/lldb_abix.py
    abix info
    abix type Foo
    abix function Foo::bar
    abix verify other.abix
    abix check Foo other.abix

Set ABIX_AMC to point at the `amc` executable when it is not on PATH.
"""

import json
import os
import shlex
import subprocess

import lldb


def _amc():
    return os.environ.get("ABIX_AMC", "amc")


def _binary(debugger):
    target = debugger.GetSelectedTarget()
    if not target or not target.IsValid():
        return None
    executable = target.GetExecutable()
    if not executable or not executable.IsValid():
        return None
    return executable.fullpath or None


def _run(args):
    return subprocess.run([_amc()] + args, capture_output=True, text=True)


def _emit_json(result, document):
    result.AppendMessage(json.dumps(document, indent=2))


def _query(binary, extra):
    return _run(["query", binary] + extra + ["--format", "json"])


def abix(debugger, command, result, internal_dict=None):
    """Entry point registered as the `abix` LLDB command."""
    argv = shlex.split(command)
    binary = _binary(debugger)
    if binary is None:
        result.SetStatus(lldb.eReturnStatusFailure)
        result.AppendMessage("abix: no target binary loaded")
        return

    if not argv:
        argv = ["info"]
    sub = argv[0]

    if sub == "info":
        proc = _run(["metadata", "--from-elf", binary, "--format", "json"])
        if proc.returncode != 0:
            result.SetStatus(lldb.eReturnStatusFailure)
            result.AppendMessage(proc.stderr.strip() or "abix: no embedded metadata region")
            return
        _emit_json(result, json.loads(proc.stdout))
        return

    if sub == "type" and len(argv) >= 2:
        proc = _query(binary, ["--type", argv[1], "--layout"])
        _emit_json(result, json.loads(proc.stdout))
        return

    if sub == "function" and len(argv) >= 2:
        proc = _query(binary, ["--function", argv[1]])
        _emit_json(result, json.loads(proc.stdout))
        return

    if sub == "verify" and len(argv) >= 2:
        proc = _query(binary, ["--compatible", argv[1]])
        _emit_json(result, json.loads(proc.stdout))
        if proc.returncode != 0:
            result.SetStatus(lldb.eReturnStatusFailure)
        return

    if sub == "check" and len(argv) >= 3:
        name, other = argv[1], argv[2]
        proc = _query(binary, ["--compatible", other])
        changes = [change for change in json.loads(proc.stdout).get("changes", [])
                   if change.get("type") == name]
        _emit_json(result, {"type": name, "compatible": not changes, "changes": changes})
        if changes:
            result.SetStatus(lldb.eReturnStatusFailure)
        return

    result.SetStatus(lldb.eReturnStatusFailure)
    result.AppendMessage(
        "usage: abix info | type <name> | function <name> | "
        "verify <other> | check <name> <other>")


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f lldb_abix.abix abix")
