r"""Le malhas (.skin) e esqueletos (.ske) do Gun (2005) direto do .pak, com gun_pak.py. Decodificacao parcial; o que consta aqui foi conferido no cavalo e na carroca.

Uso: python gun_model.py skin <pak> <parte do nome> <saida.obj> [--png previa.png]    (ex.: global.pak.xbx horse\horse.skin horse.obj)
     python gun_model.py ske  <pak> <parte do nome>                                    (ossos e posicoes de repouso)
     python gun_model.py ska  <pak> <parte do nome> [saida.json]                       (animacao: chaves por osso; json = {duracao, flags, ossos:[{rot,trans}]})

Os offsets vem de gun_pak.py ja corrigidos (relativos a entrada, ver la): cada arquivo comeca exatamente no offset listado.
.skin: o bloco comeca com dados de pre-cabecalho (0x300 a 0x6a0 bytes, nao decodificados) e so depois vem o cabecalho:
  cabecalho de 36 bytes = [0, ?, 0, 0, 0, 0, 0xffffffff, 0, (?<<16)|n_indices], achado pelo par ffffffff 00000000.
  A 44 bytes do inicio do cabecalho fica a posicao do vertice 0; os vertices tem passo de 44 (1 UV) ou 60 bytes (3 UVs):
    pos 3 float | pesos 4 | ossos 4 x u16 | normal 4 | tangente 4 | cor 4 | UVs (2 float cada).
  Depois dos V vertices vem n_indices u16 em tira de triangulos, com degenerados (a a b c d d) unindo as tiras. V = maior indice + 1.
  Apos 0xfacecaa2 vem a tabela de materiais (hash de textura, formato Xbox). Ligacao material -> faixa de indices: NAO decodificada.
  Ainda nao testado fora do cavalo e da carroca (peds e skins sem pesos podem divergir).
.ske: o cabecalho fica no inicio do arquivo.
  Cabecalho de 48 bytes [0x00300001, n, 0, 0, 6 ponteiros, ptr das matrizes, 0x30], ponteiros relativos ao cabecalho. Depois: n posicoes locais
  (x,y,z,1) em hdr+0x30, n matrizes 4x4 de 64 bytes e 6 vetores de n u32: hash do nome, hash do pai, hash do osso espelho, indice do espelho
  (0xffffffff = nenhum), 0, flag. O pai vem de procurar o hash do pai entre os hashes dos nomes (raiz = ela mesma).
.ska (decodificado lendo o amostrador do Gun.exe: 0x497090, chaves de rotacao 0x4952b0, de translacao 0x495500; conferido nas 1904 animacoes do global.pak):
  u32: [0]=0x28 (tamanho do cabecalho), [1]=flags F, [2]=duracao (float, s), byte 0xd = n_ossos, [5..9]=ponteiros (0xffffffff no arquivo),
  [10]=soma dos bytes dos fluxos de rotacao, [11]=idem de translacao. Em 0x30: n u16 com os BYTES do fluxo de rotacao de cada osso, depois n u16
  para a translacao; se F&0x80000 vem uma mascara de ossos (4 + 4*ceil(n/32) bytes, ignorada); depois os fluxos de rotacao de todos os ossos
  e os de translacao, em ordem de osso. Tempos em 1/60 s. Osso sem chave de rotacao = identidade; sem translacao = fica a pose do esqueleto.
  Chave de rotacao: u16 hdr [bit15 = w negativo, bit14 = comprimida, bits13-11 = componente em 1 byte, bits0-10 = tempo] (com F&0x100 vem antes
  um u16 de tempo de 15 bits com sinal); sem bit14: 3 componentes s16 (F&0x8000: 3 bytes << 8); bit14 e bits13-11 = 0: 1 byte = indice em
  standardkeyQ.bin (x,y,z); senao cada componente e 1 byte (F&0x10000: indice, valor = 4o int16 do no) ou u16/byte<<8. Quaternio = s16/16384,
  w = sqrt(1-x2-y2-z2). Chave de translacao: byte f [0x40: tempo = f&0x3f, senao u16 de tempo depois; 0x80: 1 byte = indice em standardkeyT.bin,
  senao 3 x s16], valor = s16/32; com F&0x20000 a translacao e aditiva sobre a posicao do osso no esqueleto.
  Codebooks em data\anims\standardkeyQ.bin / T.bin (256 nos de 4 x int16).
"""
import math, os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gun_pak

MARCA = b'\xff\xff\xff\xff\x00\x00\x00\x00'

def acha(pak, trecho, tipo):
    data = gun_pak.raiz(pak)
    es, dados = gun_pak.le_pak(pak)
    r = [(o, s, n) for t, o, s, h, n in gun_pak.filtra(es, tipo, trecho, gun_pak.nomes(data))]
    r = [x for x in r if x[2].lower().endswith(trecho.lower())] or r
    if len(r) != 1: sys.exit('%d entradas casam com %r: %s' % (len(r), trecho, [x[2] for x in r][:8]))
    return dados, r[0]

def passo(d, p0):
    """Passo do vertice: o que mais repete bytes (cor 0x80808080, pesos)."""
    return max(range(28, 100, 4), key=lambda s: sum(d[p0 + i] == d[p0 + i + s] for i in range(3000)))

def le_skin(dados, off, tam):
    """Testa cada marca do bloco e fica com a malha de mais indices: nos skins de cutscene o pre-cabecalho traz marcas de tabelas pequenas
    (64 indices) que tambem fecham a conta de vertices."""
    k, best = off, None
    while True:                       # o bloco comeca com dados de pre-cabecalho que tambem podem conter a marca: vale a que fecha a conta
        k = dados.find(MARCA, k + 1, off + tam)
        if k < 0: break
        hdr = k - 24
        n = struct.unpack_from('<I', dados, hdr + 32)[0] & 0xffff
        p0, s = hdr + 44, passo(dados, hdr + 44)
        try:
            for V in range(2, 6000):  # V = o valor em que n indices logo apos os vertices tem maximo V-1
                idx = struct.unpack_from('<%dH' % n, dados, p0 + V * s)
                if n and max(idx) == V - 1: break
            else: continue
        except struct.error: continue
        if not best or n > best[0]: best = (n, V, p0, s, idx)
    if not best: sys.exit('cabecalho de .skin nao achado')
    n, V, p0, s, idx = best
    vs = []
    for i in range(V):
        o = p0 + i * s
        w0, w1 = struct.unpack_from('<2H', dados, o + 12)
        vs.append((struct.unpack_from('<3f', dados, o), struct.unpack_from('<2f', dados, o + 36),
                   (w0 / 32767, w1 / 32767), struct.unpack_from('<4H', dados, o + 16)))
    return vs, list(idx), s

def tiras(idx):
    for i in range(len(idx) - 2):
        a, b, c = idx[i:i + 3]
        if a != b != c != a: yield (a, c, b) if i & 1 else (a, b, c)

def le_ske(dados, off, nomes):
    """Ossos [(nome, pai, espelho, pos local)]; o cabecalho fica no inicio do bloco."""
    a = off
    w = struct.unpack_from('<12I', dados, a)
    if w[0] != 0x300001 or w[11] != 0x30: sys.exit('cabecalho de .ske invalido')
    n = w[1]
    hs, pais, _, esp = (struct.unpack_from('<%dI' % n, dados, a + w[4 + k]) for k in range(4))
    return [(nomes.get(hs[i]) or '%08x' % hs[i], hs.index(pais[i]) if pais[i] in hs else -1, esp[i] if esp[i] < n else -1,
             struct.unpack_from('<3f', dados, a + 0x30 + 16 * i)) for i in range(n)]

def codebook(data, nome):
    """standardkeyQ/T.bin: 256 nos de 4 x int16."""
    return struct.unpack('<1024h', open(os.path.join(data, 'anims', nome), 'rb').read())

def chave_rot(b, p, F, Q):
    """Uma chave de rotacao (decodificada como FUN_004952b0 do Gun.exe): devolve ((tempo, x, y, z, w), novo p). tempo em 1/60 s."""
    u16 = lambda q: struct.unpack_from('<H', b, q)[0]
    if F & 0x100: ext, hdr, q = u16(p), u16(p + 2), p + 4
    else: hdr = u16(p); ext, q = hdr, p + 2
    c = [0, 0, 0]
    if not hdr & 0x4000:
        if F & 0x8000: c = [b[q] << 8, b[q + 1] << 8, b[q + 2] << 8]; q += 3
        else: c = [u16(q), u16(q + 2), u16(q + 4)]; q += 6
    elif not hdr & 0x3800:
        c = [Q[b[q] * 4 + k] for k in range(3)]; q += 1
    else:
        for k, flag in enumerate((0x2000, 0x1000, 0x800)):
            if hdr & flag: c[k] = Q[b[q] * 4 + 3] if F & 0x10000 else b[q]; q += 1
            elif F & 0x8000: c[k] = b[q] << 8; q += 1
            else: c[k] = u16(q); q += 2
    c = [((x & 0xffff) ^ 0x8000) - 0x8000 for x in c]                 # int16
    c = [x / 16384 for x in c]
    t = (ext & 0x7fff) if F & 0x100 else hdr & 0x7ff                   # tempo estendido de 15 bits (com sinal) ou de 11 bits
    if F & 0x100 and t & 0x4000: t -= 0x8000
    w = math.sqrt(max(0.0, 1 - sum(x * x for x in c))) if any(c) else 1.0
    return (t, *c, -w if hdr & 0x8000 else w), q

def chave_trans(b, p, T):
    """Uma chave de translacao (FUN_00495500): ((tempo, x, y, z), novo p); valores s16 / 32."""
    f = b[p]; q = p + 1
    if f & 0x40: t = f & 0x3f
    else: t = struct.unpack_from('<h', b, q)[0]; q += 2
    if f & 0x80: v = T[b[q] * 4:b[q] * 4 + 3]; q += 1
    else: v = struct.unpack_from('<3h', b, q); q += 6
    return (t, *(x / 32 for x in v)), q

def le_ska(dados, off, tam, Q, T):
    """Animacao: {'duracao', 'flags', 'ossos': [{'rot': [(t,x,y,z,w)], 'trans': [(t,x,y,z)]}]}; t em 1/60 s."""
    b = dados[off:off + tam]
    if struct.unpack_from('<I', b, 0)[0] != 0x28: sys.exit('cabecalho de .ska invalido')
    F, dur = struct.unpack_from('<If', b, 4)
    n = b[0xd]
    A = struct.unpack_from('<%dH' % n, b, 0x30)
    B = struct.unpack_from('<%dH' % n, b, 0x30 + 2 * n)
    p = 0x30 + 4 * n + (4 + 4 * ((n + 31) // 32) if F & 0x80000 else 0)   # 0x80000: mascara de ossos [n, palavras...] antes dos fluxos
    ossos = [{'rot': [], 'trans': []} for _ in range(n)]
    for i in range(n):
        fim = p + A[i]
        while p < fim: k, p = chave_rot(b, p, F, Q); ossos[i]['rot'].append(k)
        if p != fim: sys.exit('fluxo de rotacao do osso %d nao fecha' % i)
    for i in range(n):
        fim = p + B[i]
        while p < fim: k, p = chave_trans(b, p, T); ossos[i]['trans'].append(k)
        if p != fim: sys.exit('fluxo de translacao do osso %d nao fecha' % i)
    return {'duracao': dur, 'flags': F, 'ossos': ossos}

def png(path, W, H, px):
    raw = b''.join(b'\0' + bytes(px[y * W:(y + 1) * W]) for y in range(H))
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 0, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))

def previa(path, P, tris, W=600):
    """Arame lateral (eixos y,z) da malha."""
    px = bytearray([255]) * (W * W)
    zs, ys = [p[2] for p in P], [p[1] for p in P]
    k = (W - 60) / max(max(zs) - min(zs), max(ys) - min(ys))
    T = lambda p: (30 + (p[2] - min(zs)) * k, W - 30 - (p[1] - min(ys)) * k)
    for t in tris:
        for a, c in ((0, 1), (1, 2), (2, 0)):
            (x0, y0), (x1, y1) = T(P[t[a]]), T(P[t[c]])
            n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
            for j in range(n + 1): px[int(y0 + (y1 - y0) * j / n) * W + int(x0 + (x1 - x0) * j / n)] = 0
    png(path, W, W, px)

def main():
    a = sys.argv[1:]
    if len(a) < 3 or a[0] not in ('skin', 'ske', 'ska'): sys.exit(__doc__)
    dados, (o, s, nome) = acha(a[1], a[2], '.' + a[0])
    if a[0] == 'ska':
        data = gun_pak.raiz(a[1])
        an = le_ska(dados, o, s, codebook(data, 'standardkeyQ.bin'), codebook(data, 'standardkeyT.bin'))
        print('%s: %.3f s, flags %08x, %d ossos, %d chaves de rotacao, %d de translacao' % (
            nome, an['duracao'], an['flags'], len(an['ossos']), sum(len(x['rot']) for x in an['ossos']), sum(len(x['trans']) for x in an['ossos'])))
        for i, x in enumerate(an['ossos']):
            if x['rot'] or x['trans']: print('  osso %2d: %3d rot (t %s..%s), %3d trans' % (i, len(x['rot']), x['rot'][0][0] if x['rot'] else '-', x['rot'][-1][0] if x['rot'] else '-', len(x['trans'])))
        if len(a) > 3:
            import json
            json.dump(an, open(a[3], 'w'))
        return
    if a[0] == 'ske':
        for i, (nome_, pai, esp, p) in enumerate(le_ske(dados, o, gun_pak.nomes(gun_pak.raiz(a[1])))):
            print('%2d %-22s pai %2d espelho %3d  %8.2f %8.2f %8.2f' % (i, nome_, pai, esp, *p))
        return
    vs, idx, passo_ = le_skin(dados, o, s)
    tris = list(tiras(idx))
    print('%s: %d vertices (passo %d), %d indices, %d triangulos' % (nome, len(vs), passo_, len(idx), len(tris)))
    with open(a[3], 'w') as f:
        for p, *_ in vs: f.write('v %f %f %f\n' % p)
        for _, uv, *_ in vs: f.write('vt %f %f\n' % (uv[0], 1 - uv[1]))
        for t in tris: f.write('f %d/%d %d/%d %d/%d\n' % tuple(x + 1 for x in (t[0], t[0], t[1], t[1], t[2], t[2])))
    if '--png' in a: previa(a[a.index('--png') + 1], [v[0] for v in vs], tris)

if __name__ == '__main__':
    main()
