r"""Gera cavalo.dff/.txd do SA a partir da malha do cavalo do Gun (corpo, sela e crina) com o esqueleto do Gun nos IDs de osso do ped do SA.

Uso: python gun_horse_dff.py <global.pak.xbx do Gun> <cavalo.dff original> <saida cavalo.dff> <saida cavalo.txd> [pelagem [crina]]
pelagem = horse (cinza), horse_01 (appaloosa), horse_02 (baio), horse_03 (pinto); crina = horse_mane ou horse_mane_pinto

O esqueleto do SA (frames) copia as rotacoes de repouso e as posicoes do .ske (x0,0254 m por unidade): as chaves do .ska passam a valer sem
retarget (ver gun_to_ifp.py). O Gun tem 35 ossos e o ped do SA 32 ids: as orelhas e o topete (9, 10, 11) viram a cabeca.
"""
import math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_ifp as G
import gun_model, gun_pak, gun_tex, gun_to_ifp as X

K, GROUND, SA_ID, MERGE, NAMES = X.K, X.GROUND, X.SA_ID, X.MERGE, G.BONE_NAMES
VER = 0x1803ffff

# ---------------------------------------------------------------- escrita RenderWare
def chunk(t, payload, ver=VER): return struct.pack('<III', t, len(payload), ver) + payload
def struct_(payload): return chunk(0x01, payload)
def ext(*chunks): return chunk(0x03, b''.join(chunks))
def string(s):
    b = s.encode('latin1') + b'\0'; b += b'\0' * (-len(b) % 4); return chunk(0x02, b)

def hanim_flags(order, parent):
    """flags HAnim em ordem DFS: 2 = empilha o pai (ha irmao depois), 1 = desempilha (folha e o proximo nao e filho)"""
    out = []
    for i, n in enumerate(order):
        later_sibling = any(parent[m] == parent[n] for m in order[i + 1:])
        nxt = order[i + 1] if i + 1 < len(order) else None
        f = (2 if later_sibling else 0) | (1 if nxt is not None and parent[nxt] != n else 0)
        out.append(f)
    return out

def dfs(par):
    kids = {}
    for i, p in enumerate(par):
        if p >= 0 and p != i: kids.setdefault(p, []).append(i)
    order = []
    def go(i):
        order.append(i)
        for c in kids.get(i, []): go(c)
    go(0)
    return order

def mat_row(R, t):
    """matriz de 12 floats do frame RW (3 linhas + posicao) a partir de rotacao coluna R"""
    return struct.pack('<12f', *[R[c][r] for r in range(3) for c in range(3)], *t)    # linhas = colunas de R (convencao vetor-linha)

def darken(d, f):
    """textura DXT mais escura sem recodificar: escala as duas cores de cada bloco (DXT1: 8 bytes; DXT5: cores nos 8 ultimos de 16)"""
    def c565(v): return [((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31]
    def p565(c): return ((c[0] * 31 // 255) << 11) | ((c[1] * 63 // 255) << 5) | (c[2] * 31 // 255)
    bs = 8 if d['kind'] == 1 else 16; o = bs - 8
    out = dict(d); out['levels'] = []
    for lv in d['levels']:
        b = bytearray(lv)
        for i in range(0, len(b) - 7, bs):
            c0, c1 = struct.unpack_from('<HH', b, i + o)
            n0, n1 = p565([int(x * f) for x in c565(c0)]), p565([int(x * f) for x in c565(c1)])
            if d['kind'] == 1 and (c0 > c1) != (n0 > n1):      # DXT1: a ordem das cores escolhe o modo (4 cores ou 3 + transparente)
                if c0 > c1: n0 = max(n0, n1 + 1) if n1 < 0xffff else n0
                else: n1 = max(n1, n0 + 1) if n0 < 0xffff else n1
            struct.pack_into('<HH', b, i + o, n0, n1)
        out['levels'].append(bytes(b))
    return out

# ---------------------------------------------------------------- malha
def load_skin(pak, name):
    dados, (o, s, nome) = gun_model.acha(pak, name, '.skin')
    vs, idx, _ = gun_model.le_skin(dados, o, s)
    return vs, list(gun_model.tiras(idx))

def build(pak, orig_dff, out_dff, out_txd, coat='horse', mane='horse_mane', reed=None):
    """reed = pak da cutscene JS_Mid1: troca as partes pelo corpo do cavalo do Reed (horse_reed, atlas 256 unico: crina/cauda/cinto usam a textura errada)"""
    gun = X.GunRig(pak)
    order = dfs(gun.par)
    keep = [i for i in order if i in SA_ID]
    skel_par = {}
    for i in keep:
        p = gun.par[i]
        while p in MERGE: p = gun.par[p]
        skel_par[i] = p
    # frames: 0 = clump, 1.. = ossos em ordem DFS
    frame_of = {g: k + 1 for k, g in enumerate(keep)}
    par = [skel_par[g] if g != 0 else -1 for g in keep]
    flags = hanim_flags(keep, skel_par)
    # cinematica de repouso no espaco do SA (coluna): raiz = C x L0, posicao do raiz sobe ate a altura do ped
    org = X.origin(gun)
    W, P = {}, {}
    for g in keep:
        loc = [c * K for c in gun.loc[g]]
        if g == 0: W[g] = X.mm(X.C, gun.L[0]); P[g] = list(org)
        else:
            p = skel_par[g]; W[g] = X.mm(W[p], gun.L[g]); off = [sum(W[p][r][c] * loc[c] for c in range(3)) for r in range(3)]
            P[g] = [a + b for a, b in zip(P[p], off)]
    frames = [struct.pack('<12f', 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0) + struct.pack('<ii', -1, 0x20003)]
    frame_ext = [ext()]
    for k, g in enumerate(keep):
        if g == 0: Rl, t = X.mm(X.C, gun.L[0]), list(org)
        else: Rl, t = gun.L[g], [c * K for c in gun.loc[g]]
        frames.append(mat_row(Rl, t) + struct.pack('<ii', 0 if g == 0 else frame_of[skel_par[g]], 0x3))
        nodes = b''.join(struct.pack('<iii', SA_ID[x], j, flags[j]) for j, x in enumerate(keep))
        hanim = (struct.pack('<III', 0x100, SA_ID[g], len(keep) if g == 0 else 0) + (struct.pack('<II', 0, 36) + nodes if g == 0 else b''))
        frame_ext.append(ext(chunk(0x11E, hanim), chunk(0x253F2FE, NAMES[SA_ID[g]].encode().ljust((len(NAMES[SA_ID[g]]) + 4) // 4 * 4, b'\0'))))
    framelist = chunk(0x0E, struct_(struct.pack('<I', len(frames)) + b''.join(frames)) + b''.join(frame_ext))

    # geometrias do Gun juntas em uma so, uma material por skin
    # (skin, bloco de textura, indice da difusa no bloco: no corpo a ordem e normal, difusa, especular)
    parts = [('horse\\horse.skin', coat + '.tex', 1, 'horse'), ('horse_saddle.skin', 'horse_saddle.tex', 0, 'horse_saddle'),
             (mane + '.skin', mane + '.tex', 0, 'horse_mane')]     # na crina a ordem e difusa (DXT5 com alfa), normal, especular
    paks = [pak] * len(parts)
    if reed:    # corpo do Reed (so o corpo vem na cutscene) + sela e crina/cauda do cavalo comum, escurecidas
        parts = [('horse_reed.skin', 'horse_reed.tex', 2, 'horse')] + parts[1:]
        paks[0] = reed
    pts, uvs, wts, bidx, tris, tnames = [], [], [], [], [], []
    for mi, (skin, tex, tix, tname) in enumerate(parts):
        vs, tr = load_skin(paks[mi], skin)
        base = len(pts)
        for p, uv, (w0, w1), bs in vs:
            b3 = [MERGE.get(x // 3, x // 3) for x in bs[:3]]
            ws = [w0, w1, max(0.0, 1 - w0 - w1)]
            acc = {}
            for b, w in zip(b3, ws): acc[b] = acc.get(b, 0) + w
            lst = sorted(acc.items(), key=lambda kv: -kv[1])[:4]
            tot = sum(w for _, w in lst) or 1
            idx = [frame_of[b] - 1 if False else keep.index(b) for b, _ in lst] + [0] * (4 - len(lst))
            wl = [w / tot for _, w in lst] + [0.0] * (4 - len(lst))
            v = [-p[0] * K + org[0], p[2] * K + org[1], p[1] * K + org[2]]   # Gun (x esquerda, y cima, z frente) -> SA (x direita, y frente, z cima)
            pts.append(v); uvs.append(uv); bidx.append(idx); wts.append(wl)
        tris += [(a + base, b + base, c + base, mi) for a, b, c in tr]
        tnames.append(tname)
    nv = len(pts)
    normals = [[0.0, 0.0, 0.0] for _ in range(nv)]
    for a, b, c, _ in tris:
        u = [pts[b][i] - pts[a][i] for i in range(3)]; w = [pts[c][i] - pts[a][i] for i in range(3)]
        n = G.cross(u, w)
        for v in (a, b, c): normals[v] = [x + y for x, y in zip(normals[v], n)]
    normals = [G.norm(n) if any(n) else [0, 0, 1] for n in normals]
    # a ordem dos triangulos do Gun pode estar invertida em relacao ao RW: confere com a normal media apontando para fora do centro
    cen = [sum(p[i] for p in pts) / nv for i in range(3)]
    flip = sum(sum((p[i] - cen[i]) * n[i] for i in range(3)) for p, n in zip(pts, normals)) < 0
    if flip: tris = [(a, c, b, m) for a, b, c, m in tris]; normals = [[-x for x in n] for n in normals]
    # crina e cauda sao laminas de uma face so (o jogo descarta a face de costas e a cauda some conforme o angulo): duplica com a ordem invertida
    tris += [(a, c, b, m) for a, b, c, m in tris if parts[m][3] == 'horse_mane']
    # skin: matrizes inversas = inverso da pose de repouso de cada osso (vetor-linha: v_osso = (v - t) x Wc)
    mats = b''
    for g in keep:
        t = P[g]; Wc = W[g]
        tt = [-sum(t[r] * Wc[r][c] for r in range(3)) for c in range(3)]
        mats += struct.pack('<16f', *Wc[0], 0, *Wc[1], 0, *Wc[2], 0, *tt, 1)
    used = sorted({i for b in bidx for i, w in zip(b, [1] * 4)} | {0})
    used = list(range(len(keep)))
    skin = chunk(0x116, struct.pack('<4B', len(keep), len(used), 4, 0) + bytes(used) + b''.join(bytes(b) for b in bidx) +
                 b''.join(struct.pack('<4f', *w) for w in wts) + mats + b'\0' * 12)
    # geometria
    flags_g = 0x10076 | 0x0                                                    # posicoes, textura, normais, luz, cor do material; 1 UV
    body = struct.pack('<IIII', flags_g, len(tris), nv, 1)
    body += b''.join(struct.pack('<2f', *uv) for uv in uvs)
    body += b''.join(struct.pack('<HHHH', b, a, m, c) for a, b, c, m in tris)
    cen_r = max(math.dist(p, cen) for p in pts)
    body += struct.pack('<4f', *cen, cen_r) + struct.pack('<II', 1, 1) + b''.join(struct.pack('<3f', *p) for p in pts) + b''.join(struct.pack('<3f', *n) for n in normals)
    mats_l = struct_(struct.pack('<I', len(tnames)) + struct.pack('<i', -1) * len(tnames))
    for tn in tnames:
        mstruct = struct.pack('<IIIIfff', 0, 0xffffffff, 0, 1, 1.0, 0.0, 1.0)
        tex = chunk(0x06, struct_(bytes([6, 1, 1, 0])) + string(tn) + string('') + ext())
        mats_l += chunk(0x07, struct_(mstruct) + tex + ext())
    binmesh = struct.pack('<III', 0, len(tnames), len(tris) * 3)
    for mi in range(len(tnames)):
        ids = [v for a, b, c, m in tris if m == mi for v in (a, b, c)]
        binmesh += struct.pack('<II', len(ids), mi) + struct.pack('<%dI' % len(ids), *ids)
    geom = chunk(0x0F, struct_(body) + chunk(0x08, mats_l) + ext(chunk(0x50E, binmesh), skin))
    geoms = chunk(0x1A, struct_(struct.pack('<I', 1)) + geom)
    # atomico: o conteudo da extensao (right to render) vem do .dff original
    ob = open(orig_dff, 'rb').read(); ap, asz = G.find(ob, 0, len(ob), 0x14)
    aext = next(ob[p - 12:p + s] for t, s, v, p in G.chunks(ob, ap, ap + asz) if t == 0x03)
    atomic = chunk(0x14, struct_(struct.pack('<IIII', 0, 0, 5, 0)) + aext)
    clump = chunk(0x10, struct_(struct.pack('<III', 1, 0, 0)) + framelist + geoms + atomic + ext())
    open(out_dff, 'wb').write(clump)
    texs = [(tn, gun_tex.le_tex(pk, tex)[tix]) for pk, (_, tex, tix, tn) in zip(paks, parts)]
    if reed: texs = [(tn, darken(d, 0.3) if tn != 'horse' else d) for tn, d in texs]
    open(out_txd, 'wb').write(gun_tex.txd(texs))
    return pts, parts, keep, tris, normals

if __name__ == '__main__':
    pak, orig, out_dff, out_txd = sys.argv[1:5]
    reed = sys.argv[sys.argv.index('--reed') + 1] if '--reed' in sys.argv else None
    rest = [a for a in sys.argv[5:] if a != '--reed' and a != reed]
    coat, mane = (rest + ['horse_mane'])[:2] if rest else ('horse', 'horse_mane')
    pts, parts, keep, _, _ = build(pak, orig, out_dff, out_txd, coat, mane, reed)
    print('cavalo.dff: %d vertices, %d ossos -> %s (%d bytes)' % (len(pts), len(keep), out_dff, os.path.getsize(out_dff)))
