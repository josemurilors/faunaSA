"""Gera IFP (ANP3) simples de andar/correr/sprint/parado para os animais sem .ifp (vaca, raposa, capivara, gorila, pato, galinha).

Os modelos desses animais tem o esqueleto de 32 ossos do ped, mas a malha e de quadrupede/ave e quase toda presa ao Pelvis/Spine;
as pernas ficam presas as canelas (ossos 42 e 52: par da frente e par de tras) ou aos pes (43/53, aves). Entao a animacao e
procedural: gira a coxa e a canela em volta do eixo lateral, com leve balanco do corpo. As rotacoes de repouso e as posicoes vem
dos proprios frames do .dff (conferido: a cinematica dos frames bate com as matrizes inversas do skin).

Uso: python tools/gen_ifp.py <pasta cleo\\Animals> <pasta de saida> [--preview] [Anim4 Anim5 ...]
Saida: <saida>\\FaunaAnim<N>.ifp e <saida>\\animgrp.dat (copie a pasta para cleo\\Animals\\FaunaSA no jogo).
"""
import math, os, struct, sys, zlib

# ---------------------------------------------------------------- leitura do .dff
def chunks(b, pos, end):
    while pos + 12 <= end:
        t, s, v = struct.unpack_from('<III', b, pos)
        yield t, s, v, pos + 12
        pos += 12 + s

def find(b, pos, end, want):
    for t, s, v, p in chunks(b, pos, end):
        if t == want: return p, s
        if t in (0x10, 0x1A, 0x0F, 0x03):
            r = find(b, p, p + s, want)
            if r: return r
    return None

class Model:
    pass

def read_model(path):
    b = open(path, 'rb').read()
    m = Model()
    m.raw = b
    m.sk = None
    p, s = find(b, 0, len(b), 0x0E)
    kids = list(chunks(b, p, p + s))
    n = struct.unpack_from('<I', b, kids[0][3])[0]
    m.rot, m.pos, m.parent = [], [], []
    for i in range(n):
        v = struct.unpack_from('<12f2i', b, kids[0][3] + 4 + i * 56)
        # aves: o pelvis tem escala (~3,3) no .dff. O IFP so guarda rotacao pura, entao a animacao tira a escala e a ave
        # aparece reduzida (a malha foi modelada grande de proposito); a cinematica aqui ja usa rotacoes sem escala.
        rows = [norm(list(v[0:3])), norm(list(v[3:6])), norm(list(v[6:9]))]
        m.rot.append(rows); m.pos.append(list(v[9:12])); m.parent.append(v[12])
    m.bone_of_frame = [-1] * n
    m.hn = []
    exts = [k for k in kids[1:] if k[0] == 0x03]
    for i, e in enumerate(exts[:n]):
        for t, s2, v, p2 in chunks(b, e[3], e[3] + e[1]):
            if t == 0x11E:
                ver, nid, cnt = struct.unpack_from('<III', b, p2)
                m.bone_of_frame[i] = nid
                for k in range(cnt if cnt else 0):
                    m.hn.append(struct.unpack_from('<i', b, p2 + 20 + k * 12)[0])
    m.frame_of_bone = {bid: f for f, bid in enumerate(m.bone_of_frame) if bid >= 0}
    gp, gs = find(b, 0, len(b), 0x1A)
    m.inv = m.pts = m.idx = m.wt = None
    for t, s2, v, p2 in chunks(b, gp, gp + gs):
        if t != 0x0F: continue
        st = next(chunks(b, p2, p2 + s2)); q = st[3]
        fl, nt, nv, nm = struct.unpack_from('<IIII', b, q); q += 16
        if fl & 8: q += 4 * nv
        nuv = (fl >> 16) & 0xff or (2 if fl & 0x80 else (1 if fl & 4 else 0))
        q += 8 * nv * nuv + 8 * nt + 16 + 8
        vs = struct.unpack_from('<%df' % (3 * nv), b, q)
        m.pts = [vs[i * 3:i * 3 + 3] for i in range(nv)]
        for t3, s3, v3, p3 in chunks(b, p2, p2 + s2):
            if t3 != 0x03: continue
            for t4, s4, v4, p4 in chunks(b, p3, p3 + s3):
                if t4 == 0x116:
                    nb, nu, mw, _ = struct.unpack_from('<4B', b, p4)
                    m.sk = dict(data=p4, size=s4, nb=nb, nu=nu, mw=mw, nv=nv, used=list(b[p4 + 4:p4 + 4 + nu]))
                    o = p4 + 4 + nu
                    m.idx = [struct.unpack_from('<4B', b, o + i * 4) for i in range(nv)]
                    m.wt = [struct.unpack_from('<4f', b, o + nv * 4 + i * 16) for i in range(nv)]
                    off = o + nv * 4 + nv * 16
                    m.inv = [struct.unpack_from('<16f', b, off + i * 64) for i in range(nb)]
                    m.sk['mat'] = off
        break
    return m

# ---------------------------------------------------------------- matrizes (convencao RW: vetor-linha, mundo = local x pai)
def mul(a, b): return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def T(a): return [[a[j][i] for j in range(3)] for i in range(3)]
def inv3(a):
    c = [[a[1][1] * a[2][2] - a[1][2] * a[2][1], a[0][2] * a[2][1] - a[0][1] * a[2][2], a[0][1] * a[1][2] - a[0][2] * a[1][1]],
         [a[1][2] * a[2][0] - a[1][0] * a[2][2], a[0][0] * a[2][2] - a[0][2] * a[2][0], a[0][2] * a[1][0] - a[0][0] * a[1][2]],
         [a[1][0] * a[2][1] - a[1][1] * a[2][0], a[0][1] * a[2][0] - a[0][0] * a[2][1], a[0][0] * a[1][1] - a[0][1] * a[1][0]]]
    d = sum(a[0][j] * c[j][0] for j in range(3))
    return [[x / d for x in r] for r in c]

def vmul(v, m): return [sum(v[i] * m[i][j] for i in range(3)) for j in range(3)]
def norm(v):
    l = math.sqrt(sum(c * c for c in v)); return [c / l for c in v]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def dot(a, b): return sum(x * y for x, y in zip(a, b))

def rot_axis(n, a):
    """rotacao de a rad em volta do eixo n (convencao vetor-linha: v' = v x D)"""
    x, y, z = n; c, s = math.cos(a), math.sin(a); k = 1 - c
    Rc = [[c + x * x * k, x * y * k - z * s, x * z * k + y * s],
          [y * x * k + z * s, c + y * y * k, y * z * k - x * s],
          [z * x * k - y * s, z * y * k + x * s, c + z * z * k]]
    return T(Rc)

def q2m(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
            [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
            [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]]

def m2q(m):
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = 2 * math.sqrt(tr + 1); w = s / 4; x = (m[1][2] - m[2][1]) / s; y = (m[2][0] - m[0][2]) / s; z = (m[0][1] - m[1][0]) / s
    elif m[0][0] >= m[1][1] and m[0][0] >= m[2][2]:
        s = 2 * math.sqrt(1 + m[0][0] - m[1][1] - m[2][2]); x = s / 4; y = (m[0][1] + m[1][0]) / s; z = (m[0][2] + m[2][0]) / s; w = (m[1][2] - m[2][1]) / s
    elif m[1][1] >= m[2][2]:
        s = 2 * math.sqrt(1 + m[1][1] - m[0][0] - m[2][2]); y = s / 4; x = (m[0][1] + m[1][0]) / s; z = (m[1][2] + m[2][1]) / s; w = (m[2][0] - m[0][2]) / s
    else:
        s = 2 * math.sqrt(1 + m[2][2] - m[0][0] - m[1][1]); z = s / 4; x = (m[0][2] + m[2][0]) / s; y = (m[1][2] + m[2][1]) / s; w = (m[0][1] - m[1][0]) / s
    l = math.sqrt(x * x + y * y + z * z + w * w)
    return (x / l, y / l, z / l, w / l)


# ---------------------------------------------------------------- pernas separadas (4 patas)
# Nesses modelos as quatro patas estao presas a so dois ossos (canelas 42 e 52): cada par anda junto, como um pulo. Aqui o .dff
# e copiado com as patas reatribuidas a quatro ossos (UpperArm L/R e Thigh L/R, sem malha no original), com o pivo (a matriz
# inversa do skin) no quadril/ombro de cada pata. O IFP gerado coloca esses ossos nos mesmos pontos.
LEG_BONES = {'FL': 32, 'FR': 22, 'HL': 41, 'HR': 51}

def kmeans2(vals):
    lo, hi = min(vals), max(vals)
    for _ in range(30):
        a = [v for v in vals if abs(v - lo) <= abs(v - hi)] or [lo]; b = [v for v in vals if abs(v - lo) > abs(v - hi)] or [hi]
        lo, hi = sum(a) / len(a), sum(b) / len(b)
    return lo, hi

def plan_legs(m):
    """pivos das 4 patas e o y que separa esquerda de direita em cada par; None se o modelo nao tem o desenho 'um osso por par'"""
    hn = m.hn
    if 42 not in hn or 52 not in hn: return None
    i42, i52 = hn.index(42), hn.index(52)
    groups = {}
    for name, idx in (('front', i42), ('hind', i52)):
        vs = [m.pts[v] for v, (ids, ws) in enumerate(zip(m.idx, m.wt)) if any(ids[k] == idx and ws[k] >= 0.5 for k in range(4))]
        if len(vs) < 40: return None
        groups[name] = vs
    if sum(p[0] for p in groups['front']) / len(groups['front']) < sum(p[0] for p in groups['hind']) / len(groups['hind']): return None  # frente = +x
    plan = {'thr': {}, 'pivot': {}}
    for name, vs in groups.items():
        a, b = kmeans2([p[1] for p in vs])
        if abs(a - b) < 0.08: return None  # nao ha duas patas separadas em y
        thr = (a + b) / 2
        plan['thr'][name] = thr
        for side, sel in (('L', [p for p in vs if p[1] > thr]), ('R', [p for p in vs if p[1] <= thr])):
            plan['pivot'][('F' if name == 'front' else 'H') + side] = (sum(p[0] for p in sel) / len(sel), sum(p[1] for p in sel) / len(sel), max(p[2] for p in sel) + 0.04)
    return plan

def patch_legs(m, plan, out_path):
    b = bytearray(m.raw)
    sk = m.sk; nv, nb, nu = sk['nv'], sk['nb'], sk['nu']
    hn = m.hn
    ix = {n: hn.index(bid) for n, bid in LEG_BONES.items()}
    used = sorted(set(sk['used']) - {hn.index(42), hn.index(52)} | set(ix.values()))
    idx_off = sk['data'] + 4 + nu
    new_idx = bytearray(b[idx_off:idx_off + nv * 4])
    for v, (ids, ws) in enumerate(zip(m.idx, m.wt)):
        y = m.pts[v][1]
        for k in range(4):
            if ids[k] == hn.index(42): new_idx[v * 4 + k] = ix['FL'] if y > plan['thr']['front'] else ix['FR']
            elif ids[k] == hn.index(52): new_idx[v * 4 + k] = ix['HL'] if y > plan['thr']['hind'] else ix['HR']
    mats = bytearray(b[sk['mat']:sk['mat'] + nb * 64])
    for n, (px, py, pz) in plan['pivot'].items():
        o = ix[n] * 64
        M = struct.unpack_from('<16f', mats, o)
        R = [[M[0], M[1], M[2]], [M[4], M[5], M[6]], [M[8], M[9], M[10]]]
        t = [-sum((px, py, pz)[i] * R[i][j] for i in range(3)) for j in range(3)]
        struct.pack_into('<3f', mats, o + 48, *t)
    body = bytes([nb, len(used), sk['mw'], 0]) + bytes(used) + bytes(new_idx) + bytes(b[idx_off + nv * 4:sk['mat']]) + bytes(mats) + bytes(b[sk['mat'] + nb * 64:sk['data'] + sk['size']])
    delta = len(body) - sk['size']
    # reescreve o chunk 0x116 e corrige o tamanho dos chunks que o contem (clump > lista de geometrias > geometria > extensao)
    chain = []
    def walk(pos, end, stack):
        for t, s2, v, p2 in chunks(b, pos, end):
            if p2 <= sk['data'] < p2 + s2 and t in (0x10, 0x1A, 0x0F, 0x03): walk(p2, p2 + s2, stack + [p2 - 12])
            elif p2 == sk['data']: chain.extend(stack + [p2 - 12])
    walk(0, len(b), [])
    out = bytearray(b[:sk['data']]) + body + b[sk['data'] + sk['size']:]
    for h in chain:
        struct.pack_into('<I', out, h + 4, struct.unpack_from('<I', out, h + 4)[0] + delta)
    open(out_path, 'wb').write(out)

# ---------------------------------------------------------------- cinematica
def children_order(m):
    order, seen = [], set()
    def visit(f):
        if f in seen: return
        p = m.parent[f]
        if p >= 0 and p != f: visit(p)
        seen.add(f); order.append(f)
    for f in range(len(m.parent)): visit(f)
    return order

def fk(m, rot, pos):
    """rot/pos por frame (locais) -> (W, P) mundiais por frame"""
    W, P = {}, {}
    for f in children_order(m):
        p = m.parent[f]
        if p < 0 or p == f: W[f], P[f] = rot[f], pos[f]
        else:
            W[f] = mul(rot[f], W[p]); P[f] = [a + b for a, b in zip(vmul(pos[f], W[p]), P[p])]
    return W, P

class Rig:
    """eixos e ossos usados na animacao, achados a partir do proprio modelo"""
    def __init__(self, m):
        self.m = m
        self.W0, self.P0 = fk(m, m.rot, m.pos)
        bp = lambda bid: self.P0[m.frame_of_bone[bid]]
        # pernas: canelas 42/52 (quadrupede) ou pes 43/53 (ave); o que tem vertices presos manda
        weight = {}
        for ids, ws in zip(m.idx, m.wt):
            k = max(range(4), key=lambda j: ws[j]); b = m.hn[ids[k]]; weight[b] = weight.get(b, 0) + 1
        self.weight = weight
        self.bird = weight.get(43, 0) > 0 and weight.get(53, 0) > 0
        # quatro patas proprias (modelo ja passado por patch_legs): pivos vem da matriz inversa do skin
        self.legs = {}
        self.quad4 = all(weight.get(bid, 0) > 40 for bid in LEG_BONES.values())
        if self.quad4:
            for bid in LEG_BONES.values():
                M = m.inv[m.hn.index(bid)]; t = (M[12], M[13], M[14]); R = [[M[0], M[1], M[2]], [M[4], M[5], M[6]], [M[8], M[9], M[10]]]
                self.legs[bid] = [-sum(t[k] * R[j][k] for k in range(3)) for j in range(3)]
        self.dz = 0.0
        if self.bird:
            lat = norm([a - b for a, b in zip(bp(43), bp(53))])
            body = bp(2)
            mid = [(a + b) / 2 for a, b in zip(bp(43), bp(53))]
            v = [a - b for a, b in zip(body, mid)]
            up = norm([a - dot(v, lat) * l for a, l in zip(v, lat)])
            def snap(v):  # os modelos sao alinhados aos eixos; evita inclinar o corpo por causa de pes fora de prumo
                k = max(range(3), key=lambda i: abs(v[i])); return [math.copysign(1.0, v[k]) if i == k else 0.0 for i in range(3)]
            lat, up = snap(lat), snap(up)
            fwd = cross(lat, up)
            hs = sorted(m.pts, key=lambda q: -dot(q, up))[:max(1, len(m.pts) // 20)]  # a cabeca e o ponto mais alto da malha
            cen = [sum(q[i] for q in m.pts) / len(m.pts) for i in range(3)]
            hv = [sum(q[i] for q in hs) / len(hs) - cen[i] for i in range(3)]
            if dot(hv, fwd) < 0: fwd = [-c for c in fwd]; lat = [-c for c in lat]  # a cabeca fica na frente
            self.lat = tuple(lat); self.up = tuple(up); self.fwd = tuple(fwd)
        else:
            self.up = (0, 0, 1); self.fwd = (1, 0, 0); self.lat = tuple(cross(self.up, self.fwd))
            # comprimento da perna = altura do quadril acima do ponto mais baixo da malha presa a canela
            low = min(p[2] for p, ids, ws in zip(m.pts, m.idx, m.wt) if m.hn[ids[max(range(4), key=lambda j: ws[j])]] in (42, 52, 32, 22, 41, 51))
            hip = sum(v[2] for v in self.legs.values()) / len(self.legs) if self.legs else bp(51)[2]
            self.leg = hip - low

        # a malha fica deitada nos eixos do modelo; a animacao do Root gira o corpo para frente = +y do ped, cima = +z (como no Boar.ifp)
        self._rel()

    def _rel(self):
        mesh = [list(self.fwd), list(self.lat), list(self.up)]
        self.rel = mul(inv3(mesh), [[0, 1, 0], [-1, 0, 0], [0, 0, 1]])

    def face_forward(self):
        """confere no corpo ja posado (com a escala do skin) se a cabeca - o ponto mais alto - fica a frente (+y); senao inverte a frente"""
        pts = skin(self, *self.pose({}, {}))
        top = sorted(pts, key=lambda q: -q[2])[:max(1, len(pts) // 20)]
        dy = sum(q[1] for q in top) / len(top) - sum(q[1] for q in pts) / len(pts)
        if dy < 0:
            self.fwd = tuple(-c for c in self.fwd); self.lat = tuple(-c for c in self.lat); self._rel()
        return dy

    def local_delta(self, f, vworld):
        """vetor em coordenadas do mundo -> coordenadas locais do frame f (para somar a posicao local)"""
        p = self.m.parent[f]
        return vmul(vworld, inv3(self.W0[p])) if p >= 0 and p != f else list(vworld)

    def pose(self, ang, off):
        """ang: {bone: rad em volta do eixo lateral}, roll: {bone: rad em volta do eixo frontal}; off: {bone: deslocamento em coords do mundo}"""
        m = self.m
        rot = [r for r in m.rot]; pos = [list(p) for p in m.pos]
        for bid, P in self.legs.items():
            f = m.frame_of_bone[bid]; par = m.parent[f]
            pos[f] = vmul([a - b for a, b in zip(P, self.P0[par])], inv3(self.W0[par]))
        for key, a in ang.items():
            bid, axis = key
            f = m.frame_of_bone[bid]; p = m.parent[f]
            Wp = self.W0[p]
            rot[f] = mul(m.rot[f], mul(Wp, mul(rot_axis(axis, a), inv3(Wp))))
        froot = m.frame_of_bone[0]
        rot[froot] = mul(m.rot[froot], self.rel)
        for bid, v in off.items():
            f = m.frame_of_bone[bid]
            d = self.local_delta(f, v)
            pos[f] = [a + b for a, b in zip(pos[f], d)]
        return rot, pos

# ---------------------------------------------------------------- animacoes
D2R = math.pi / 180

# tempo do ciclo (s) e amplitude do quadril (graus) por ciclo; a velocidade sai de 4*A*L/T (sem deslizar o pe)
GAITS = {'walk': (1.0, 24), 'run': (0.55, 38), 'sprint': (0.42, 44)}
SPECIES = {'Anim9': {'walk': (1.3, 14), 'run': (0.85, 22), 'sprint': (0.65, 28)}}  # gorila: mais lento e pesado
LEG_PHASE = {  # fase de cada pata (fracao do ciclo): passo lateral, trote e galope
    'walk': {'HL': 0.0, 'FL': 0.25, 'HR': 0.5, 'FR': 0.75},
    'run': {'FL': 0.0, 'HR': 0.0, 'FR': 0.5, 'HL': 0.5},
    'sprint': {'HL': 0.0, 'HR': 0.08, 'FL': 0.5, 'FR': 0.58}}

def clips(rig, folder=None):
    """nome -> (duracao, funcao(fase 0..1) -> (ang, off), velocidade m/s)"""
    up, lat, fwd = rig.up, rig.lat, rig.fwd
    gaits = SPECIES.get(folder, GAITS)
    def quad4(n, T_, A, pitch, bob):
        def f(ph):
            p = 2 * math.pi * ph
            ang, off = {}, {1: [c * bob * math.cos(2 * p) for c in up]}
            for leg, bid in LEG_BONES.items():
                q = p - 2 * math.pi * LEG_PHASE[n][leg]
                ang[(bid, lat)] = A * math.sin(q)
                off[bid] = [c * 0.12 * rig.leg * max(0.0, -math.cos(q)) for c in up]  # a pata inteira sobe ao ir para a frente
            ang[(1, lat)] = pitch * math.sin(p)
            return ang, off
        return f
    def pairs(T_, A, B, pitch, bob, lag):  # fallback: so duas patas (um osso por par), caso do gorila
        def f(ph):
            p = 2 * math.pi * ph
            ang = {}
            for thigh, calf, shift in ((41, 42, 0.0), (51, 52, lag)):
                q = p - shift
                ang[(thigh, lat)] = A * math.sin(q)
                ang[(calf, lat)] = B * max(0.0, -math.cos(q))
            ang[(1, lat)] = pitch * math.sin(p)
            return ang, {1: [c * bob * math.cos(2 * p) for c in up]}
        return f
    def birdgait(T_, A, h, size):
        def f(ph):
            p = 2 * math.pi * ph
            ang, off = {}, {}
            for foot, sgn in ((43, 1), (53, -1)):
                q = p + (0 if sgn > 0 else math.pi)
                ang[(foot, lat)] = A * math.sin(q)
                off[foot] = [c * h * max(0.0, -math.cos(q)) for c in up]
            ang[(1, fwd)] = 5 * D2R * math.sin(p)  # balanca de lado a lado
            off[1] = [c * 0.02 * size * math.cos(2 * p) for c in up]
            return ang, off
        return f
    def idle(ph):
        p = 2 * math.pi * ph
        return {(4, lat): 2 * D2R * math.sin(p)}, {1: [c * 0.004 * math.sin(p) for c in up]}
    out = {}
    if rig.bird:
        spec = {'walk': (0.9, 30, 0.10, 0.8), 'run': (0.5, 40, 0.16, 2.4), 'sprint': (0.38, 45, 0.2, 4.0)}
        size = abs(dot([a - b for a, b in zip(rig.P0[rig.m.frame_of_bone[2]], rig.P0[rig.m.frame_of_bone[43]])], up))
        for n, (T_, A, h, v) in spec.items():
            out[n] = (T_, birdgait(T_, A * D2R, h * size, size), v)
    else:
        for n, (T_, A) in gaits.items():
            v = 4 * A * D2R * rig.leg / T_
            if rig.quad4: out[n] = (T_, quad4(n, T_, A * D2R, (0.5 if n == 'walk' else 1.5) * D2R, 0.006 * rig.leg), v)
            else: out[n] = (T_, pairs(T_, A * D2R, 0.75 * A * D2R, (0.5 if n == 'walk' else 2) * D2R, 0.015 * rig.leg, math.pi if n == 'walk' else 0.7 * math.pi), v)
    out['idle'] = (3.0, idle, 0.0)
    return out

def build_anim(rig, name, T_, fn, speed, steps=16):
    m = rig.m
    bones = [bid for bid in m.bone_of_frame if bid >= 0]
    seqs = []
    for bid in bones:
        seqs.append([bid, []])
    prev = {}
    for k in range(steps + 1):
        ph = (k % steps) / steps
        t = T_ * k / steps
        ang, off = fn(ph)
        rot, pos = rig.pose(ang, off)
        for seq in seqs:
            bid = seq[0]; f = m.frame_of_bone[bid]
            q = m2q(rot[f])
            if bid in prev and dot(q, prev[bid]) < 0: q = tuple(-c for c in q)
            prev[bid] = q
            px, py, pz = pos[f]
            if bid == 0: pz += rig.dz; px += speed * T_ * k / steps  # o ASI mede a velocidade pelo deslocamento da raiz e depois zera x,y
            seq[1].append((q, t, (px, py, pz)))
    return name, seqs

BONE_NAMES = {0: 'Root', 1: 'Pelvis', 2: 'Spine', 3: 'Spine1', 4: 'Neck', 5: 'Head', 6: 'L Brow', 7: 'R Brow', 8: 'Jaw', 21: 'Bip01 R Clavicle', 22: 'R UpperArm',
              23: 'R ForeArm', 24: 'R Hand', 25: 'R Finger', 26: 'R Finger01', 31: 'Bip01 L Clavicle', 32: 'L UpperArm', 33: 'L ForeArm', 34: 'L Hand',
              35: 'L Finger', 36: 'L Finger01', 41: 'L Thigh', 42: 'L Calf', 43: 'L Foot', 44: 'L Toe0', 51: 'R Thigh', 52: 'R Calf', 53: 'R Foot',
              54: 'R Toe0', 201: 'Belly', 301: 'R breast', 302: 'L breast'}

def fixed(s, n=24): return s.encode('latin1')[:n - 1].ljust(n, b'\0')

def write_ifp(path, block, anims):
    body = b''
    for name, seqs in anims:
        data = b''; sh = b''
        for bid, keys in seqs:
            frames = b''.join(struct.pack('<4f1f3f', *q, t, *p) for q, t, p in keys)  # tipo 2: rot + tempo + pos, floats
            sh += fixed(BONE_NAMES.get(bid, 'Bone%d' % bid)) + struct.pack('<IIi', 2, len(keys), bid) + frames
            data += frames
        body += fixed(name) + struct.pack('<III', len(seqs), len(data), 0) + sh
    blob = b'ANP3' + struct.pack('<I', 0) + fixed(block) + struct.pack('<I', len(anims)) + body
    open(path, 'wb').write(blob[:4] + struct.pack('<I', len(blob) - 8) + blob[8:])

# ---------------------------------------------------------------- previa em PNG (renderiza a malha com o skin)
def skin(rig, rot, pos):
    m = rig.m
    W, P = fk(m, rot, pos)
    mats = []
    for i, bid in enumerate(m.hn):
        f = m.frame_of_bone[bid]; M = m.inv[i]
        R = [[M[0], M[1], M[2]], [M[4], M[5], M[6]], [M[8], M[9], M[10]]]; t = (M[12], M[13], M[14])
        mats.append((R, t, W[f], P[f]))
    out = []
    for p, ids, ws in zip(m.pts, m.idx, m.wt):
        acc = [0.0, 0.0, 0.0]
        for k in range(4):
            if ws[k] <= 0: continue
            R, t, Wb, Pb = mats[ids[k]]
            v = [a + b for a, b in zip(vmul(p, R), t)]
            v = [a + b for a, b in zip(vmul(v, Wb), Pb)]
            acc = [a + ws[k] * b for a, b in zip(acc, v)]
        out.append(acc)
    return out

def png(path, w, h, px):
    raw = b''.join(b'\0' + bytes(px[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def ck(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t + d))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ck(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ck(b'IDAT', zlib.compress(raw)) + ck(b'IEND', b''))

def preview(rig, clip_name, T_, fn, path, cols=6, cell=200):
    m = rig.m
    W = cols * cell; H = cell
    px = bytearray([245] * (W * H * 3))
    col = {1: (90, 90, 90), 2: (90, 90, 90), 42: (210, 50, 50), 52: (50, 80, 210), 43: (40, 160, 60), 53: (230, 140, 20), 32: (210, 50, 50), 22: (230, 140, 20), 41: (50, 80, 210), 51: (40, 160, 60)}
    dom = []
    for ids, ws in zip(m.idx, m.wt):
        dom.append(m.hn[ids[max(range(4), key=lambda j: ws[j])]])
    scale = cell * 0.4 / max(1e-6, max(abs(c) for p in m.pts for c in p))
    for c in range(cols):
        ang, off = fn(c / cols)
        pts = skin(rig, *rig.pose(ang, off))
        ox, oy = c * cell + cell // 2, cell // 2
        for p, d in zip(pts, dom):
            u, v = p[1], p[2]  # depois do Root: frente = +y, cima = +z
            x, y = int(ox + u * scale), int(oy - v * scale)
            if 0 <= x < W - 1 and 0 <= y < H - 1:
                r, g, b = col.get(d, (120, 120, 120))
                for dx in (0, 1):
                    for dy in (0, 1):
                        i = ((y + dy) * W + x + dx) * 3; px[i:i + 3] = bytes((r, g, b))
        for x in range(c * cell, c * cell + cell):  # linha do chao (ponto mais baixo no repouso)
            pass
    png(path, W, H, px)

# ---------------------------------------------------------------- principal
NO_SPLIT = {'Anim9'}  # gorila: os bracos ja tem malha presa, fica com o par de patas

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    do_prev = '--preview' in sys.argv
    src, dst, folders = args[0], args[1], args[2:] or ['Anim4', 'Anim5', 'Anim6', 'Anim8', 'Anim9', 'Anim10', 'Anim11', 'Anim12', 'Anim14']
    lst = os.path.join(src, 'FaunaSA', 'imported.txt')  # pastas criadas por import_pack.py
    if len(args) <= 2 and os.path.exists(lst): folders += open(lst).read().split()
    os.makedirs(os.path.join(dst, 'models'), exist_ok=True)
    grp = ['# gerado por tools/gen_ifp.py' + chr(10)]
    for folder in folders:
        d = os.path.join(src, folder)
        dffs = sorted(f for f in os.listdir(d) if f.lower().endswith('.dff'))
        variants = [f for f in dffs if f[0] in 'Aa' and '_' in f]
        # o ASI sorteia entre as variantes A<n>_<k> quando existem; so sem elas usa o <pasta>.dff
        path = next(os.path.join(d, f) for f in variants + [folder + '.dff'] if os.path.exists(os.path.join(d, f)))
        m0 = read_model(path)
        plan = None if folder in NO_SPLIT else plan_legs(m0)
        def prepare(fname):
            """modelo (ja com as 4 patas, se for o caso) que o Rig deve ler"""
            fp = os.path.join(d, fname)
            if not plan: return read_model(fp)
            out = os.path.join(dst, 'models', fname)
            mv = read_model(fp)
            patch_legs(mv, plan_legs(mv) or plan, out)  # pivos de cada raca/variante (a silhueta muda de uma para outra)
            return read_model(out)
        m = prepare(os.path.basename(path))
        todo = [(folder, path, m)]
        for other in (variants or [f for f in dffs if f != os.path.basename(path)]):
            if other == os.path.basename(path): continue
            o = prepare(other)
            same = o.parent == m.parent and o.bone_of_frame == m.bone_of_frame and all(
                abs(a - b) < 0.1 for ra, rb in zip(o.rot, m.rot) for r1, r2 in zip(ra, rb) for a, b in zip(r1, r2)) and all(
                abs(a - b) < 0.1 for pa, pb in zip(o.pos, m.pos) for a, b in zip(pa, pb))
            if not same: todo.append((os.path.splitext(other)[0], os.path.join(d, other), o))  # esqueleto proprio: grupo por modelo
        for name, mpath, mod in todo:
            rig = Rig(mod)
            dy = rig.face_forward()
            low = min(q[2] for q in skin(rig, *rig.pose({}, {})))
            rig.dz = -0.99 - low  # a malha pousa no chao (o centro do ped fica 1 m acima dele)
            block = 'Fauna' + name
            cl = clips(rig, folder)
            anims = []
            for n in ('walk', 'run', 'sprint', 'idle'):
                T_, fn, v = cl[n]
                anims.append(build_anim(rig, '%s_%s' % (block, n), T_, fn, v))
                if do_prev: preview(rig, n, T_, fn, os.path.join(dst, '%s_%s.png' % (block, n)))
            write_ifp(os.path.join(dst, block + '.ifp'), block, anims)
            grp.append('%s, %s, walkcycle, 6' % (block, block) + chr(10) + ''.join(chr(9) + '%s_%s' % (block, n) + chr(10) for n in ('walk', 'run', 'sprint', 'idle', 'idle', 'walk')) + 'end' + chr(10) + chr(10))
            print('%-7s %s  %s  vel walk/run/sprint = %.1f/%.1f/%.1f m/s  dz=%.2f  cabeca %+.2f  (%s)' % (name, 'AVE' if rig.bird else ('4 PATAS' if rig.quad4 else '2 PATAS'), os.path.basename(mpath), cl['walk'][2], cl['run'][2], cl['sprint'][2], rig.dz, dy, block + '.ifp'))
    open(os.path.join(dst, 'animgrp.dat'), 'w').write(''.join(grp))

if __name__ == '__main__':
    main()
