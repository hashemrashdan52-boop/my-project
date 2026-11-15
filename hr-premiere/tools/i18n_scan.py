#!/usr/bin/env python3
"""Lists the string literals of the C++ sources together with the call they appear in.
Used to find the texts that need an Arabic translation (TR("...")) and to check that none is missing."""
import re, sys, collections, pathlib

def literals(src):
    """yield (start, end, text, line) of every ordinary "..." literal, skipping comments, chars and raw strings"""
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if c == '\n': line += 1; i += 1; continue
        if src.startswith('//', i):
            j = src.find('\n', i); i = n if j < 0 else j; continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); j = n if j < 0 else j + 2
            line += src.count('\n', i, j); i = j; continue
        if c == 'R' and src.startswith('R"', i):
            m = re.match(r'R"([^(\s]*)\(', src[i:])
            if m:
                end = ')' + m.group(1) + '"'
                j = src.find(end, i + len(m.group(0)))
                j = n if j < 0 else j + len(end)
                line += src.count('\n', i, j); i = j; continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == '\\' else 1
            i = j + 1; continue
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == '\\' else 1
            yield i, j + 1, src[i + 1:j], line
            i = j + 1; continue
        i += 1

def merged_literals(src):
    """like literals(), but adjacent literals separated only by white space (C++ concatenation) are one item:
    yields (start, end, text, line) with the texts joined"""
    items = list(literals(src))
    i = 0
    while i < len(items):
        s, e, text, line = items[i]
        j = i + 1
        while j < len(items) and src[e:items[j][0]].strip() == '':
            e = items[j][1]
            text += items[j][2]
            j += 1
        yield s, e, text, line
        i = j

def callee(src, start):
    """the identifier of the call / constructor whose argument list the literal belongs to (best effort)"""
    depth = 0
    j = start - 1
    while j >= 0:
        ch = src[j]
        if ch in ')]}': depth += 1
        elif ch in '([{':
            if depth == 0:
                if ch == '(':
                    m = re.search(r'([A-Za-z_][A-Za-z_0-9:>\-\.]*)\s*$', src[max(0, j - 80):j])
                    return m.group(1) if m else '?'
                if ch == '{':
                    m = re.search(r'([A-Za-z_][A-Za-z_0-9:>\-\.]*)\s*\(\s*$', src[max(0, j - 80):j])
                    return (m.group(1) + '{') if m else '{'
                return '['
            depth -= 1
        elif ch == ';' and depth == 0: return ';'
        j -= 1
    return '?'

def main(paths):
    hist = collections.Counter()
    rows = []
    for p in paths:
        src = pathlib.Path(p).read_text(encoding='utf-8')
        for s, e, text, line in literals(src):
            if not re.search(r'[A-Za-z؀-ۿ]{2}', text): continue
            c = callee(src, s)
            hist[c] += 1
            rows.append((p, line, c, text))
    if '--hist' in sys.argv:
        for c, k in hist.most_common(): print(f'{k:4d} {c}')
    else:
        for p, line, c, text in rows: print(f'{p}:{line}: [{c}] {text}')

if __name__ == '__main__':
    main([a for a in sys.argv[1:] if not a.startswith('--')])
