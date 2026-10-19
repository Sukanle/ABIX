#!/usr/bin/env python3
"""Reflow Markdown prose so that each paragraph occupies one source line.

The docs are hard-wrapped at a fixed column.  Some renderers turn every soft
line break into a space, so Chinese paragraphs show gaps between characters and
English paragraphs contain artificial breaks.  This script joins the lines of a
paragraph back into one line while leaving everything that is not prose alone:

* fenced code blocks and indented code blocks
* `$$` math blocks
* HTML blocks (`<table>`, `<details>`, `<p>`, `<pre>`, `<!-- ... -->`, ...)
* Markdown tables
* headings, thematic breaks and link reference definitions
* list items (their indented continuation lines are joined)
* blockquotes (their consecutive `>` lines are joined)

Blank lines are kept, so paragraph boundaries do not change.

Usage:
    python3 scripts/reflow_markdown.py --check      # report files that would change
    python3 scripts/reflow_markdown.py --diff       # show a unified diff
    python3 scripts/reflow_markdown.py --write      # rewrite the files
    python3 scripts/reflow_markdown.py docs/abix/interface.md --write

With no paths, every `docs/**/*.md` file is processed.
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

FENCE_RE = re.compile(r"^(\s*)(`{3,}|~{3,})(.*)$")
FENCE_CLOSE_RE = re.compile(r"^\s*(`{3,}|~{3,})\s*$")
HEADING_RE = re.compile(r"^\s{0,3}#{1,6}\s")
THEMATIC_RE = re.compile(r"^\s{0,3}([-*_])(?:\s*\1){2,}\s*$")
LIST_RE = re.compile(r"^(\s*)([-*+]|\d+[.)])\s+")
QUOTE_RE = re.compile(r"^(\s*)>\s?(.*)$")
LINK_DEF_RE = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*\S")
SETEXT_RE = re.compile(r"^\s{0,3}(=+|-+)\s*$")
MATH_FENCE_RE = re.compile(r"^\s*\$\$\s*$")
TABLE_RE = re.compile(r"^\s*\|")
INDENTED_CODE_RE = re.compile(r"^(?:\t| {4})\S")

# A line starting with one of these tags is treated as an HTML block.  Tag
# names are matched case-insensitively.
HTML_CONTAINERS = {
    "table": "table",
    "details": "details",
    "p": "p",
    "pre": "pre",
    "div": "div",
    "ul": "ul",
    "ol": "ol",
    "blockquote": "blockquote",
    "dl": "dl",
}
HTML_ONE_LINE = {"summary", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "br", "img", "a"}
HTML_START_RE = re.compile(r"^\s*<([A-Za-z][A-Za-z0-9-]*)")

# Characters that are CJK ideographs, kana, hangul or CJK punctuation.  Two
# adjacent CJK characters come from one hard-wrapped sentence, so they are
# joined without a space.  Everything else keeps the space that a Markdown soft
# break would have rendered.
CJK_RANGES = (
    (0x2E80, 0x2FFF),    # CJK radicals, Kangxi radicals
    (0x3000, 0x303F),    # CJK symbols and punctuation
    (0x3040, 0x30FF),    # Hiragana, Katakana
    (0x3400, 0x4DBF),    # CJK Extension A
    (0x4E00, 0x9FFF),    # CJK Unified Ideographs
    (0xF900, 0xFAFF),    # CJK Compatibility Ideographs
    (0xFE30, 0xFE4F),    # CJK Compatibility Forms
    (0xFF00, 0xFFEF),    # Halfwidth and Fullwidth Forms
    (0x20000, 0x2FA1F),  # CJK Extension B-F and compatibility supplement
)


def is_cjk(ch: str) -> bool:
    code = ord(ch)
    return any(lo <= code <= hi for lo, hi in CJK_RANGES)


def is_blank(line: str) -> bool:
    return not line.strip()


def is_fence(line: str) -> bool:
    match = FENCE_RE.match(line)
    if match is None:
        return False
    fence = match.group(2)
    rest = match.group(3).strip()
    # A one-line span such as ```inline code``` is not a fence.
    return not (rest.endswith(fence[0] * len(fence)) and rest)


def is_math_fence(line: str) -> bool:
    return MATH_FENCE_RE.match(line) is not None


def is_table_line(line: str) -> bool:
    return TABLE_RE.match(line) is not None


def is_heading(line: str) -> bool:
    return HEADING_RE.match(line) is not None


def is_thematic(line: str) -> bool:
    return THEMATIC_RE.match(line) is not None


def is_list_item(line: str) -> bool:
    return LIST_RE.match(line) is not None


def is_quote(line: str) -> bool:
    return QUOTE_RE.match(line) is not None


def is_link_def(line: str) -> bool:
    return LINK_DEF_RE.match(line) is not None


def is_html(line: str) -> bool:
    stripped = line.lstrip()
    if stripped.startswith("<!--"):
        return True
    match = HTML_START_RE.match(line)
    if match is None:
        return False
    tag = match.group(1).lower()
    return tag in HTML_CONTAINERS or tag in HTML_ONE_LINE


def is_indented_code(line: str) -> bool:
    return INDENTED_CODE_RE.match(line) is not None


def leading_width(line: str) -> int:
    expanded = line.expandtabs(4)
    return len(expanded) - len(expanded.lstrip(" "))


def needs_space(left: str, right: str) -> bool:
    """Should a space be inserted when joining two prose fragments?"""
    if not left or not right:
        return False
    last, first = left[-1], right[0]
    if is_cjk(last) and is_cjk(first):
        return False
    # A Chinese sentence mark followed by inline markup stays attached:
    # "记录；`InterfaceRef`", not "记录； `InterfaceRef`".  Before a Latin
    # letter a space is still correct ("结束。Next").
    if last in "，。；：！？、…":
        return first not in "`[*_<" and not is_cjk(first)
    if first in "，。；：！？、…":
        return False
    if last in "([{":
        return False
    if first in ")]},.;:!?%":
        return False
    return True


def join_fragments(left: str, right: str) -> str:
    left = left.rstrip()
    right = right.strip()
    if not left:
        return right
    if not right:
        return left
    return left + (" " if needs_space(left, right) else "") + right


def join_lines(lines: list[str]) -> list[str]:
    """Join prose lines into one line per paragraph, honouring hard breaks."""
    result: list[str] = []
    current = ""
    for raw in lines:
        hard_break = raw.endswith("  ") or raw.rstrip().endswith("\\")
        text = raw.strip()
        if not text:
            continue
        if not current:
            current = text
        elif current.endswith("\\"):
            result.append(current)
            current = text
        else:
            current = join_fragments(current, text)
        if hard_break:
            # Preserve the explicit break: two trailing spaces or a backslash.
            if text.endswith("\\"):
                result.append(current)
            else:
                result.append(current + "  ")
            current = ""
    if current:
        result.append(current)
    return result


class Reflower:
    def __init__(self, lines: list[str]):
        self.lines = lines
        self.setext_text = self._find_setext_text()

    def _find_setext_text(self) -> set[int]:
        marked: set[int] = set()
        for i in range(len(self.lines) - 1):
            line = self.lines[i]
            if is_blank(line) or is_list_item(line) or is_quote(line) or is_fence(line):
                continue
            if SETEXT_RE.match(self.lines[i + 1]) is not None:
                marked.add(i)
        return marked

    def starts_block(self, i: int) -> bool:
        line = self.lines[i]
        return (
            is_blank(line)
            or is_fence(line)
            or is_math_fence(line)
            or is_html(line)
            or is_table_line(line)
            or is_heading(line)
            or is_thematic(line)
            or is_link_def(line)
            or is_quote(line)
            or is_list_item(line)
            or i in self.setext_text
            or SETEXT_RE.match(line) is not None
        )

    def copy_fence(self, i: int) -> int:
        match = FENCE_RE.match(self.lines[i])
        assert match is not None
        marker = match.group(2)
        char, size = marker[0], len(marker)
        i += 1
        while i < len(self.lines):
            close = FENCE_CLOSE_RE.match(self.lines[i])
            if close is not None and close.group(1)[0] == char and len(close.group(1)) >= size:
                return i + 1
            i += 1
        return i

    def copy_math(self, i: int) -> int:
        first = self.lines[i]
        stripped = first.strip()
        if stripped.count("$$") >= 2 and len(stripped) > 2:
            return i + 1
        i += 1
        while i < len(self.lines):
            if MATH_FENCE_RE.match(self.lines[i]) is not None:
                return i + 1
            i += 1
        return i

    def copy_html(self, i: int) -> int:
        first = self.lines[i]
        stripped = first.lstrip()
        if stripped.startswith("<!--"):
            while i < len(self.lines):
                if "-->" in self.lines[i]:
                    return i + 1
                i += 1
            return i
        match = HTML_START_RE.match(first)
        if match is None:
            return i + 1
        tag = match.group(1).lower()
        close_tag = HTML_CONTAINERS.get(tag)
        if close_tag is None:
            return i + 1
        close = re.compile(r"</" + re.escape(close_tag) + r"\s*>", re.IGNORECASE)
        depth = 0
        open_tag = re.compile(r"<" + re.escape(close_tag) + r"\b", re.IGNORECASE)
        while i < len(self.lines):
            depth += len(open_tag.findall(self.lines[i]))
            depth -= len(close.findall(self.lines[i]))
            i += 1
            if depth <= 0:
                return i
        return i

    def copy_table(self, i: int) -> int:
        while i < len(self.lines) and is_table_line(self.lines[i]):
            i += 1
        return i

    def copy_indented_code(self, i: int) -> int:
        while i < len(self.lines):
            line = self.lines[i]
            if is_blank(line):
                i += 1
                continue
            if not is_indented_code(line):
                break
            i += 1
        return i

    def copy_quote(self, i: int) -> tuple[list[str], int]:
        output: list[str] = []
        current = ""
        prefix = ""

        def flush() -> None:
            nonlocal current
            if current:
                output.append(prefix + "> " + current)
                current = ""

        while i < len(self.lines):
            match = QUOTE_RE.match(self.lines[i])
            if match is None:
                break
            indent, text = match.group(1), match.group(2)
            if not prefix:
                prefix = indent
            if not text.strip():
                flush()
                output.append(prefix + ">")
                i += 1
                continue
            # A list or another block inside the quote ends the simple reflow.
            if is_list_item(text) or is_table_line(text) or is_fence(text) or is_heading(text):
                flush()
                output.append(self.lines[i].rstrip())
                i += 1
                continue
            current = join_fragments(current, text)
            i += 1
        flush()
        return output, i

    def copy_list_item(self, i: int) -> int:
        match = LIST_RE.match(self.lines[i])
        assert match is not None
        item_indent = leading_width(match.group(1))
        output_index = len(self.output)
        self.output.append(self.lines[i].rstrip())
        i += 1
        while i < len(self.lines):
            line = self.lines[i]
            if is_blank(line):
                break
            if is_list_item(line):
                break
            if (
                is_fence(line)
                or is_math_fence(line)
                or is_html(line)
                or is_table_line(line)
                or is_heading(line)
                or is_thematic(line)
                or is_quote(line)
            ):
                break
            if leading_width(line) < item_indent:
                break
            self.output[output_index] = join_fragments(self.output[output_index], line)
            i += 1
        return i

    def run(self) -> list[str]:
        self.output: list[str] = []
        lines = self.lines
        i = 0

        # YAML front matter: copy verbatim.
        if lines and lines[0].strip() == "---":
            j = 1
            while j < len(lines) and lines[j].strip() not in ("---", "..."):
                j += 1
            if j < len(lines):
                self.output.extend(lines[: j + 1])
                i = j + 1

        while i < len(lines):
            line = lines[i]
            if is_blank(line):
                self.output.append("")
                i += 1
                continue
            if is_fence(line):
                end = self.copy_fence(i)
                self.output.extend(lines[i:end])
                i = end
                continue
            if is_math_fence(line):
                end = self.copy_math(i)
                self.output.extend(lines[i:end])
                i = end
                continue
            if is_html(line):
                end = self.copy_html(i)
                self.output.extend(lines[i:end])
                i = end
                continue
            if is_table_line(line):
                end = self.copy_table(i)
                self.output.extend(lines[i:end])
                i = end
                continue
            if is_heading(line) or i in self.setext_text or SETEXT_RE.match(line) is not None:
                self.output.append(line.rstrip())
                i += 1
                continue
            if is_thematic(line) or is_link_def(line):
                self.output.append(line.rstrip())
                i += 1
                continue
            if is_quote(line):
                block, i = self.copy_quote(i)
                self.output.extend(block)
                continue
            if is_list_item(line):
                i = self.copy_list_item(i)
                continue
            if is_indented_code(line) and (i == 0 or is_blank(lines[i - 1])):
                end = self.copy_indented_code(i)
                self.output.extend(lines[i:end])
                i = end
                continue

            paragraph = [line]
            i += 1
            while i < len(lines) and not self.starts_block(i):
                paragraph.append(lines[i])
                i += 1
            self.output.extend(join_lines(paragraph))

        return self.output


def reflow(text: str) -> str:
    had_final_newline = text.endswith("\n")
    lines = text.splitlines()
    result = Reflower(lines).run()
    output = "\n".join(result)
    if had_final_newline:
        output += "\n"
    return output


def gather_paths(arguments: list[str]) -> list[Path]:
    if arguments:
        return [Path(arg) for arg in arguments]
    return sorted(REPO_ROOT.glob("docs/**/*.md"))


def display(path: Path) -> str:
    try:
        return str(path.relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="rewrite the files in place")
    mode.add_argument("--check", action="store_true", help="exit non-zero if any file would change")
    mode.add_argument("--diff", action="store_true", help="print a unified diff")
    parser.add_argument("paths", nargs="*", help="files to process (default: docs/**/*.md)")
    args = parser.parse_args()

    changed = 0
    for path in gather_paths(args.paths):
        original = path.read_text(encoding="utf-8")
        updated = reflow(original)
        if updated == original:
            continue
        changed += 1
        if args.diff:
            diff = difflib.unified_diff(
                original.splitlines(keepends=True),
                updated.splitlines(keepends=True),
                fromfile=display(path),
                tofile=display(path),
            )
            sys.stdout.writelines(diff)
        else:
            print(display(path))
        if args.write:
            path.write_text(updated, encoding="utf-8")

    if args.check:
        return 1 if changed else 0
    if not (args.write or args.diff):
        print(f"{changed} file(s) would change; pass --write to apply", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
