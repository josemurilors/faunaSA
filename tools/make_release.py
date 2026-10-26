"""Monta o pacote completo da release: plugin + configuracao + animais (so o que o FaunaSA usa) + modelos do Modloader.

Uso: python tools/make_release.py <pasta do jogo> <versao>   ->   dist/FaunaSA-v<versao>.zip
Le da pasta do jogo ja instalada e testada (rode tools/install_anims.ps1 e build.bat antes). Sons (.mp3) e scripts .cs ficam de fora.
"""
import os, sys, zipfile

KEEP = ('.dff', '.txd', '.ifp', '.ini')
KEEP_NAMES = ('animgrp.dat',)

def main():
    jogo, ver = sys.argv[1], sys.argv[2]
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.makedirs(os.path.join(raiz, 'dist'), exist_ok=True)
    zpath = os.path.join(raiz, 'dist', 'FaunaSA-v%s.zip' % ver)
    n = 0
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        def add(src, arc):
            nonlocal n
            z.write(src, arc.replace(os.sep, '/')); n += 1
        add(os.path.join(raiz, 'out', 'FaunaSA.asi'), 'FaunaSA.asi')
        add(os.path.join(raiz, 'docs', 'FaunaSA.ini'), 'FaunaSA.ini')
        for sub in ('cleo/Animals', 'cleo/Myths', 'modloader/Myths Maker'):
            base = os.path.join(jogo, *sub.split('/'))
            for d, _, fs in os.walk(base):
                for f in sorted(fs):
                    if f.lower().endswith(KEEP) or f.lower() in KEEP_NAMES:
                        add(os.path.join(d, f), os.path.relpath(os.path.join(d, f), jogo))
        for f in ('CREDITS.md', 'LICENSE'):
            add(os.path.join(raiz, f), f)
        z.writestr('LEIA-ME.txt', open(os.path.join(raiz, 'docs', 'LEIA-ME-release.txt'), encoding='utf-8').read().replace('{VERSAO}', ver))
    print('%s: %d arquivos, %.0f MB' % (zpath, n + 1, os.path.getsize(zpath) / 1e6))

main()
