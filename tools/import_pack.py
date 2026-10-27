r"""Importa os modelos dos packs externos (Pack de animais, dogs-pack, cat-packs) como pastas AnimNN em cleo\Animals.

Uso: python import_pack.py <pasta com os packs> <cleo\Animals> [--force] [--tris N]

Cada animal vira uma pasta AnimNN com o .ini copiado de uma pasta parecida e uma variante A<NN>_<k> por raca/cor. Os
dogs-pack/cat-packs vem de <packs>\packanimais-texturas-menores (atlas de textura, ja com UV remapeada). A pasta e anotada em
<cleo\Animals>\FaunaSA\imported.txt, que o gen_ifp.py le para gerar a animacao. Nao mexe em pastas existentes (so com --force).
--tris N reduz cada malha distinta para N triangulos (tools/decimate_dff.py, ~2 min por malha, em paralelo).
Depois: install_models.ps1 e install_anims.ps1.
"""
import concurrent.futures, hashlib, os, re, shutil, subprocess, sys

PA = 'Pack de animais/gta3img/'
OT = 'packanimais-texturas-menores/'
def ot(n): return OT + '%s/%s_optimized' % (n, n)

# (pasta, ini-modelo, nome, [modelos sem extensao, relativos a <packs>])
SPECS = [
    ('Anim90', 'Anim42', 'leao',          [PA + 'bmybu']),
    ('Anim91', 'Anim42', 'tigre',         [PA + 'swat']),
    # Anim92 (porco, Pack bmybe) foi retirado a pedido
    ('Anim93', 'Anim14', 'flamingo',      [PA + 'wmyva']),
    ('Anim94', 'Anim68', 'caes grandes',  [ot(n) for n in ('hmybe', 'hmyst', 'hmycr', 'sbfyst', 'hmyri', 'omokung', 'bmyboun', 'bmori', 'wmymech', 'wmyplt')]),
    ('Anim95', 'Anim73', 'gatos',         [ot(n) for n in ('wmybp', 'bmyap', 'bmybe', 'male01', 'bmycr', 'bmyst')]),  # branco, laranja, malhado, malhado-laranja, preto, siames
    ('Anim96', 'Anim68', 'cao marrom',    [ot(n) for n in ('dwmolc1', 'dwmolc2', 'dwmylc1')]),
    # Anim97 (bulldogs: bmydj, wmybmx) e Anim101 (lagarto: Pack bmori) ficaram de fora: o gerador de animacao erra a pose deles (ave / 2 patas, velocidade 8 m/s)
    # Anim98 (rottweiler, Pack WMYVA2) tambem ficou de fora: as patas de tras nao tem pesos que o gerador consiga separar (perna solta no jogo)
    ('Anim99', 'Anim52', 'lobo',          [PA + 'bmycr']),
    ('Anim100', 'Anim57', 'urso preto',   [PA + 'hmybe']),
]
NO_GEN = set()  # rotulos que nao passam pelo gerador de animacao (usam um grupo existente do FaunaSA.ini)

def md5(p): return hashlib.md5(open(p, 'rb').read()).hexdigest()

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    tris = int(sys.argv[sys.argv.index('--tris') + 1]) if '--tris' in sys.argv else 0
    args = [a for a in args if not (tris and a == str(tris))]
    packs, dst = args[0], args[1]
    force = '--force' in sys.argv
    here = os.path.dirname(os.path.abspath(__file__))
    todo = [s for s in SPECS if force or not os.path.exists(os.path.join(dst, s[0]))]
    for s in SPECS:
        if s not in todo: print('%-8s ja existe, pulado' % s[0])
    # reduz uma vez cada .dff distinto (varias racas/cores dividem a malha), em paralelo
    cache = {}
    if tris:
        src = {}
        for _, _, _, models in todo:
            for m in models: src.setdefault(md5(os.path.join(packs, m + '.dff')), os.path.join(packs, m + '.dff'))
        tmp = os.path.join(dst, 'FaunaSA', '_reduzidos')
        os.makedirs(tmp, exist_ok=True)
        def run(item):
            h, f = item
            out = os.path.join(tmp, h + '.dff')
            if os.path.getsize(f) < 1000000: return h, f  # malha pequena (< ~10 mil triangulos): fica como esta
            if not os.path.exists(out):
                subprocess.run([sys.executable, os.path.join(here, 'decimate_dff.py'), f, out, '--tris', str(tris)], check=True, stdout=subprocess.DEVNULL)
            return h, out
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(4, os.cpu_count() or 2)) as ex:
            cache = dict(ex.map(run, src.items()))
        print('%d malhas distintas reduzidas para %d triangulos' % (len(cache), tris))
    done = []
    for folder, tmpl, label, models in todo:
        out = os.path.join(dst, folder)
        tini = os.path.join(dst, tmpl, tmpl + '.ini')
        if not os.path.exists(tini): print('%-8s sem o ini-modelo %s, pulado' % (folder, tmpl)); continue
        os.makedirs(out, exist_ok=True)
        n = folder[4:]
        ini = open(tini, encoding='latin1').read()
        if label not in NO_GEN:  # o ini-modelo aponta para o grupo do animal original (lion, dog, cat...), que nao serve nesta malha
            ini = re.sub(r'(?im)^custom animgroup=.*$', 'custom animgroup=Fauna' + folder, ini)
        open(os.path.join(out, folder + '.ini'), 'w', encoding='latin1', newline='').write(ini)
        for k, m in enumerate(models, 1):
            d, t = os.path.join(packs, m + '.dff'), os.path.join(packs, m + '.txd')
            shutil.copy(cache[md5(d)] if tris else d, os.path.join(out, 'A%s_%d.dff' % (n, k)))
            shutil.copy(t, os.path.join(out, 'A%s_%d.txd' % (n, k)))
        if len(models) == 1:  # um so modelo: o ASI usa <pasta>.dff
            for ext in ('.dff', '.txd'): os.replace(os.path.join(out, 'A%s_1%s' % (n, ext)), os.path.join(out, folder + ext))
        else:  # o ASI sorteia entre A<n>_<k>; <pasta>.dff fica como no pacote original (mesmo conteudo da 1a variante)
            for ext in ('.dff', '.txd'): shutil.copy(os.path.join(out, 'A%s_1%s' % (n, ext)), os.path.join(out, folder + ext))
        if label not in NO_GEN: done.append(folder)
        print('%-8s %s: %d modelo(s), ini de %s' % (folder, label, len(models), tmpl))
    if done:
        os.makedirs(os.path.join(dst, 'FaunaSA'), exist_ok=True)
        lst = os.path.join(dst, 'FaunaSA', 'imported.txt')
        old = open(lst).read().split() if os.path.exists(lst) else []
        open(lst, 'w').write('\n'.join(old + [f for f in done if f not in old]) + '\n')

if __name__ == '__main__':
    main()
