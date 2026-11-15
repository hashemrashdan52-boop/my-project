#!/usr/bin/env python3
"""Appends English -> Arabic pairs to src/core/ar/<name>.inc (the Arabic table of the program).
    from ar_add import add
    add('menus', {'Open Project…': 'فتح مشروع…', ...})
Checks that both texts use the same %1 %2 ... markers and that a text is not translated twice."""
import re, pathlib, glob

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIR = ROOT / 'src/core/ar'

def esc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t')

def existing():
    have = {}
    for f in sorted(glob.glob(str(DIR / '*.inc'))):
        for m in re.finditer(r'\{"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\},', pathlib.Path(f).read_text(encoding='utf-8')):
            have[m.group(1)] = f
    return have

def add(name, pairs):
    DIR.mkdir(parents=True, exist_ok=True)
    have = existing()
    lines, skipped = [], 0
    for en, ar in pairs.items():
        if sorted(re.findall(r'%\d', en)) != sorted(re.findall(r'%\d', ar)):
            raise SystemExit(f'placeholder mismatch: {en!r} -> {ar!r}')
        if esc(en) in have:
            skipped += 1
            continue
        lines.append(f'    {{"{esc(en)}", "{esc(ar)}"}},')
    path = DIR / f'{name}.inc'
    old = path.read_text(encoding='utf-8') if path.exists() else ''
    path.write_text(old + '\n'.join(lines) + ('\n' if lines else ''), encoding='utf-8')
    print(f'{name}.inc: +{len(lines)} (skipped {skipped} already translated)')
