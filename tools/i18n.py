#!/usr/bin/env python3
"""Localizzazione di Stackly.

Estrae i testi italiani marcati nel codice (TR(L"...") e N_(L"...")) e quelli delle risorse
(src/app.rc: titoli, etichette, caselle, pulsanti, voci di menu), li confronta con le traduzioni
in tools/lang_en.json e scrive src/lang_table.inc.

    python3 tools/i18n.py            # genera la tabella; errore se manca una traduzione
    python3 tools/i18n.py --list     # stampa i testi estratti (JSON) senza generare nulla
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
TRANSLATIONS = os.path.join(ROOT, 'tools', 'lang_en.json')
OUT = os.path.join(SRC, 'lang_table.inc')

LIT = r'L"((?:[^"\\]|\\.)*)"'
CODE_RE = re.compile(r'\b(?:TR|N_)\(\s*' + LIT + r'\s*\)')
RC_RE = re.compile(r'^\s*(?:CAPTION|LTEXT|RTEXT|CTEXT|AUTOCHECKBOX|CHECKBOX|PUSHBUTTON|DEFPUSHBUTTON|MENUITEM|POPUP)\s+"((?:[^"]|"")*)"', re.M)


def decode_c(body):
    """Decodifica le sequenze di escape di un letterale C++ wide."""
    out, i = [], 0
    simple = {'n': '\n', 't': '\t', 'r': '\r', '\\': '\\', '"': '"', "'": "'", '0': '\0'}
    while i < len(body):
        c = body[i]
        if c != '\\':
            out.append(c); i += 1; continue
        n = body[i + 1]
        if n in simple:
            out.append(simple[n]); i += 2
        elif n == 'u':
            out.append(chr(int(body[i + 2:i + 6], 16))); i += 6
        elif n == 'U':
            out.append(chr(int(body[i + 2:i + 10], 16))); i += 10
        elif n == 'x':
            j = i + 2
            while j < len(body) and body[j] in '0123456789abcdefABCDEF': j += 1
            out.append(chr(int(body[i + 2:j], 16))); i = j
        else:
            raise ValueError('escape non gestito: \\' + n)
    return ''.join(out)


def encode_c(text):
    """Letterale C++ wide sicuro: ASCII stampabile, il resto come \\uXXXX."""
    out = []
    for ch in text:
        o = ord(ch)
        if ch == '\\': out.append('\\\\')
        elif ch == '"': out.append('\\"')
        elif ch == '\n': out.append('\\n')
        elif ch == '\t': out.append('\\t')
        elif ch == '\r': out.append('\\r')
        elif 32 <= o < 127: out.append(ch)
        elif o <= 0xFFFF: out.append('\\u%04x' % o)
        else: out.append('\\U%08x' % o)
    return 'L"' + ''.join(out) + '"'


def extract():
    found = {}
    for name in sorted(os.listdir(SRC)):
        path = os.path.join(SRC, name)
        if name.endswith(('.h', '.cpp')) and name != 'lang.h':
            for m in CODE_RE.finditer(open(path, encoding='utf-8').read()):
                found.setdefault(decode_c(m.group(1)), name)
        elif name.endswith('.rc'):
            text = open(path, encoding='utf-8').read()
            for m in RC_RE.finditer(text):
                s = m.group(1).replace('""', '"').replace('\\t', '\t')
                if s and s != 'list':
                    found.setdefault(s, name)
    return found


def main():
    found = extract()
    if '--list' in sys.argv:
        print(json.dumps(sorted(found), ensure_ascii=False, indent=1))
        return 0
    tr = json.load(open(TRANSLATIONS, encoding='utf-8'))
    missing = [s for s in found if s not in tr]
    unused = [s for s in tr if s not in found]
    if unused:
        print('Traduzioni non piu\' usate (%d):' % len(unused))
        for s in unused: print('  ', repr(s))
    if missing:
        print('MANCANO %d traduzioni:' % len(missing))
        for s in missing: print('  ', repr(s), '(' + found[s] + ')')
        return 1
    lines = ['// Generato da tools/i18n.py da tools/lang_en.json: non modificare a mano.']
    for s in sorted(found):
        lines.append('{ %s, %s },' % (encode_c(s), encode_c(tr[s])))
    open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print('lang_table.inc: %d voci' % len(found))
    return 0


if __name__ == '__main__':
    sys.exit(main())
