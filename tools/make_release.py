"""Monta o pacote completo da release: plugin + configuracao + animais (so o que o FaunaSA usa) + modelos do Modloader.

Uso: python tools/make_release.py <pasta do jogo> <versao>   ->   dist/FaunaSA-v<versao>.zip
Le da pasta do jogo ja instalada e testada (rode tools/install_anims.ps1 e build.bat antes). Sons (.mp3) dos animais entram; scripts .cs ficam de fora. O cavalo (New Horse Mod) so entra com --cavalo <pasta>, ja que a pagina do mod nao traz termos de redistribuicao.
"""
import os, sys, zipfile

KEEP = ('.dff', '.txd', '.ifp', '.ini', '.mp3')
KEEP_NAMES = ('animgrp.dat',)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    jogo, ver = args[0], args[1]
    horse = sys.argv[sys.argv.index('--cavalo') + 1] if '--cavalo' in sys.argv else None
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
        if horse:  # modelo, animacoes e sons do cavalo, nos mesmos lugares do install_horse.ps1
            for f, arc in (('cavalo.dff', 'modloader/FaunaSA/gta3.img/cavalo.dff'), ('cavalo.txd', 'modloader/FaunaSA/gta3.img/cavalo.txd'), ('horse_animation.ifp', 'FaunaSA/horse/horse_animation.ifp')):
                add(os.path.join(horse, f), arc)
            for f in sorted(os.listdir(os.path.join(horse, 'cleo', 'cleo_audio'))):
                if f.lower().endswith('.mp3'): add(os.path.join(horse, 'cleo', 'cleo_audio', f), 'FaunaSA/horse/' + f)
        if not horse:  # cavalo ja instalado no jogo (modloader\FaunaSA e FaunaSA\horse)
            for sub in ('modloader/FaunaSA', 'FaunaSA/horse'):
                for d, _, fs in os.walk(os.path.join(jogo, *sub.split('/'))):
                    for f in sorted(fs):
                        if f.lower().endswith(KEEP): add(os.path.join(d, f), os.path.relpath(os.path.join(d, f), jogo))
        for f in ('CREDITS.md', 'LICENSE'):
            add(os.path.join(raiz, f), f)
        z.writestr('LEIA-ME.txt', open(os.path.join(raiz, 'docs', 'LEIA-ME-release.txt'), encoding='utf-8').read().replace('{VERSAO}', ver))
    print('%s: %d arquivos, %.0f MB' % (zpath, n + 1, os.path.getsize(zpath) / 1e6))

main()
