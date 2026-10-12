#!/usr/bin/env python3
"""Independent positive/negative controls for the read-only writing checker."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools" / "verify_writing.py"
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("verify_writing", CHECKER)
WRITING = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = WRITING
SPEC.loader.exec_module(WRITING)


class ProseControls(unittest.TestCase):
    def lint(self, text):
        return WRITING.lint_markdown("README.md", text)

    def assertClean(self, text):
        self.assertEqual([], self.lint(text), text)

    def test_japanese_latin_numeric_boundaries(self):
        pairs = [
            ("APIを使う", "API を使う"),
            ("日本でMacを使う", "日本で Mac を使う"),
            ("M1搭載のMac", "M1 搭載の Mac"),
            ("macOSの更新", "macOS の更新"),
            ("2種類、最大5時間、第2世代", "2 種類、最大 5 時間、第 2 世代"),
            ("10件と3回", "10 件と 3 回"),
            ("2026年10月6日", "2026 年 10 月 6 日"),
            ("10,000円", "10,000 円"),
            ("約50%の割引", "約 50 % の割引"),
            ("90°の角度", "90° の角度"),
            ("C++を使う", "C++ を使う"),
            ("v0.1.1の候補", "v0.1.1 の候補"),
        ]
        for bad, good in pairs:
            with self.subTest(bad=bad):
                self.assertTrue(self.lint(bad))
                self.assertClean(good)

    def test_units_and_percent(self):
        for bad, good in [
            ("4.3g", "4.3 g"), ("30W", "30 W"), ("30.2mm", "30.2 mm"),
            ("120Hz", "120 Hz"), ("25°C", "25 °C"), ("256GB", "256 GB"),
            ("10Gbps", "10 Gbps"), ("32MiB", "32 MiB"), ("95%", "95 %"),
            ("150,000bytes", "150,000 bytes"), ("1.25MB/s", "1.25 MB/s"),
            ("10fixtures", "10 fixtures"), ("44symbols", "44 symbols"),
            ("25 ° C", "25 °C"), ("90 °", "90°"), ("30 ′", "30′"),
            ("15 ″", "15″"),
        ]:
            with self.subTest(bad=bad):
                self.assertTrue(self.lint(bad))
                self.assertClean(good)

    def test_exact_single_ascii_space(self):
        for gap in ("", "  ", "\t", "\u3000", "\u00a0", "\u2009", " \u00a0"):
            for left, right in (("API", "を"), ("約", "50"), ("10", "件"), ("95", "%"), ("32", "MiB")):
                with self.subTest(gap=repr(gap), left=left):
                    self.assertTrue(self.lint(left + gap + right))
                    self.assertClean(left + " " + right)

    def test_preserved_spellings_and_punctuation(self):
        self.assertClean("macOS、M1、USB-C、4K、MacBook Air、iPhone 16e。")
        self.assertClean("M1 搭載、4K の映像。「Mac」、Mac（税込）、Mac を使う。")
        self.assertClean("90°30′15″。時刻 7:30、日付 2026-10-06、比率 3:2、範囲 1-40。")
        self.assertClean("Version 3.0.0-rc.1、25 °C、GB、Gb、Mbps、MB/s。")
        self.assertClean("漢数字の一件、三回、二〇二六年。")

    def test_markup_visible_boundaries(self):
        pairs = [
            ("**API**を使う", "**API** を使う"),
            ("日本*API*", "日本 *API*"),
            ("__API__を使う", "__API__ を使う"),
            ("~~API~~を使う", "~~API~~ を使う"),
            ("**APIを使う**", "**API を使う**"),
            ("[API](https://example.test/APIを使う)を使う", "[API](https://example.test/APIを使う) を使う"),
            ("[API][ref]を使う", "[API][ref] を使う"),
            ("![APIの図](images/a.png)", "![API の図](images/a.png)"),
            ("<strong>Mac</strong>の設定", "<strong>Mac</strong> の設定"),
            ('日本<span title="APIを使う">API</span>', '日本 <span title="APIを使う">API</span>'),
            ('<a title="2>1" href="https://example.test/APIを">API</a>を使う',
             '<a title="2>1" href="https://example.test/APIを">API</a> を使う'),
            ("API<!-- comment -->を使う", "API<!-- comment --> を使う"),
            ("日本&#65;PI", "日本 &#65;PI"),
            ("API&nbsp;を使う", "API を使う"),
            ("**32**MiB", "**32** MiB"),
            ("[95](https://example.test)%", "[95](https://example.test) %"),
        ]
        for bad, good in pairs:
            with self.subTest(bad=bad):
                self.assertTrue(self.lint(bad))
                self.assertClean(good)

    def test_inline_code_and_protected_tokens(self):
        self.assertClean("`APIを使う 95% 30W` を保存する。")
        self.assertClean("`` `APIを使う` 95% `` を保存する。")
        self.assertClean("<code>APIを使う 30W</code> を保存する。")
        self.assertClean("URL https://example.test/日本語/APIを使う?q=30W とメール a_10件@example.test。")
        self.assertClean("https://example.test/30W a@example.test docs/30W.md src/reader.hpp reader_api specqr::decode C++")
        self.assertClean("30W.pdf 95%.txt skill://example/30W git+https://example.test/30W")
        self.assertClean("パス `docs/APIを使う.md` と識別子 `変数API名` を保存する。")
        for token in ("`APIを使う`", "`` `API` ``", "<code>APIを使う</code>",
                      "<https://example.test/30W>", "<a@example.test>",
                      "reader_api", "specqr::decode", "docs/30W.md", "30W.pdf"):
            with self.subTest(token=token):
                self.assertTrue(self.lint(token + "を保存する。"))
                self.assertClean(token + " を保存する。")

    def test_fences_comments_and_indented_code(self):
        self.assertClean("```text\nAPIを使う 95%\n```\nAPI を使う。")
        self.assertClean("~~~text\nAPIを使う\n~~~\nAPI を使う。")
        self.assertClean("````text\n```\nAPIを使う\n````")
        self.assertClean("    APIを使う\n\t95%")
        self.assertClean("<!-- APIを\n使う 95% -->API を使う。")
        self.assertTrue(self.lint("<!-- quoted\ntext -->APIを使う。"))
        self.assertTrue(self.lint("`<!--` APIを使う。"))
        self.assertClean("[ref]: https://example.test/APIを使う 'original 95%'\n[API][ref] を使う。")
        self.assertClean("<pre>\nAPIを使う\n</pre>\nAPI を使う。")

    def test_no_broad_quote_exemption(self):
        self.assertTrue(self.lint("> APIを使う"))
        self.assertTrue(self.lint('原文「APIを使う」はそのまま引用。'))
        self.assertTrue(self.lint('"APIを使う"'))
        self.assertClean("> API を使う")

    def test_diagnostic_location(self):
        found = self.lint("見出し\n**API**を使う。")
        self.assertEqual(1, len(found))
        self.assertEqual(("README.md", 2, 5, "ja-ascii-space", "I**を"),
                         (found[0].file, found[0].line, found[0].column, found[0].rule, found[0].fragment))

    def test_terminology(self):
        for bad, good in (("QR Model 2", "QR Code Model 2"), ("Github", "GitHub"),
                          ("Structured append", "Structured Append"), ("ZXingJava", "ZXing Java")):
            self.assertTrue(self.lint(bad))
            self.assertClean(good)
        self.assertClean("`QR Model 2` と `Github` は原文の例。")


class WorkflowControls(unittest.TestCase):
    def lint(self, source):
        return WRITING.lint_workflow(".github/workflows/ci.yml", source)

    def test_display_names_and_comments(self):
        bad = "name: APIの検査\njobs:\n  core:\n    name: 'Linuxの検査'\n    steps:\n      - name: APIを検査\n        run: make test # 10件を検査\n# 95%を確認\n"
        good = bad.replace("APIの", "API の").replace("Linuxの", "Linux の").replace("APIを", "API を").replace("10件", "10 件").replace("95%を", "95 % を")
        self.assertEqual(6, len(self.lint(bad)))
        self.assertEqual([], self.lint(good))

    def test_code_expressions_and_yaml_syntax_preserved(self):
        source = """name: 'Linux ${{ matrix.os }} の検査'
run-name: "# API を確認"
jobs:
  APIを使う:
    steps:
      - name: API を検査
        run: |
          # APIを使う 95%
          echo 'APIを使う'
      - run: "echo 'APIを使う # 95%'"
      - uses: actions/checkout@abc # v4.2.2
"""
        self.assertEqual([], self.lint(source))
        self.assertTrue(self.lint("name: 日本${{ matrix.os }}"))
        self.assertTrue(self.lint("run: |\n  # opaque\nname: APIを使う"))

    def test_multiline_display_values_are_documented_manual_scope(self):
        self.assertEqual([], self.lint("name: >-\n  APIを使う\n"))


class FilesAndExceptions(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="specqr-writing-")
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def put(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def cli(self, *args):
        return subprocess.run([sys.executable, str(CHECKER), "--root", str(self.root)] + list(args),
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8",
                              env=dict(__import__("os").environ, PYTHONIOENCODING="utf-8", PYTHONUTF8="1"), timeout=20)

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in self.root.rglob("*") if p.is_file()}

    def test_discovery_read_only_repeatability_and_cli_exit(self):
        self.put("README.md", "APIを使う\n")
        self.put("docs/nested/api.md", "95%\n")
        self.put(".github/workflows/ci.yml", "name: APIの検査\n")
        self.put("LICENSE", "95% APIを使う")
        self.put("third_party/notices/UPSTREAM.txt", "95% APIを使う")
        self.put("tests/fixture.md", "95% APIを使う")
        before = self.snapshot()
        paths = WRITING.discover(self.root)
        self.assertEqual(3, len(paths))
        first = WRITING.check_paths(self.root, paths)
        self.assertEqual(first, WRITING.check_paths(self.root, paths))
        run1, run2 = self.cli(), self.cli()
        self.assertEqual(1, run1.returncode)
        self.assertEqual((run1.stdout, run1.stderr), (run2.stdout, run2.stderr))
        self.assertEqual(before, self.snapshot())
        self.put("README.md", "API を使う\n")
        self.put("docs/nested/api.md", "95 %\n")
        self.put(".github/workflows/ci.yml", "name: API の検査\n")
        corrected = self.snapshot()
        self.assertEqual(0, self.cli().returncode)
        self.assertEqual(corrected, self.snapshot())

    def test_exact_reasoned_quote_exception_and_stale_rejection(self):
        self.put("README.md", "> APIを使う\n\nAPIを使う\n")
        exception = [{"file": "README.md", "line": 1, "rule": "ja-ascii-space",
                      "fragment": "Iを", "reason": "原典の直接引用をそのまま保持する。"}]
        path = self.put("exceptions.json", json.dumps(exception, ensure_ascii=False))
        self.assertEqual(1, self.cli("--exceptions", str(path)).returncode)
        self.put("README.md", "> APIを使う\n\nAPI を使う\n")
        before = self.snapshot()
        self.assertEqual(0, self.cli("--exceptions", str(path)).returncode)
        self.assertEqual(before, self.snapshot())
        self.put("README.md", "> API を使う\n\nAPI を使う\n")
        self.assertEqual(2, self.cli("--exceptions", str(path)).returncode)

    def test_invalid_or_broad_exceptions_rejected(self):
        self.put("README.md", "APIを使う\n")
        for entry in [
            {"file": "README.md", "reason": "ignore whole file"},
            {"file": "README.md", "line": 1, "rule": "*", "fragment": "Iを", "reason": "wildcard"},
            {"file": "README.md", "line": 1, "rule": "ja-ascii-space", "fragment": "Iを", "reason": ""},
            {"file": "README.md", "line": True, "rule": "ja-ascii-space", "fragment": "Iを", "reason": "wrong type"},
        ]:
            path = self.put("exceptions.json", json.dumps([entry]))
            self.assertEqual(2, self.cli("--exceptions", str(path)).returncode)

    def test_explicit_paths_errors_and_no_targets(self):
        self.assertEqual(2, self.cli().returncode)
        self.put("README.md", "API を使う")
        self.put("docs/api.md", "APIを使う")
        self.assertEqual(0, self.cli("README.md").returncode)
        self.assertEqual(2, self.cli("missing.md").returncode)
        self.assertEqual(2, self.cli("../outside.md").returncode)
        self.assertEqual(2, self.cli("LICENSE").returncode)


if __name__ == "__main__":
    unittest.main(verbosity=2)
