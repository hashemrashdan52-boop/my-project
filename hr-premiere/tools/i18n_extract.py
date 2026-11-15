#!/usr/bin/env python3
"""Lists every text that needs an Arabic translation: the TR("...") literals of the sources plus the effect names,
categories, labels and choices of the effect registry.

    i18n_extract.py [--missing] [--json]

--missing  only the texts that have no entry yet in src/core/ar/*.inc
The texts are printed decoded (a newline is shown as \\n)."""
import re, sys, json, pathlib, glob

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import i18n_scan as sc

def decode(raw):
    out, i = [], 0
    while i < len(raw):
        c = raw[i]
        if c == '\\' and i + 1 < len(raw):
            n = raw[i + 1]
            out.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'"}.get(n, n))
            i += 2
        else:
            out.append(c); i += 1
    return ''.join(out)

def tr_literals(path):
    src = pathlib.Path(path).read_text(encoding='utf-8')
    found = []
    for m in re.finditer(r'\bTR\(', src):
        j = m.end()
        # one or more adjacent string literals
        text = ''
        k = j
        while True:
            while k < len(src) and src[k] in ' \t\r\n': k += 1
            if k < len(src) and src[k] == '"':
                e = k + 1
                while e < len(src) and src[e] != '"': e += 2 if src[e] == '\\' else 1
                text += src[k + 1:e]
                k = e + 1
            else: break
        if text and src[k:k + 1] in (')', ','):
            line = src.count('\n', 0, m.start()) + 1
            found.append((decode(text), f'{pathlib.Path(path).name}:{line}'))
    return found

def effect_texts():
    src = (ROOT / 'src/core/Effects.cpp').read_text(encoding='utf-8')
    out = []
    for s, e, text, line in sc.merged_literals(src):
        c = sc.callee(src, s)
        # arg index
        depth = 0; idx = 0; j = s - 1
        while j >= 0:
            ch = src[j]
            if ch in ')]}': depth += 1
            elif ch in '([{':
                if depth == 0: break
                depth -= 1
            elif ch == ',' and depth == 0: idx += 1
            j -= 1
        t = decode(text)
        if c == 'add' and idx in (1, 2) and t: out.append((t, f'Effects.cpp:{line}'))
        elif c in ('num', 'boolean', 'text', 'color', 'font', 'choice') and idx == 1: out.append((t, f'Effects.cpp:{line}'))
        elif c == '{' and re.search(r'choice\([^;]*\{', src[max(0, s - 160):s]) and idx <= 4 and re.fullmatch(r'[A-Za-z][A-Za-z ]*', t): out.append((t, f'Effects.cpp:{line}'))
    return out

def known():
    have = set()
    for f in glob.glob(str(ROOT / 'src/core/ar/*.inc')):
        for m in re.finditer(r'\{"((?:[^"\\]|\\.)*)",\s*"', pathlib.Path(f).read_text(encoding='utf-8')):
            have.add(decode(m.group(1)))
    return have

def all_texts():
    files = [p for pat in ('src/ui/*.cpp', 'src/main.cpp', 'src/ai/*.cpp', 'src/engine/*.cpp', 'src/media/*.cpp', 'src/core/*.cpp') for p in glob.glob(str(ROOT / pat))]
    seen = {}
    for f in sorted(files):
        for t, where in tr_literals(f):
            seen.setdefault(t, where)
    for t, where in effect_texts():
        seen.setdefault(t, where)
    return seen

if __name__ == '__main__':
    texts = all_texts()
    have = known()
    items = [(t, w) for t, w in texts.items() if not ('--missing' in sys.argv and t in have)]
    if '--json' in sys.argv:
        print(json.dumps({t: w for t, w in items}, ensure_ascii=False, indent=1))
    else:
        for t, w in items: print(f'{w:34s} {t!r}')
        print(f'-- {len(items)} text(s), {len([1 for t in texts if t in have])} of {len(texts)} already translated', file=sys.stderr)
