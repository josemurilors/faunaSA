r"""Reduz os triangulos de um .dff de ped com skin (GTA SA), preservando UV, normais e pesos de skin dos vertices que ficam.

Uso: python tools/decimate_dff.py <entrada.dff> <saida.dff> [--tris N] [--preview saida.png]

Colapso de arestas por erro quadratico (Garland-Heckbert), sem criar vertices: o vertice removido vai para o vizinho, que
mantem posicao, UV, normal e pesos exatos (nada e interpolado, entao o skin nao quebra). Vertices de borda (costuras de UV, onde
o .dff duplica vertices) nunca sao removidos, para nao abrir fendas. O custo soma uma penalidade por diferenca de pesos de osso
e de UV, para a malha perder detalhe longe das juntas e das costuras de textura.
Suporta o que os modelos de animais usam: 1 geometria, lista de triangulos (sem strip), 1 conjunto de UV, sem cor por vertice.
"""
import heapq, itertools, math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_ifp import chunks, find  # noqa: E402

W_BONE, W_UV = 3e-4, 3e-4  # penalidades, em fracao do (diagonal da malha)^2


class Geo:
    pass


def read_geo(b):
    gp, gs = find(b, 0, len(b), 0x0F)
    g = Geo()
    g.hdr = gp - 12
    st = next(chunks(b, gp, gp + gs))
    q = st[3]
    g.struct = (q, st[1])
    fl, nt, nv, nm = struct.unpack_from('<IIII', b, q)
    nuv = (fl >> 16) & 0xff or (2 if fl & 0x80 else (1 if fl & 4 else 0))
    if fl & 1 or fl & 8 or nuv != 1 or nm != 1:
        raise SystemExit('geometria nao suportada (flags=0x%X, uv=%d, morph=%d): so lista de triangulos, 1 UV, sem prelit' % (fl, nuv, nm))
    q += 16
    g.flags, g.nt, g.nv = fl, nt, nv
    g.uv = [struct.unpack_from('<2f', b, q + i * 8) for i in range(nv)]
    q += 8 * nv
    raw = [struct.unpack_from('<4H', b, q + i * 8) for i in range(nt)]
    q += 8 * nt
    g.sphere = b[q:q + 16]
    hasv, hasn = struct.unpack_from('<II', b, q + 16)
    q += 24
    g.pos = [struct.unpack_from('<3f', b, q + i * 12) for i in range(nv)]
    q += 12 * nv
    g.nrm = [struct.unpack_from('<3f', b, q + i * 12) for i in range(nv)] if hasn else None
    # triangulo no struct: (vertice2, vertice1, material, vertice3); o binmesh (o que o SA desenha) pode ter outra ordem
    g.mat = [r[2] for r in raw]
    sv = [(r[0], r[1], r[3]) for r in raw]
    # binmesh e pele
    g.bin = g.skin = None
    for t, s, v, p in chunks(b, gp, gp + gs):
        if t != 0x03: continue
        for t2, s2, v2, p2 in chunks(b, p, p + s):
            if t2 == 0x50E: g.bin = (p2, s2)
            if t2 == 0x116: g.skin = (p2, s2)
        g.ext = p - 12
    if not g.bin or not g.skin: raise SystemExit('sem binmesh ou skin')
    bp = g.bin[0]
    flg, nmesh, tot = struct.unpack_from('<III', b, bp)
    if flg != 0 or nmesh < 1 or tot != 3 * nt: raise SystemExit('binmesh nao e lista de triangulos')
    bm = []
    o = bp + 12
    for _ in range(nmesh):
        n, mi = struct.unpack_from('<II', b, o)
        o += 8
        idx = struct.unpack_from('<%dI' % n, b, o)
        o += 4 * n
        bm += [(idx[i], idx[i + 1], idx[i + 2], mi) for i in range(0, n, 3)]
    # descobre a permutacao entre a ordem do struct e a do binmesh
    perm = None
    for pm in itertools.permutations(range(3)):
        if all(tuple(sv[i][pm[k]] for k in range(3)) == bm[i][:3] for i in range(min(200, nt))): perm = pm; break
    if perm is None: raise SystemExit('nao consegui casar a ordem dos triangulos do struct com a do binmesh')
    # struct e binmesh podem listar os triangulos em ordem diferente: usa a ordem do struct (cada tri com seu material)
    g.perm = perm
    g.tris = [list(sv[i][perm[k]] for k in range(3)) for i in range(nt)]
    sp, ss = g.skin
    nb, nu, mw, _ = struct.unpack_from('<4B', b, sp)
    g.sk_head = b[sp:sp + 4 + nu]
    o = sp + 4 + nu
    g.sk_idx = [b[o + i * 4:o + i * 4 + 4] for i in range(nv)]
    g.sk_wt = [struct.unpack_from('<4f', b, o + nv * 4 + i * 16) for i in range(nv)]
    g.sk_rest = b[o + nv * 20:sp + ss]
    return g


def decimate(g, target):
    pos, uv = g.pos, g.uv
    xs = [p[0] for p in pos]; ys = [p[1] for p in pos]; zs = [p[2] for p in pos]
    diag2 = (max(xs) - min(xs)) ** 2 + (max(ys) - min(ys)) ** 2 + (max(zs) - min(zs)) ** 2
    nv = g.nv
    tris = g.tris
    alive = [True] * len(tris)
    vt = [set() for _ in range(nv)]
    for t, tr in enumerate(tris):
        for v in tr: vt[v].add(t)
    Q = [[0.0] * 10 for _ in range(nv)]
    for t, (a, b_, c) in enumerate(tris):
        pa, pb, pc = pos[a], pos[b_], pos[c]
        u = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]); w = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        if ln < 1e-12: continue
        area = ln / 2
        A, B, C = n[0] / ln, n[1] / ln, n[2] / ln
        D = -(A * pa[0] + B * pa[1] + C * pa[2])
        k = (A * A, A * B, A * C, A * D, B * B, B * C, B * D, C * C, C * D, D * D)
        for v in tris[t]:
            q = Q[v]
            for i in range(10): q[i] += area * k[i]
    # borda: aresta usada por um numero de triangulos diferente de 2 (costura de UV ou buraco)
    ec = {}
    for tr in tris:
        for i in range(3):
            e = (min(tr[i], tr[(i + 1) % 3]), max(tr[i], tr[(i + 1) % 3]))
            ec[e] = ec.get(e, 0) + 1
    border = [False] * nv
    for (a, b_), n in ec.items():
        if n != 2: border[a] = border[b_] = True
    wd = [dict(zip(g.sk_idx[v], g.sk_wt[v])) for v in range(nv)]

    def err(q, p):
        x, y, z = p
        return (q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y
                + q[7] * z * z + 2 * q[8] * z + q[9])

    def cost(u, v):  # remove u, mantem v
        q = [Q[u][i] + Q[v][i] for i in range(10)]
        c = max(0.0, err(q, pos[v]))
        wb = sum(abs(wd[u].get(k, 0) - wd[v].get(k, 0)) for k in set(wd[u]) | set(wd[v]))
        du = math.hypot(uv[u][0] - uv[v][0], uv[u][1] - uv[v][1])
        return c + diag2 * (W_BONE * wb + W_UV * du)

    ver = [0] * nv
    heap = []

    def push(u, v):
        if border[u]: return
        heapq.heappush(heap, (cost(u, v), ver[u], ver[v], u, v))

    def nbrs(v):
        s = set()
        for t in vt[v]: s.update(tris[t])
        s.discard(v)
        return s

    for a, b_ in ec:
        push(a, b_); push(b_, a)
    cur = sum(alive)
    print('  bordas: %d de %d vertices; arestas na fila: %d' % (sum(border), nv, len(heap)), flush=True)
    pops = 0
    removed_note = 0
    while cur > target and heap:
        c, vu, vv, u, v = heapq.heappop(heap)
        pops += 1
        if pops % 20000 == 0: print('  pops %d, triangulos %d, fila %d' % (pops, cur, len(heap)), flush=True)
        if ver[u] != vu or ver[v] != vv or border[u]: continue
        if v not in nbrs(u): continue
        both = [t for t in vt[u] if v in tris[t]]
        if len(both) != 2 or len(nbrs(u) & nbrs(v)) != 2: continue  # topologia: so aresta interna, sem criar nao-manifold
        ok = True
        for t in vt[u]:
            if v in tris[t]: continue
            a, b_, d = tris[t]
            old = [pos[a], pos[b_], pos[d]]
            new = [pos[v] if x == u else pos[x] for x in (a, b_, d)]
            def nrm(p):
                x1 = (p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]); x2 = (p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2])
                return (x1[1] * x2[2] - x1[2] * x2[1], x1[2] * x2[0] - x1[0] * x2[2], x1[0] * x2[1] - x1[1] * x2[0])
            n0, n1 = nrm(old), nrm(new)
            l0, l1 = math.sqrt(sum(x * x for x in n0)), math.sqrt(sum(x * x for x in n1))
            if l1 < 1e-10 or l0 < 1e-10 or sum(x * y for x, y in zip(n0, n1)) / (l0 * l1) < 0.2: ok = False; break
        if not ok: continue
        for t in list(vt[u]):
            if v in tris[t]:
                alive[t] = False
                for x in tris[t]: vt[x].discard(t)
                cur -= 1
            else:
                tris[t] = [v if x == u else x for x in tris[t]]
                vt[u].discard(t); vt[v].add(t)
        for i in range(10): Q[v][i] += Q[u][i]
        ver[u] += 1; ver[v] += 1
        for w in nbrs(v):
            push(w, v); push(v, w)
    keep = [t for t in range(len(tris)) if alive[t]]
    return keep


def write_geo(b, g, keep, out_path):
    used = sorted({v for t in keep for v in g.tris[t]})
    new = {v: i for i, v in enumerate(used)}
    nv, nt = len(used), len(keep)
    # struct da geometria
    s = bytearray(struct.pack('<IIII', g.flags, nt, nv, 1))
    for v in used: s += struct.pack('<2f', *g.uv[v])
    for t in keep:
        tr = g.tris[t]
        sv = [0, 0, 0]
        for k in range(3): sv[g.perm[k]] = new[tr[k]]
        s += struct.pack('<4H', sv[0], sv[1], g.mat[t], sv[2])
    s += g.sphere + struct.pack('<II', 1, 1 if g.nrm else 0)
    for v in used: s += struct.pack('<3f', *g.pos[v])
    if g.nrm:
        for v in used: s += struct.pack('<3f', *g.nrm[v])
    # binmesh: um bloco por material, na ordem de material
    bm = bytearray()
    mats = sorted({g.mat[t] for t in keep})
    for mi in mats:
        idx = [new[v] for t in keep if g.mat[t] == mi for v in g.tris[t]]
        bm += struct.pack('<II', len(idx), mi) + struct.pack('<%dI' % len(idx), *idx)
    bm = struct.pack('<III', 0, len(mats), 3 * nt) + bm
    sk = bytearray(g.sk_head)
    for v in used: sk += g.sk_idx[v]
    for v in used: sk += struct.pack('<4f', *g.sk_wt[v])
    sk += g.sk_rest
    # troca do fim para o comeco, ajustando o tamanho dos chunks que contem cada trecho
    out = bytearray(b)
    for (off, size), body, outer in sorted([(g.skin, bytes(sk), [g.ext]), (g.bin, bytes(bm), [g.ext]), (g.struct, bytes(s), [])], key=lambda x: -x[0][0]):
        delta = len(body) - size
        out[off:off + size] = body
        struct.pack_into('<I', out, off - 12 + 4, size + delta)  # o proprio cabecalho do trecho
        for h in sorted({0, find_list_hdr(b), g.hdr} | set(outer)):
            struct.pack_into('<I', out, h + 4, struct.unpack_from('<I', out, h + 4)[0] + delta)
    open(out_path, 'wb').write(out)
    return nv, nt


def find_list_hdr(b):
    p, s = find(b, 0, len(b), 0x1A)
    return p - 12


def preview(g, keep, path, W=900, H=300):
    """silhueta lateral (x,z) da malha original e da reduzida, so para conferir a forma"""
    from gen_ifp import png
    px = bytearray(b'\xf5' * (W * H * 3))
    xs = [p[0] for p in g.pos]; ys = [p[1] for p in g.pos]; zs = [p[2] for p in g.pos]
    ex = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    sc = (H * 0.8) / ex
    x0, z0 = min(xs), min(zs)
    def draw(tris, ox, col):
        for tr in tris:
            for i in range(3):
                a, b_ = g.pos[tr[i]], g.pos[tr[(i + 1) % 3]]
                n = 6
                for k in range(n + 1):
                    x = a[0] + (b_[0] - a[0]) * k / n; z = a[2] + (b_[2] - a[2]) * k / n
                    X, Y = int(ox + (x - x0) * sc), int(H * 0.9 - (z - z0) * sc)
                    if 0 <= X < W and 0 <= Y < H: px[(Y * W + X) * 3:(Y * W + X) * 3 + 3] = bytes(col)
    draw(g.tris_orig, 20, (120, 120, 120))
    draw([g.tris[t] for t in keep], W // 2 + 20, (200, 60, 60))
    png(path, W, H, px)


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--tris', type=int, default=10000)
    ap.add_argument('--preview')
    o = ap.parse_args()
    src, dst, target = o.src, o.dst, o.tris
    b = open(src, 'rb').read()
    g = read_geo(b)
    g.tris_orig = [list(t) for t in g.tris]
    print('%s: %d vertices, %d triangulos -> alvo %d' % (os.path.basename(src), g.nv, g.nt, target))
    keep = decimate(g, target)
    nv, nt = write_geo(b, g, keep, dst)
    print('saida: %d vertices, %d triangulos, %d KB -> %d KB' % (nv, nt, len(b) // 1024, os.path.getsize(dst) // 1024))
    if o.preview: preview(g, keep, o.preview)


if __name__ == '__main__':
    main()
