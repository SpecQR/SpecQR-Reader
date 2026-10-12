#!/usr/bin/env python3
"""Read-only writing checks for SpecQR Reader (Python 3.8+, standard library).

This is deliberately a line-oriented subset, not a Markdown or YAML parser.
It checks root *.md, docs/**/*.md, and workflow names/descriptions/comments.
Supported inline forms: code spans, emphasis, inline/reference links and images,
HTML tags/entities, and autolinks. Fenced/indented code and link definitions are
excluded. Inline code/URL/path/identifier interiors are opaque, but their outer
Japanese boundaries are checked. Blockquotes are NOT implicitly exempt.

Multiline inline syntax, nested same-delimiter emphasis, HTML alt attributes,
YAML flow collections/escaped scalars and multiline display scalars need review.
Only the finite unit list below is checked; language choice and translation
accuracy remain human checks. No source or exception file is ever rewritten.
"""
import argparse
from dataclasses import dataclass
import html
import json
from pathlib import Path
import re
import sys
from typing import Iterable, List, Optional, Sequence, Tuple


# K is intentionally absent: 4K is a brand/format spelling, not an SI quantity.
UNITS = ("MB/s", "Mb/s", "GB/s", "Gb/s", "kB/s", "KB/s", "KiB/s", "MiB/s",
         "Gbps", "Mbps", "Kbps", "kbps", "bps", "KiB", "MiB", "GiB", "TiB",
         "kB", "KB", "MB", "GB", "TB", "B", "bits", "bit", "bytes", "byte",
         "GHz", "MHz", "kHz", "Hz", "kW", "mW", "W", "kg", "mg", "g",
         "km", "cm", "mm", "m", "ms", "ns", "s", "min", "h", "dpi", "ppi",
         "px", "pixels", "pixel", "symbols", "symbol", "fixtures", "fixture",
         "cases", "case", "files", "file", "rows", "row", "characters", "character",
         "modules", "module", "codewords", "codeword", "°C", "%")
NUMBER = r"(?<![A-Za-z0-9_.:/+\-])(?:[+\-]?\d+(?:,\d{3})*(?:\.\d+)?)"
UNIT_RE = re.compile(NUMBER + r"(?P<gap>[^\S\r\n]*)(?:" +
                     "|".join(re.escape(x) for x in sorted(UNITS, key=len, reverse=True)) +
                     r")(?![A-Za-z0-9_])")
ANGLE_RE = re.compile(NUMBER + r"(?P<gap>[^\S\r\n]+)[°′″](?!C)")
TEMPERATURE_RE = re.compile(r"°[^\S\r\n]+C(?![A-Za-z0-9_])")
TERMS = ((re.compile(r"\bQR Model 2\b|\bQR [Cc]ode model 2\b"), "QR Code Model 2"),
         (re.compile(r"\bstructured append\b|\bStructured append\b"), "Structured Append"),
         (re.compile(r"\bGithub\b"), "GitHub"),
         (re.compile(r"\bZXingJava\b|\bZXing java\b"), "ZXing Java"))
URL_RE = re.compile(r"(?:[A-Za-z][A-Za-z0-9+.\-]*://|mailto:)[^\s<>\[\]{}\"'`]+")
EMAIL_RE = re.compile(r"[A-Za-z0-9\u3040-\u30ff\u3400-\u9fff.!#$%&'*+/=?^_`{|}~\-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
PATH_RE = re.compile(
    r"(?:[A-Za-z]:[\\/]|\.{1,2}/|/)[A-Za-z0-9_.~@%+,:=\\/\-]+"
    r"|(?:[A-Za-z0-9_.\-]+/)+[A-Za-z0-9_.\-]+\.[A-Za-z][A-Za-z0-9]*"
    r"|[A-Za-z0-9_][A-Za-z0-9_.%+\-]*\.[A-Za-z][A-Za-z0-9]*(?![A-Za-z0-9_])")
IDENTIFIER_RE = re.compile(
    r"[A-Za-z_][A-Za-z0-9_]*(?:(?:::|->)[A-Za-z_][A-Za-z0-9_]*)+"
    r"|[A-Za-z_][A-Za-z0-9]*_[A-Za-z0-9_]+|C\+\+(?:\d+)?")
TAG_RE = re.compile(r"</?[A-Za-z][A-Za-z0-9:-]*(?:\s+(?:[^'\"<>]|'[^']*'|\"[^\"]*\")*)?\s*/?>")
ENTITY_RE = re.compile(r"&(?:#[0-9]+|#x[0-9a-fA-F]+|[A-Za-z][A-Za-z0-9]+);")
EXPRESSION_RE = re.compile(r"\$\{\{.*?\}\}")


@dataclass(frozen=True)
class Cell:
    char: str
    start: int
    end: int
    opaque: bool = False


@dataclass(frozen=True)
class Diagnostic:
    file: str
    line: int
    column: int
    rule: str
    fragment: str
    message: str

    def format(self) -> str:
        return "{}:{}:{} [{}] {} ({})".format(
            self.file, self.line, self.column, self.rule, self.message,
            json.dumps(self.fragment, ensure_ascii=False))


def _matching(text: str, start: int, opening: str, closing: str) -> Optional[int]:
    """Find a balanced, escaped-aware closing bracket on this single line."""
    depth = 1
    i = start + 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == opening:
            depth += 1
        elif text[i] == closing:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return None


def inline_cells(text: str, offset: int = 0) -> List[Cell]:
    """Project supported inline syntax to visible cells retaining source offsets."""
    cells = []  # type: List[Cell]
    i = 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text) and text[i + 1] in r"\`*_{}[]<>()#+-.!|~":
            cells.append(Cell(text[i + 1], offset + i, offset + i + 2))
            i += 2
            continue
        if text.startswith("<!--", i):
            end = text.find("-->", i + 4)
            if end >= 0:
                i = end + 3
                continue
        if text[i] == "`":
            run = len(text[i:]) - len(text[i:].lstrip("`"))
            closing = re.search(r"(?<!`)`{" + str(run) + r"}(?!`)", text[i + run:])
            if closing:
                end = i + run + closing.end()
                cells.append(Cell("\ufffc", offset + i, offset + end, True))
                i = end
                continue
        bracket = i + 1 if text.startswith("![", i) else i
        if text[bracket:bracket + 1] == "[":
            end_label = _matching(text, bracket, "[", "]")
            if end_label is not None and end_label + 1 < len(text):
                next_char = text[end_label + 1]
                if next_char in "([":
                    end_link = _matching(text, end_label + 1, next_char, ")" if next_char == "(" else "]")
                    if end_link is not None:
                        cells.extend(inline_cells(text[bracket + 1:end_label], offset + bracket + 1))
                        i = end_link + 1
                        continue
        if text[i] == "<":
            auto = re.match(r"<(?:[A-Za-z][A-Za-z0-9+.\-]*://|mailto:)[^<>\s]+>|<[A-Za-z0-9_.+\-]+@[A-Za-z0-9.\-]+>", text[i:])
            if auto:
                end = i + auto.end()
                cells.append(Cell("\ufffc", offset + i, offset + end, True))
                i = end
                continue
            code = re.match(r"<(code|kbd|samp)(?:\s[^>]*)?>(.*?)</\1\s*>", text[i:], re.I)
            if code:
                end = i + code.end()
                cells.append(Cell("\ufffc", offset + i, offset + end, True))
                i = end
                continue
            tag = TAG_RE.match(text, i)
            if tag:
                i = tag.end()
                continue
        entity = ENTITY_RE.match(text, i)
        if entity:
            decoded = html.unescape(entity.group())
            cells.extend(Cell(char, offset + i, offset + entity.end()) for char in decoded)
            i = entity.end()
            continue
        protected = None
        for pattern in (EXPRESSION_RE, URL_RE, EMAIL_RE, PATH_RE, IDENTIFIER_RE):
            protected = pattern.match(text, i)
            if protected:
                break
        if protected:
            cells.append(Cell("\ufffc", offset + i, offset + protected.end(), True))
            i = protected.end()
            continue
        matched_emphasis = False
        for marker in ("**", "__", "~~", "*", "_"):
            if text.startswith(marker, i):
                end = text.find(marker, i + len(marker))
                if end > i + len(marker):
                    cells.extend(inline_cells(text[i + len(marker):end], offset + i + len(marker)))
                    i = end + len(marker)
                    matched_emphasis = True
                    break
        if matched_emphasis:
            continue
        cells.append(Cell(text[i], offset + i, offset + i + 1))
        i += 1
    return cells


def _japanese(char: str) -> bool:
    cp = ord(char)
    return (0x3041 <= cp <= 0x3096 or 0x309D <= cp <= 0x309F or
            0x30A1 <= cp <= 0x30FA or 0x30FC <= cp <= 0x30FF or
            0x3400 <= cp <= 0x4DBF or 0x4E00 <= cp <= 0x9FFF or
            0xF900 <= cp <= 0xFAFF or 0x20000 <= cp <= 0x323AF or
            char in "々〆〇")


def _latin_number(cell: Cell) -> bool:
    return cell.opaque or cell.char in "%°′″" or (
        cell.char.isascii() and cell.char.isalnum())


def lint_line(file: str, line_number: int, source: str) -> List[Diagnostic]:
    cells = inline_cells(source)
    visible = "".join(cell.char for cell in cells)
    found = []  # type: List[Diagnostic]

    def report(rule: str, begin: int, end: int, message: str) -> None:
        if begin < end:
            left, right = cells[begin].start, cells[end - 1].end
            found.append(Diagnostic(file, line_number, left + 1, rule, source[left:right], message))

    for i, left in enumerate(cells):
        if left.char.isspace():
            continue
        j = i + 1
        while j < len(cells) and cells[j].char.isspace():
            j += 1
        if j == len(cells):
            continue
        right = cells[j]
        if ((_japanese(left.char) and _latin_number(right)) or
                (_latin_number(left) and _japanese(right.char))):
            if visible[i + 1:j] != " ":
                report("ja-ascii-space", i, j + 1,
                       "日本語と英字・数字の境界は U+0020 を 1 個にします。")
    for match in UNIT_RE.finditer(visible):
        if match.group("gap") != " ":
            report("number-unit-space", match.start(), match.end(),
                   "数値と既知の単位・% の間は U+0020 を 1 個にします。")
    for match in ANGLE_RE.finditer(visible):
        report("angle-space", match.start(), match.end(), "平面角の °・′・″ は数字へ続けます。")
    for match in TEMPERATURE_RE.finditer(visible):
        report("temperature-unit", match.start(), match.end(), "温度単位 °C の内部は分割しません。")
    for pattern, preferred in TERMS:
        for match in pattern.finditer(visible):
            report("terminology", match.start(), match.end(), "正式名称 {} を使います。".format(preferred))
    return found


def lint_markdown(file: str, source: str) -> List[Diagnostic]:
    found = []  # type: List[Diagnostic]
    fence = None  # type: Optional[Tuple[str, int]]
    comment = False
    raw_element = None  # type: Optional[str]
    for number, line in enumerate(source.splitlines(), 1):
        # Replace comments with zero-width markers: HTML comments do not insert
        # visible space. Offsets remain correct by rendering them as inline tags.
        if comment:
            end = line.find("-->")
            if end < 0:
                continue
            line = "<x" + " " * max(0, end) + ">" + line[end + 3:]
            comment = False
        if raw_element:
            if re.search(r"</" + raw_element + r"\s*>", line, re.I):
                raw_element = None
            continue
        if fence:
            close = re.match(r"^ {0,3}(" + re.escape(fence[0]) + r"{" + str(fence[1]) + r",})\s*$", line)
            if close:
                fence = None
            continue
        opening = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", line)
        if opening:
            if opening.group(1)[0] != "`" or "`" not in opening.group(2):
                fence = (opening.group(1)[0], len(opening.group(1)))
                continue
        if line.startswith("    ") or line.startswith("\t"):
            continue
        if re.match(r"^ {0,3}\[[^\]]+\]:", line):
            continue
        raw = re.match(r"^\s*<(pre|script|style)\b", line, re.I)
        if raw:
            if not re.search(r"</" + raw.group(1) + r"\s*>", line, re.I):
                raw_element = raw.group(1)
            continue
        # Multiline comments are prose-only syntax here; code spans are handled
        # first so a literal <!-- inside code cannot suppress the following text.
        unmatched = None
        i = 0
        while i < len(line):
            if line[i] == "`":
                run = len(line[i:]) - len(line[i:].lstrip("`"))
                close = re.search(r"(?<!`)`{" + str(run) + r"}(?!`)", line[i + run:])
                if close:
                    i += run + close.end()
                    continue
            if line.startswith("<!--", i):
                end = line.find("-->", i + 4)
                if end < 0:
                    unmatched = i
                    break
                i = end + 3
            else:
                i += 1
        if unmatched is not None:
            comment = True
            line = line[:unmatched]
        found.extend(lint_line(file, number, line))
    return found


def _yaml_comment(line: str) -> Optional[int]:
    quote = None
    i = 0
    while i < len(line):
        char = line[i]
        if quote == '"' and char == "\\":
            i += 2
            continue
        if quote == "'" and line[i:i + 2] == "''":
            i += 2
            continue
        if char in "'\"":
            if quote is None:
                quote = char
            elif quote == char:
                quote = None
        elif char == "#" and quote is None and (i == 0 or line[i - 1].isspace()):
            return i
        i += 1
    return None


def lint_workflow(file: str, source: str) -> List[Diagnostic]:
    found = []  # type: List[Diagnostic]
    block_indent = None  # type: Optional[int]
    for number, line in enumerate(source.splitlines(), 1):
        indent = len(line) - len(line.lstrip(" "))
        if block_indent is not None:
            if not line.strip() or indent > block_indent:
                continue
            block_indent = None
        comment_at = _yaml_comment(line)
        value = line if comment_at is None else line[:comment_at]
        # All block scalar contents, including shell comments, are opaque.
        if re.search(r":\s*[>|][+\-0-9]*\s*$", value):
            block_indent = indent
        else:
            match = re.match(r"^\s*(?:-\s+)?(?:name|run-name|description):\s*(.*)$", value)
            if match:
                begin, end = match.start(1), len(value.rstrip())
                if begin < end and value[begin] in "'\"" and value[end - 1] == value[begin]:
                    begin, end = begin + 1, end - 1
                found.extend(lint_line(file, number, " " * begin + value[begin:end]))
        if comment_at is not None:
            found.extend(lint_line(file, number, " " * (comment_at + 1) + line[comment_at + 1:]))
    return found


def discover(root: Path) -> List[Path]:
    paths = set(root.glob("*.md"))
    paths.update((root / "docs").rglob("*.md"))
    for suffix in ("*.yml", "*.yaml"):
        paths.update((root / ".github" / "workflows").glob(suffix))
    return sorted(path for path in paths if path.is_file())


def check_paths(root: Path, paths: Iterable[Path]) -> List[Diagnostic]:
    found = []  # type: List[Diagnostic]
    for path in sorted(set(paths)):
        relative = path.resolve().relative_to(root.resolve()).as_posix()
        if path.suffix not in (".md", ".yml", ".yaml"):
            raise ValueError("対象は Markdown / YAML のみです: " + relative)
        source = path.read_text(encoding="utf-8")
        lint = lint_markdown if path.suffix == ".md" else lint_workflow
        found.extend(lint(relative, source))
    return sorted(found, key=lambda d: (d.file, d.line, d.column, d.rule))


def apply_exceptions(found: Sequence[Diagnostic], exception_path: Optional[Path]) -> List[Diagnostic]:
    """Allow only exact, reasoned diagnostic exceptions; reject stale entries."""
    if exception_path is None:
        return list(found)
    entries = json.loads(exception_path.read_text(encoding="utf-8"))
    if not isinstance(entries, list):
        raise ValueError("例外は JSON 配列で指定してください。")
    required = {"file", "line", "rule", "fragment", "reason"}
    remaining = list(found)
    for entry in entries:
        if (not isinstance(entry, dict) or set(entry) != required or
                type(entry["line"]) is not int or entry["line"] < 1 or
                any(not isinstance(entry[key], str) or not entry[key].strip()
                    for key in required - {"line"})):
            raise ValueError("例外には file/line/rule/fragment/reason の完全な指定が必要です。")
        matches = [d for d in remaining if all(getattr(d, key) == entry[key]
                   for key in ("file", "line", "rule", "fragment"))]
        if len(matches) != 1:
            raise ValueError("例外は現在の診断 1 件へ一致する必要があります: " + entry["file"])
        remaining.remove(matches[0])
    return remaining


def main(argv: Optional[Sequence[str]] = None) -> int:
    # Deterministic UTF-8 diagnostics also when Windows uses a legacy locale.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--exceptions", type=Path, help="原文引用などの理由を持つ完全一致の JSON 例外配列")
    parser.add_argument("paths", nargs="*", help="root からの相対パス。省略時は文書と workflow を検査")
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve(strict=True)
        paths = [root / path for path in args.paths] if args.paths else discover(root)
        if not paths:
            raise ValueError("検査対象がありません。")
        found = apply_exceptions(check_paths(root, paths), args.exceptions)
    except (OSError, UnicodeError, ValueError) as error:
        print("writing check: " + str(error), file=sys.stderr)
        return 2
    for diagnostic in found:
        print(diagnostic.format())
    if found:
        print("表記検査: {} ファイル、{} 件の違反".format(len(paths), len(found)))
        return 1
    print("表記検査: {} ファイル、違反なし".format(len(paths)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
