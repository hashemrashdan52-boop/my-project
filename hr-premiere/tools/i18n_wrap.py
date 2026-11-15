#!/usr/bin/env python3
"""Wraps the user visible string literals of the C++ sources in TR("...").

    i18n_wrap.py [--apply] files...

Without --apply it only prints what would be wrapped (and the prose-like literals it leaves alone, so they can be
reviewed).  A literal is wrapped when it is an argument of a call that shows text to the user (a label, a tool tip,
a menu entry, a message box, an undo text ...).  Wrapping a text that has no Arabic entry is harmless (the English
text is shown), so the rules lean towards wrapping."""
import re, sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import i18n_scan as sc

# callee regex -> argument indexes whose string literal is shown to the user
RULES = [
    (r'(.*[\.>])?addAction', {0, 1}),
    (r'.*addMenu', {0}),
    (r'.*create(Undo|Redo)Action', {1}),
    (r'makeDock', {0}),
    (r'.*setWindowTitle', {0}),
    (r'.*(setToolTip|setStatusTip|setWhatsThis|setPlaceholderText|setPlaceholder|setEmptyLabel|setText|setTitle|setLabelText|setCaption)', {0}),
    (r'.*(setHeaderLabels|setHorizontalHeaderLabels)\{', {0, 1, 2, 3}),
    (r'(QLabel|QPushButton|QCheckBox|QRadioButton|QGroupBox|QToolButton|QAction)', {0}),
    (r'.*(addItem|addRow|insertItem)', {0}),
    (r'.*addTab', {1}),
    (r'(.*[\.>])?(setStatus|say|setMessage|showMessage)', {0}),
    (r'(.*[\.>])?edit', {0}),
    (r'.*beginMacro', {0}),
    (r'QMessageBox::\w+', {1, 2}),
    (r'QFileDialog::\w+', {1, 3}),
    (r'QInputDialog::\w+', {1, 2}),
    (r'tbtn', {1}),
    (r'card', {0}),
    (r'ops::makeTitle', {2}),
]
CSS = re.compile(r'^\s*[\w\-#\.\s:,]*[a-z\-]+\s*:\s*[^;]+;?\s*$|^\s*Q[A-Z]\w*[\s\{#:]')
PROSE = re.compile(r'[A-Za-z]{3}')

def arg_index(src, start):
    depth = 0; idx = 0; j = start - 1
    while j >= 0:
        ch = src[j]
        if ch in ')]}': depth += 1
        elif ch in '([{':
            if depth == 0: break
            depth -= 1
        elif ch == ',' and depth == 0: idx += 1
        j -= 1
    return idx

def decide(src, s, text):
    if not PROSE.search(text): return False, ''
    if CSS.match(text) or text.startswith(('.', '#', '/', ':/')) or re.fullmatch(r'[\w/\.\-]+', text) and ' ' not in text and not text[0].isupper():
        return False, 'technical'
    if src[max(0, s - 3):s] == 'TR(' or src[max(0, s - 17):s].endswith('QStringLiteral('): return False, 'already'
    c = sc.callee(src, s)
    i = arg_index(src, s)
    for pat, idxs in RULES:
        if re.fullmatch(pat, c) and i in idxs:
            return True, f'{c}#{i}'
    if c == 'QString' and i == 0 and (' ' in text or '%' in text) and not CSS.match(text):
        return True, 'QString'
    return False, f'{c}#{i}'

def process(path, apply):
    src = pathlib.Path(path).read_text(encoding='utf-8')
    edits, left = [], []
    for s, e, text, line in sc.merged_literals(src):
        ok, why = decide(src, s, text)
        if ok: edits.append((s, e, text, line, why))
        elif PROSE.search(text) and why not in ('technical', 'already') and (' ' in text or text[:1].isupper()):
            left.append((line, why, text))
    print(f'== {path}: wrap {len(edits)}, left {len(left)}')
    if not apply:
        for line, why, text in left: print(f'   left  {line:4d} [{why}] {text[:100]}')
        return
    out = src
    for s, e, text, line, why in sorted(edits, reverse=True):
        out = out[:s] + 'TR(' + out[s:e] + ')' + out[e:]
    if edits and '#include "core/I18n.h"' not in out:
        m = re.search(r'#include "[^"]+"\n', out)
        out = out[:m.start()] + '#include "core/I18n.h"\n' + out[m.start():] if m else '#include "core/I18n.h"\n' + out
    pathlib.Path(path).write_text(out, encoding='utf-8')

if __name__ == '__main__':
    apply = '--apply' in sys.argv
    for p in [a for a in sys.argv[1:] if not a.startswith('--')]:
        process(p, apply)
