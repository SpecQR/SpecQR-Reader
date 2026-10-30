#!/usr/bin/env python3
"""Check or update the source-only SHA-256 manifest (Python standard library).

Generated builds, browser assets, evidence and Git metadata are not source.
Package only the listed paths plus MANIFEST.sha256; never archive a work tree
recursively. A runnable browser distribution has a separate artifact manifest.
"""
import argparse
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'MANIFEST.sha256'


def excluded(path):
    parts = path.relative_to(ROOT).parts
    if any(p in {'.git', '__pycache__', '.DS_Store'} for p in parts):
        return True
    if path.suffix == '.pyc' or parts == ('MANIFEST.sha256',):
        return True
    if len(parts) > 1 and (parts[0].startswith('build') or parts[0] in {'out', 'install', 'evidence'}):
        return True
    if len(parts) > 1 and parts[0] == 'browser':
        if len(parts) > 2 and (parts[1].startswith('build') or parts[1] == 'evidence'):
            return True
        if parts[:3] == ('browser', 'trial', 'assets'):
            return True
    return False


def contents():
    rows = []
    # Compare case-sensitive relative components on every platform.
    for path in sorted(ROOT.rglob('*'), key=lambda path: path.relative_to(ROOT).parts):
        if excluded(path):
            continue
        if path.is_symlink():
            raise ValueError('source symlinks are not supported: ' + str(path))
        if path.is_file():
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            rows.append(digest + '  ' + path.relative_to(ROOT).as_posix())
    return '\n'.join(rows) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--update', action='store_true', help='rewrite the source manifest')
    args = parser.parse_args()
    expected = contents()
    if args.update:
        MANIFEST.write_text(expected, encoding='utf-8')
    elif not MANIFEST.exists() or MANIFEST.read_text(encoding='utf-8') != expected:
        print('Source manifest mismatch; inspect changes, then run tools/source_manifest.py --update.', file=sys.stderr)
        return 1
    print('Source manifest matches: {} files (excluding manifest itself).'.format(len(expected.splitlines())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
