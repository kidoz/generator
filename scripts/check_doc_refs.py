#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check markdown docs for stale source-file references.

Scans documentation for tokens that look like in-repo source paths
(``src/...``, ``include/...``, ``tests/...``) and reports:

  1. paths that no longer exist on disk, and
  2. ``.c`` / ``.h`` paths that have a ``.cpp`` / ``.hpp`` sibling (the
     common C->C++ migration rename rot).

This is a read-only lint: it prints findings and exits 1 if any are
found, 0 otherwise. Pure stdlib, no dependencies.

Usage:
    scripts/check_doc_refs.py [path...]
        (default paths: TODO.md, CHANGELOG.md, README.md)
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

# Root of the repo (this script lives in <root>/scripts/).
ROOT = Path(__file__).resolve().parent.parent

# A source-like token: optional repo-rooted path containing src/|include/|tests/
# followed by a filename with a C/C++ extension. Captures the path and an
# optional trailing :LINENO[:COL] reference.
PATH_RE = re.compile(
    r'(?<![A-Za-z0-9_/.-])'                       # no leading path char
    r'((?:src|include|tests)/[A-Za-z0-9_./-]+?'   # repo-rooted source path
    r'\.(?:c|h|cpp|hpp|cc))'                       # C/C++ extension
    r'(?::\d+)?'                                   # optional :lineno
    r'(?![A-Za-z0-9_])'                            # not followed by ident char
)

RENAME_SIBLINGS = {'.c': '.cpp', '.h': '.hpp'}

# Skip these matched substrings: they are clearly example/template
# placeholders, not real file claims (e.g. "<path/to/good_example>", or the
# path is inside a fenced code block illustrating a command).
PLACEHOLDER_RE = re.compile(r'[<>]')

# When a line contains one of these markers, any source path on it is an
# intentional documentary reference to a file that was removed/renamed as
# part of a migration (e.g. "obsolete — the `src/...z80.c` backend was
# removed", or "(was foo.c)"). We don't want to flag those; only stale
# claims that a path is current. Match case-insensitively against the whole
# line. Keep these specific enough not to mask real findings.
OBSOLETE_MARKERS = (
    'obsolete',
    'was removed',
    'were removed',
    'no longer',
    'migrated to',
    'moved to',
    'before the c->c++',
    'before the c -> c++',
    'older notes',
    'some older',
    'earlier drafts',
    '(was ',          # "foo.cpp (was foo.c)" — explicit rename annotation
    '(removed)',      # explicit removal annotation
)


def default_targets() -> list[Path]:
    """Docs to scan by default. Any other documentation set is scanned by
    passing its paths as arguments."""
    targets: list[Path] = []
    for name in ('TODO.md', 'CHANGELOG.md', 'README.md'):
        p = ROOT / name
        if p.exists():
            targets.append(p)
    return targets


def check_path(raw_path: str) -> tuple[str | None, str | None]:
    """Return (missing_error, rename_error) for a matched path, or (None, None)."""
    target = ROOT / raw_path
    if target.exists():
        # Exists — check for a renamed sibling only if it's a .c/.h.
        ext = os.path.splitext(raw_path)[1]
        if ext in RENAME_SIBLINGS:
            sibling = target.with_suffix(RENAME_SIBLINGS[ext])
            if sibling.exists():
                return (None, f"renamed: '{raw_path}' exists but '{sibling.relative_to(ROOT)}' also present (C->C++ migration)")
        return (None, None)

    # Missing on disk — but the .cpp/.hpp sibling may exist (pure rename).
    ext = os.path.splitext(raw_path)[1]
    if ext in RENAME_SIBLINGS:
        sibling = target.with_suffix(RENAME_SIBLINGS[ext])
        if sibling.exists():
            return (None, f"renamed: '{raw_path}' -> '{sibling.relative_to(ROOT)}'")

    # Genuinely missing — but skip obvious placeholders/template strings.
    if PLACEHOLDER_RE.search(raw_path):
        return (None, None)
    return (f"missing: '{raw_path}'", None)


def is_archived(path: Path, text: str) -> bool:
    """A decision record / context doc marked superseded/archived/deprecated in
    its YAML frontmatter is a frozen historical record. Old paths inside it are
    intentional history, not rot — skip it entirely."""
    if not text.startswith('---'):
        return False
    end = text.find('\n---', 3)
    if end == -1:
        return False
    frontmatter = text[3:end]
    m = re.search(r'^status:\s*["\']?(\w+)["\']?\s*$', frontmatter, re.MULTILINE)
    if m:
        return m.group(1).lower() in ('superseded', 'archived', 'deprecated', 'historical')
    return False


def scan_file(path: Path) -> list[tuple[int, str, str]]:
    """Return list of (lineno, matched_path, message) findings for one file."""
    findings: list[tuple[int, str, str]] = []
    try:
        text = path.read_text(encoding='utf-8', errors='replace')
    except OSError as exc:
        return [(0, '', f"unreadable: {exc}")]
    if is_archived(path, text):
        return []
    for lineno, line in enumerate(text.splitlines(), start=1):
        line_lower = line.lower()
        obsolescence_documented = any(marker in line_lower for marker in OBSOLETE_MARKERS)
        for match in PATH_RE.finditer(line):
            raw = match.group(1)
            # If the line already documents that this path is obsolete /
            # renamed, don't re-report it — that prose is the fix.
            if obsolescence_documented:
                continue
            missing, rename = check_path(raw)
            # Prefer the rename signal (more actionable) over missing.
            msg = rename or missing
            if msg:
                findings.append((lineno, raw, msg))
    return findings


def main(argv: list[str]) -> int:
    if len(argv) > 1:
        targets = [Path(a).resolve() for a in argv[1:]]
    else:
        targets = default_targets()

    total = 0
    for path in sorted(set(targets)):
        findings = scan_file(path)
        if not findings:
            continue
        rel = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
        for lineno, raw, msg in findings:
            total += 1
            print(f"{rel}:{lineno}: {msg}")
            if raw:
                print(f"    path: {raw}")
    if total:
        print(f"\n{total} stale doc reference(s) found.", file=sys.stderr)
        return 1
    print("No stale doc references found.", file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
