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
import struct
import subprocess

import lldb

# Primitive ABI decoding table: name -> (width in bytes, kind). AMC normalizes
# `int32_t` -> `int`, `uint64_t` -> `unsigned long`, etc.
_PRIMITIVE = {}
for _name, _size, _kind in (
    ("bool", 1, "uint"),
    ("char", 1, "int"), ("signed char", 1, "int"), ("unsigned char", 1, "uint"),
    ("short", 2, "int"), ("unsigned short", 2, "uint"),
    ("int", 4, "int"), ("unsigned int", 4, "uint"), ("unsigned", 4, "uint"),
    ("long", 8, "int"), ("unsigned long", 8, "uint"),
    ("long long", 8, "int"), ("unsigned long long", 8, "uint"),
    ("float", 4, "float"), ("double", 8, "float"),
):
    _PRIMITIVE[_name] = (_size, _kind)


def _decode(kind, raw):
    table = {"int": {1: "b", 2: "h", 4: "i", 8: "q"},
             "uint": {1: "B", 2: "H", 4: "I", 8: "Q"}}
    if kind in table and len(raw) in table[kind]:
        return str(struct.unpack("<" + table[kind][len(raw)], raw)[0])
    if kind == "float" and len(raw) in (4, 8):
        return str(struct.unpack("<f" if len(raw) == 4 else "<d", raw)[0])
    if kind == "ptr" and len(raw) == 8:
        return "0x%x" % struct.unpack("<Q", raw)[0]
    return raw.hex()


def _field_encoding(type_name):
    info = _PRIMITIVE.get(type_name)
    if info is not None:
        return info
    if type_name.rstrip().endswith("*"):
        return (8, "ptr")
    return None


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

    if sub == "source" and len(argv) >= 2:
        name = argv[1]
        source = _source_of(binary, name)
        if source is None:
            # The embedded Metadata Region is intentionally source-free (release
            # image). Fall back to the plan's chain: BuildID -> symbol server ->
            # debug `.abix` (which carries the optional Source Origin section).
            fetched = _run(["fetch", binary])
            path = fetched.stdout.strip().splitlines()[-1] if fetched.returncode == 0 else ""
            if path.endswith(".abix"):
                source = _source_of(path, name)
        if source is None:
            result.SetStatus(lldb.eReturnStatusFailure)
            result.AppendMessage(
                "abix: no source origin for '" + name + "' "
                "(embedded region is source-free; publish the debug .abix to the symbol server)")
            return
        result.AppendMessage(f"{source['file']}:{source['line']}:{source['column']}")
        return

    if sub == "cast" and len(argv) >= 3:
        _cast(debugger, result, argv[1], argv[2])
        return

    result.SetStatus(lldb.eReturnStatusFailure)
    result.AppendMessage(
        "usage: abix info | type <name> | function <name> | "
        "verify <other> | check <name> <other> | source <name> | "
        "cast <address-expression> <type>")


def _cast(debugger, result, expression, type_name):
    """Interpret memory at `expression` using the AMC layout of `type_name`."""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess() if target and target.IsValid() else None
    if process is None or not process.IsValid():
        result.SetStatus(lldb.eReturnStatusFailure)
        result.AppendMessage("abix cast: no running process (run the target first)")
        return
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame() if thread else None
    if frame is None:
        result.SetStatus(lldb.eReturnStatusFailure)
        result.AppendMessage("abix cast: no selected frame")
        return
    value = frame.EvaluateExpression(expression)
    if not value.IsValid() or not value.GetError().Success():
        result.SetStatus(lldb.eReturnStatusFailure)
        result.AppendMessage("abix cast: cannot evaluate '" + expression + "'")
        return
    address = value.GetValueAsUnsigned(0)

    proc = _query(_binary(debugger), ["--type", type_name, "--layout"])
    if proc.returncode != 0:
        result.SetStatus(lldb.eReturnStatusFailure)
        result.AppendMessage("abix cast: no layout for '" + type_name + "'")
        return
    matches = json.loads(proc.stdout).get("matches", [])
    if not matches:
        result.SetStatus(lldb.eReturnStatusFailure)
        result.AppendMessage("abix cast: no type matches '" + type_name + "'")
        return
    layout = matches[0]
    size = layout.get("size", 0)
    error = lldb.SBError()
    data = process.ReadMemory(address, size, error)
    if data is None or not error.Success():
        result.SetStatus(lldb.eReturnStatusFailure)
        result.AppendMessage("abix cast: cannot read %d bytes at 0x%x" % (size, address))
        return

    lines = ["abix cast: %s @ 0x%x (%d bytes)" % (layout.get("name"), address, size)]
    fields = layout.get("fields", [])
    if not fields:
        lines.append("  +0   <value>   " + data.hex())
    for field in fields:
        offset = field.get("offset", 0)
        encoding = _field_encoding(field.get("type_name", ""))
        if encoding is None:
            lines.append("  +%-3d %-10s %-10s <opaque>" %
                         (offset, field.get("name"), field.get("type_name")))
            continue
        width, kind = encoding
        raw = data[offset:offset + width]
        lines.append("  +%-3d %-10s %-10s %s" %
                     (offset, field.get("name"), field.get("type_name"), _decode(kind, raw)))
    result.AppendMessage("\n".join(lines))


def _source_of(module, name):
    """Return {'file','line','column'} for a type, or None when unavailable."""
    proc = _query(module, ["--type", name])
    if proc.returncode != 0:
        return None
    try:
        matches = json.loads(proc.stdout).get("matches", [])
    except ValueError:
        return None
    if not matches:
        return None
    return matches[0].get("source")


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f lldb_abix.abix abix")
