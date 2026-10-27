r"""Lista e extrai os pacotes .pak.xbx do Gun (2005, Neversoft). Nao muda nada no jogo.

Uso: python gun_pak.py list    <pak> [--type .ska] [--grep texto]
     python gun_pak.py extract <pak> <pasta saida> [--type .ska] [--grep texto]
     python gun_pak.py scan    <data do Gun> [--type .ska] [--grep texto]    (lista todos os pacotes, uma linha por pacote)

O .pak e uma tabela de entradas de 32 bytes (tipo, offset, tamanho, hash do nome, 16 bytes) ate o tipo 0xb524565f; se o bit 0x20 do
8o dword estiver ligado a entrada tem 0xc0 bytes (nome em texto a partir de +0x20). O offset e RELATIVO A POSICAO DA PROPRIA ENTRADA no
.pak: posicao real = posicao da entrada + offset, menos o tamanho do .pak quando os dados ficam no .pab irmao (confirmado no carregador do
Gun.exe, 0x410b50, e pelas 1904 animacoes comecando com o mesmo cabecalho). Tipo e nome sao ~CRC32 do texto em minusculas.
Os nomes vem de <data>\pakhi\dbg.pak.xbx e dos pakhi\**\*.txt.xbx ("0xHASH caminho"); o que nao tiver nome sai como <hash>.<tipo>.
"""
import os, re, struct, sys, zlib

TIPOS = {e: (~zlib.crc32(e.encode())) & 0xffffffff for e in
         ('.ska', '.ske', '.skin', '.geom', '.col', '.qb', '.tex', '.img', '.mdl', '.scn', '.cam', '.fam')}
NOME_DO_TIPO = {h: e for e, h in TIPOS.items()}

def raiz(p):
    p = os.path.abspath(p)
    while p and os.path.basename(p).lower() != 'data':
        pai = os.path.dirname(p)
        if pai == p: return None
        p = pai
    return p

def nomes(data):
    """hash -> caminho original, lido dos .txt.xbx de pakhi."""
    t = {int(h, 16): n.decode() for h, n in re.findall(rb'0x([0-9a-f]{8}) ([ -~]+)', open(os.path.join(data, 'pakhi', 'dbg.pak.xbx'), 'rb').read())}
    for d, _, fs in os.walk(os.path.join(data, 'pakhi')):
        for f in fs:
            if f.lower().endswith('.txt.xbx'):
                for h, n in re.findall(r'^0x([0-9a-f]{8}) (.+?)\s*$', open(os.path.join(d, f), 'r', errors='replace').read(), re.M):
                    t[int(h, 16)] = n
    return t

def le_pak(pak):
    """Devolve (entradas [(tipo, off, tam, hash)], bytes dos dados). Descarta entradas zeradas ou que saem do arquivo de dados."""
    b = open(pak, 'rb').read()
    pab = pak[:-len('.pak.xbx')] + '.pab.xbx'
    inline = not os.path.exists(pab)
    dados = b if inline else open(pab, 'rb').read()
    es, i = [], 0
    while i + 32 <= len(b):
        t, o, s, h, *_, fl = struct.unpack_from('<8I', b, i)
        if t == 0xb524565f: break
        real = i + o - (0 if inline else len(b))
        if 0 <= real and real + s <= len(dados): es.append((t, real, s, h))
        i += 0xc0 if fl & 0x20 else 0x20
    return es, dados

def filtra(es, tipo, grep, tab):
    for t, o, s, h in es:
        ext = NOME_DO_TIPO.get(t, '.%08x' % t)
        nome = tab.get(h) or '%08x%s' % (h, ext)
        if tipo and ext != tipo: continue
        if grep and grep.lower() not in nome.lower(): continue
        yield t, o, s, h, nome

def main():
    a = [x for x in sys.argv[1:]]
    opt = lambda k: a[a.index(k) + 1] if k in a else None
    tipo, grep = opt('--type'), opt('--grep')
    cmd, alvo = a[0], a[1]
    data = raiz(alvo)
    if not data: sys.exit('nao achei a pasta data acima de ' + alvo)
    tab = nomes(data)
    if cmd == 'scan':
        for d, _, fs in os.walk(data):
            for f in sorted(fs):
                if f.lower().endswith('.pak.xbx'):
                    es, _ = le_pak(os.path.join(d, f))
                    n = list(filtra(es, tipo, grep, tab))
                    if n: print('%5d  %s' % (len(n), os.path.relpath(os.path.join(d, f), data)))
        return
    es, dados = le_pak(alvo)
    if cmd == 'list':
        for t, o, s, h, nome in filtra(es, tipo, grep, tab): print('%08x %9d  %s' % (o, s, nome))
    elif cmd == 'extract':
        for t, o, s, h, nome in filtra(es, tipo, grep, tab):
            dst = os.path.join(a[2], os.path.basename(nome.replace('\\', '/')) if h in tab else nome)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(dados[o:o + s])
    else:
        sys.exit(__doc__)

if __name__ == '__main__':
    main()
