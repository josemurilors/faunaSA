r"""Converte animacoes .ska do cavalo do Gun (2005) em IFP (ANP3) para o cavalo do FaunaSA (cavalo.dff, esqueleto de ped do GTA SA).

Uso: python gun_to_ifp.py <global.pak.xbx do Gun> <horse_animation.ifp original> <cavalo.dff> <saida.ifp> [--preview pasta]   (retarget para o cavalo antigo)
     python gun_to_ifp.py <global.pak.xbx do Gun> <horse_animation.ifp original> <saida.ifp> --native                  (modelo novo de gun_horse_dff.py)

Retarget por orientacao no mundo: para cada osso do SA mapeado a um osso do Gun, a rotacao do Gun em relacao a pose parada (standidle) vira a
mesma mudanca de orientacao sobre a pose parada do SA (horse_idle), depois de trocar os eixos (Gun: x esquerda, y cima, z frente; SA: x direita,
y frente, z cima). Ossos sem par (dedos, sobrancelhas, barriga...) ficam como na pose parada e acompanham o pai. A saida e o horse_animation.ifp
original com as animacoes do cavalo (idle, idle_1, walk, run, sprint, kick_forward) trocadas pelas do Gun; as do cavaleiro ficam intactas.
"""
import math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_ifp as G
import gun_pak, gun_model

# ---------------------------------------------------------------- IFP (ANP3) do SA
KS = {1: 20, 2: 32, 3: 10, 4: 16}

def fixed_str(b, p): return b[p:p + 24].split(b'\0')[0].decode('latin1')

def read_ifp(path):
    """-> (nome do bloco, [(nome, [(bone_id, tipo, [(t, (x,y,z,w), pos|None)])], bytes originais da animacao)])"""
    b = open(path, 'rb').read()
    if b[:4] != b'ANP3': sys.exit('IFP nao e ANP3')
    block = fixed_str(b, 8); n = struct.unpack_from('<I', b, 32)[0]; p = 36
    anims = []
    for _ in range(n):
        start = p; name = fixed_str(b, p); p += 24
        nseq, _, _ = struct.unpack_from('<III', b, p); p += 12
        seqs = []
        for _ in range(nseq):
            p += 24
            typ, nk, bid = struct.unpack_from('<IIi', b, p); p += 12
            keys = []
            for k in range(nk):
                if typ in (1, 2):
                    v = struct.unpack_from('<4ff' if typ == 1 else '<4f1f3f', b, p + k * KS[typ])
                    keys.append((v[4], tuple(v[:4]), tuple(v[5:8]) if typ == 2 else None))
                else:
                    v = struct.unpack_from('<%dh' % (KS[typ] // 2), b, p + k * KS[typ])
                    keys.append((v[4] / 60, tuple(c / 4096 for c in v[:4]), tuple(c / 1024 for c in v[5:8]) if typ == 4 else None))
            seqs.append((bid, typ, keys)); p += nk * KS[typ]
        anims.append((name, seqs, b[start:p]))
    return block, anims

def write_ifp(path, block, anims):
    """anims: [(nome, bytes da animacao)] -> ANP3"""
    body = b''.join(a for _, a in anims)
    blob = b'ANP3' + struct.pack('<I', 0) + G.fixed(block) + struct.pack('<I', len(anims)) + body
    open(path, 'wb').write(blob[:4] + struct.pack('<I', len(blob) - 8) + blob[8:])

def anim_bytes_c(name, seqs):
    """seqs: [(bone_id, tipo 3|4, [(q, t, pos)])] -> animacao comprimida como as do jogo (int16: rotacao/4096, tempo*60, posicao*1024; flags = 1).
    Tipo 3 so tem rotacao (ossos do CJ sem translacao), tipo 4 tem tambem a posicao (raiz)."""
    sh = b''; size = 0
    for bid, typ, keys in seqs:
        frames = b''
        for q, t, p in keys:
            v = [round(c * 4096) for c in q] + [round(t * 60)] + ([round(c * 1024) for c in p] if typ == 4 else [])
            assert all(-32768 <= x < 32768 for x in v), 'valor fora do int16 no osso %d' % bid
            frames += struct.pack('<%dh' % len(v), *v)
        sh += G.fixed(G.BONE_NAMES.get(bid, 'Bone%d' % bid)) + struct.pack('<IIi', typ, len(keys), bid) + frames; size += len(frames)
    return G.fixed(name) + struct.pack('<III', len(seqs), size, 1) + sh

def anim_bytes(name, seqs):
    """seqs: [(bone_id, [(q, t, pos)])] -> bytes de uma animacao (tipo 2, floats)"""
    data = b''; sh = b''
    for bid, keys in seqs:
        frames = b''.join(struct.pack('<4f1f3f', *q, t, *p) for q, t, p in keys)
        sh += G.fixed(G.BONE_NAMES.get(bid, 'Bone%d' % bid)) + struct.pack('<IIi', 2, len(keys), bid) + frames
        data += frames
    return G.fixed(name) + struct.pack('<III', len(seqs), len(data), 0) + sh

# ---------------------------------------------------------------- algebra 3x3 (convencao coluna: mundo = pai x local)
def mm(a, b): return G.mul(a, b)
def tp(a): return G.T(a)
def row2col(rows): return tp(rows)          # as matrizes do .dff/IFP do SA sao vetor-linha

# Gun (x esquerda, y cima, z frente) -> SA (x direita, y frente, z cima)
C = [[-1, 0, 0], [0, 0, 1], [0, 1, 0]]
K = 0.0254        # polegadas do Gun -> metros
GROUND = 0.93     # distancia do centro do ped ao chao (kHorseCenter 0,95 menos a folga)
# modelo novo (gun_horse_dff.py): osso do Gun -> id de osso do SA; 9, 10, 11 (orelhas e topete) viram a cabeca
SA_ID = {0: 0, 1: 1, 2: 2, 3: 6, 4: 7, 5: 3, 6: 4, 7: 8, 8: 5, 12: 31, 13: 32, 14: 33, 15: 34, 16: 35, 17: 21, 18: 22, 19: 23, 20: 24, 21: 25,
         22: 201, 23: 301, 24: 302, 25: 36, 26: 26, 27: 51, 28: 52, 29: 53, 30: 54, 31: 41, 32: 42, 33: 43, 34: 44}
MERGE = {9: 8, 10: 8, 11: 8}
SEAT_Y = 0.0      # a sela do Gun fica na origem do ped: mirando, o CJ e posto na origem (sem deslocamento, senao o jogo da coronhada no cavalo)
                  # e as animacoes do cavaleiro (gun_rider_ifp.py) levam a raiz do CJ 0,33 m para frente para ele sentar ali

def origin(gun):
    """posicao do osso raiz em relacao a origem do ped: a sela fica em SEAT_Y e os cascos no chao"""
    _, P = gun.world(gun.anim('horse_standidle.ska'), 0, True)
    return (0.0, SEAT_Y - P[2][2] * K, -GROUND)

def gq2m(q):
    """quaternio do .ska -> matriz coluna (as chaves vem conjugadas)"""
    x, y, z, w = -q[0], -q[1], -q[2], q[3]
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]

def qnorm(q):
    l = math.sqrt(sum(c * c for c in q)); return tuple(c / l for c in q)
def slerp(a, b, t):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0: b = tuple(-c for c in b); d = -d
    if d > 0.9995: return qnorm(tuple(x + (y - x) * t for x, y in zip(a, b)))
    th = math.acos(d); s = math.sin(th)
    return tuple((math.sin((1 - t) * th) * x + math.sin(t * th) * y) / s for x, y in zip(a, b))

# ---------------------------------------------------------------- esqueleto do Gun
class GunRig:
    def __init__(self, pak, ske='anl_horse'):    # ske = 'gun' para o esqueleto humano (cavaleiro)
        self.data = gun_pak.raiz(pak)
        self.es, self.d = gun_pak.le_pak(pak); self.tab = gun_pak.nomes(self.data)
        self.Q = gun_model.codebook(self.data, 'standardkeyQ.bin'); self.T = gun_model.codebook(self.data, 'standardkeyT.bin')
        o = next(o for t, o, s, h in self.es if t == 0x7330095c and self.tab.get(h) == ske)
        w = struct.unpack_from('<12I', self.d, o); n = self.n = w[1]
        sk = gun_model.le_ske(self.d, o, self.tab)
        self.name = [s[0] for s in sk]; self.par = [s[1] for s in sk]; self.loc = [s[3] for s in sk]
        R = [[[M[0], M[1], M[2]], [M[4], M[5], M[6]], [M[8], M[9], M[10]]] for M in
             (struct.unpack_from('<16f', self.d, o + w[10] + 64 * i) for i in range(n))]
        self.L = [R[i] if self.par[i] < 0 or self.par[i] == i else mm(tp(R[self.par[i]]), R[i]) for i in range(n)]

    def anim(self, nome, grupo='anl_horse'):
        for t, o, s, h in self.es:
            n = (self.tab.get(h) or '').replace('\\', '/')
            if t == 0x745dcd45 and '/' + grupo + '/' in '/' + n and n.endswith('/' + nome): return gun_model.le_ska(self.d, o, s, self.Q, self.T)
        sys.exit('animacao %s nao achada' % nome)

    def local(self, an, i, t60):
        keys = an['ossos'][i]['rot']
        if not keys: return self.L[i]
        if t60 <= keys[0][0]: q = keys[0][1:]
        elif t60 >= keys[-1][0]: q = keys[-1][1:]
        else:
            for a, b in zip(keys, keys[1:]):
                if a[0] <= t60 <= b[0]: q = slerp(a[1:], b[1:], (t60 - a[0]) / max(1e-9, b[0] - a[0])); break
        return gq2m(q)

    def trans(self, an, i, t60):
        """deslocamento aditivo (unidades do Gun) sobre a posicao do osso no esqueleto"""
        keys = an['ossos'][i]['trans']
        if not keys: return (0.0, 0.0, 0.0)
        if t60 <= keys[0][0]: return keys[0][1:]
        for a, b in zip(keys, keys[1:]):
            if a[0] <= t60 <= b[0]:
                f = (t60 - a[0]) / max(1e-9, b[0] - a[0]); return tuple(x + (y - x) * f for x, y in zip(a[1:], b[1:]))
        return keys[-1][1:]

    def world(self, an, t60, pos=False):
        W = [None] * self.n; P = [[0.0, 0.0, 0.0] for _ in range(self.n)]
        for i in range(self.n):
            l = self.local(an, i, t60); p = self.par[i]
            W[i] = l if p < 0 or p == i else mm(W[p], l)
            if pos and p >= 0 and p != i:
                loc = [a + b for a, b in zip(self.loc[i], self.trans(an, i, t60))]
                P[i] = [a + sum(W[p][r][k] * loc[k] for k in range(3)) for r, a in enumerate(P[p])]
        return (W, P) if pos else W

    def ground_speed(self, an, hooves=(16, 21, 30, 34), n=120):
        """velocidade (unidades do Gun por segundo) com que o casco anda para tras enquanto esta apoiado (y perto do seu minimo)"""
        dt = an['duracao'] / n; tr = {h: [] for h in hooves}
        for s in range(n + 1):
            _, P = self.world(an, an['duracao'] * 60 * s / n, True)
            for h in hooves: tr[h].append(P[h])
        v = []
        for h in hooves:
            ymin = min(p[1] for p in tr[h])
            v += [-(b[2] - a[2]) / dt for a, b in zip(tr[h], tr[h][1:]) if a[1] < ymin + 1.5 and b[1] < ymin + 1.5]
        return sum(v) / len(v) if v else 0.0

# ---------------------------------------------------------------- cavalo do SA
class SARig:
    """cavalo.dff + pose parada (primeiro quadro de horse_idle do IFP original). Matrizes em convencao coluna."""
    def __init__(self, dff, ref_seqs):
        self.m = m = G.read_model(dff)
        self.frames = len(m.parent)
        self.bone_frame = m.frame_of_bone
        ref = {bid: keys[0] for bid, typ, keys in ref_seqs}
        self.ref_q = {bid: k[1] for bid, k in ref.items()}
        self.ref_p = {bid: k[2] for bid, k in ref.items() if k[2] is not None}
        self.loc_col = [None] * self.frames; self.pos = [list(p) for p in m.pos]
        for f in range(self.frames):
            bid = m.bone_of_frame[f]
            self.loc_col[f] = row2col(G.q2m(self.ref_q[bid])) if bid in self.ref_q else row2col(m.rot[f])
            if bid in self.ref_p: self.pos[f] = list(self.ref_p[bid])
        self.W0, self.P0 = self.fk(self.loc_col)

    def fk(self, loc):
        W, P = {}, {}
        for f in G.children_order(self.m):
            p = self.m.parent[f]
            if p < 0 or p == f: W[f] = loc[f]; P[f] = list(self.pos[f])
            else:
                W[f] = mm(W[p], loc[f])
                P[f] = [a + b for a, b in zip(P[p], [sum(W[p][i][k] * self.pos[f][k] for k in range(3)) for i in range(3)])]
        return W, P

# SA -> osso do Gun (id do bone tag do SA -> indice do osso no .ske do Gun)
MAP = {1: 22, 2: 1, 3: 5, 4: 7, 5: 8,
       31: 13, 32: 14, 33: 15, 34: 16, 21: 18, 22: 19, 23: 20, 24: 21,
       41: 31, 42: 32, 43: 33, 44: 34, 51: 27, 52: 28, 53: 29, 54: 30, 302: 23}

# animacao do SA (nome no IFP) <- (animacao do Gun, duracao alvo em s ou None para manter a do Gun)
# Os ciclos ficam com a duracao dos do cavaleiro (sit_walk 0.93, sit_run 0.63, sit_galop 0.57: o jogo toca cavalo e cavaleiro na mesma velocidade).
# run <- trote e sprint <- canter: a velocidade do solo medida bate com as constantes do jogo (4.7 e 7.5 m/s); o galope do Gun cobre 6 m por
# ciclo e seria rapido demais. kick_forward e usado como coice parado e no pulo (1.03 s): o Gun nao tem coice de trás, so empinar com as patas da
# frente (trample_fwd), que tambem serve de pulo (patas da frente erguidas); troque aqui por horse_jump.ska se preferir um salto de verdade.
CLIPS = {'horse_idle': ('horse_standidle.ska', None), 'horse_idle_1': ('horse_friskyidle.ska', None), 'horse_walk': ('horse_walk.ska', None),
         'horse_run': ('horse_trot.ska', None), 'horse_sprint': ('horse_gallop.ska', None), 'horse_kick_forward': ('horse_trample_fwd.ska', None)}
FADE = {'horse_kick_forward': (0.15, 0.25)}      # o empinar entra e sai da pose parada em vez de dar tranco
# animacao nova (nao existe no IFP original). O pulo do Gun (2,2 s) decola em ~0,30 s, pousa em ~0,82 s e depois ainda estica o corpo: usa 0,30-1,10 s,
# com o voo esticado para os ~0,7 s do arco do jogo (vz 5,6 e g 16) e a recuperacao em 0,35 s
EXTRA = {'horse_jump': ('horse_jump.ska', None, [(0.0, 0.30), (0.70, 0.82), (1.05, 1.10)])}

def dm(a, b): return [[sum(a[i][k] * b[j][k] for k in range(3)) for j in range(3)] for i in range(3)]   # a x b^T

def retarget(gun, sa, an, ref, t60):
    """matrizes locais (coluna) por frame do SA e translacao extra da raiz (m, eixos do SA) no instante t60 do Gun."""
    Wg, Wr = gun.world(an, t60), ref
    Wn, loc = {}, {}
    for f in G.children_order(sa.m):
        bid = sa.m.bone_of_frame[f]; p = sa.m.parent[f]
        if bid in MAP:
            Dg = dm(Wg[MAP[bid]], Wr[MAP[bid]])                       # W_g(t) x W_ref^T: mudanca de orientacao no mundo do Gun
            Ds = mm(mm(C, Dg), tp(C))
            Wn[f] = mm(Ds, sa.W0[f])
        elif p < 0 or p == f: Wn[f] = sa.loc_col[f]
        else: Wn[f] = mm(Wn[p], sa.loc_col[f])
        loc[f] = Wn[f] if p < 0 or p == f else mm(tp(Wn[p]), Wn[f])
    return loc

def build_native(gun, seqs0, name, gun_name, target, warp=None, fade=None):
    """modelo novo (gun_horse_dff.py): as rotacoes locais do Gun valem como estao (a raiz so troca de eixos); posicoes em metros.
    warp = [(tempo de saida, tempo no .ska)] em s, linear entre os pontos: recorta e estica o trecho que interessa (pulo).
    fade = (entrada, saida) em fracao da duracao: mistura com a pose parada no comeco e no fim, para a animacao nao dar tranco ao entrar e sair."""
    an = gun.anim(gun_name); dur = an['duracao']; scale = target / dur if target else 1.0
    if warp:
        outdur = warp[-1][0]; n = max(8, int(round(outdur * 30)))
        def src(t):
            for (o0, a0), (o1, a1) in zip(warp, warp[1:]):
                if o0 <= t <= o1: return a0 + (a1 - a0) * (t - o0) / (o1 - o0)
            return warp[-1][1]
        times = [(k / n * outdur, src(k / n * outdur) * 60) for k in range(n + 1)]
    else:
        n = max(8, int(round(dur * 30))); times = [(dur * scale * k / n, dur * 60 * k / n) for k in range(n + 1)]
    to_gun = {sa: g for g, sa in SA_ID.items()}; org = origin(gun)
    idle = gun.anim('horse_standidle.ska'); outdur = times[-1][0]
    def key(a, g, t60):
        Rl = gun.local(a, g, t60); tr = gun.trans(a, g, t60)
        loc = [(x + y) * K for x, y in zip(gun.loc[g], tr)]
        if g == 0: Rl = mm(C, Rl); loc = [sum(C[r][c] * tr[c] * K for c in range(3)) + org[r] for r in range(3)]
        return G.m2q(tp(Rl)), loc
    out = {bid: [] for bid, _, _ in seqs0}; prev = {}
    for tout, t60 in times:
        w = 1.0
        if fade:
            u = tout / outdur; w = max(0.0, min(1.0, u / fade[0], (1 - u) / fade[1])); w = w * w * (3 - 2 * w)
        for bid in out:
            g = to_gun[bid]
            q, loc = key(an, g, t60)
            if w < 1.0:
                q0, loc0 = key(idle, g, 0)
                q = slerp(q0, q, w); loc = [a + (b - a) * w for a, b in zip(loc0, loc)]
            if bid in prev and sum(a * b for a, b in zip(q, prev[bid])) < 0: q = tuple(-c for c in q)
            prev[bid] = q
            out[bid].append((q, tout, tuple(loc)))
    return anim_bytes(name, [(bid, out[bid]) for bid in out]), scale

def build(gun, sa, seqs0, name, gun_name, target):
    an = gun.anim(gun_name); ref = gun.world(gun.anim('horse_standidle.ska'), 0)
    dur = an['duracao']; scale = target / dur if target else 1.0
    n = max(8, int(round(dur * 30)))
    times = [dur * 60 * k / n for k in range(n + 1)]
    bones = [bid for bid, _, _ in seqs0]
    out = {bid: [] for bid in bones}; prev = {}; frames = []
    for t60 in times:
        loc = retarget(gun, sa, an, ref, t60)
        frames.append(loc)
        for bid in bones:
            f = sa.bone_frame[bid]
            q = G.m2q(tp(loc[f]))
            if bid in prev and sum(a * b for a, b in zip(q, prev[bid])) < 0: q = tuple(-c for c in q)
            prev[bid] = q
            p = tuple(sa.pos[f])
            out[bid].append((q, t60 / 60 * scale, p))
    return anim_bytes(name, [(bid, out[bid]) for bid in bones]), frames, times, scale

def scale_gun_to_sa(gun, sa, ref_an):
    """metros do SA por unidade do Gun: altura do quadril ao casco nos dois modelos parados"""
    _, P = gun.world(ref_an, 0, True)
    gun_h = P[22][1] - min(P[16][1], P[34][1])
    return (sa.P0[sa.bone_frame[1]][2] - sa.P0[sa.bone_frame[43]][2]) / gun_h

def preview(sa, frames, path, cols=8, cell=170):
    """malha do SA com o skin, vista lateral (y para frente, z para cima), em `cols` instantes do ciclo"""
    W = cols * cell; px = bytearray([245] * (W * cell * 3))
    pick = [frames[int(len(frames) * c / cols)] for c in range(cols)]
    rig = type('R', (), {'m': sa.m})()
    for c, loc in enumerate(pick):
        rot = [None] * sa.frames
        for f in range(sa.frames): rot[f] = tp(loc[f]) if f in loc else sa.m.rot[f]
        pts = G.skin(rig, rot, [list(p) for p in sa.pos])
        ox, oy = c * cell + cell // 2, cell - 30
        for p in pts:
            x, y = int(ox + p[1] * 70), int(oy - (p[2] + 0.93) * 70)
            if 0 <= x < W - 1 and 0 <= y < cell - 1:
                for dx in (0, 1):
                    for dy in (0, 1): i = ((y + dy) * W + x + dx) * 3; px[i:i + 3] = bytes((40, 40, 40))
    G.png(path, W, cell, px)

def main():
    native = '--native' in sys.argv      # modelo novo do gun_horse_dff.py: sem retarget, sem cavalo.dff
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    pak, ifp, out = args[0], args[1], args[-1]
    prev_dir = sys.argv[sys.argv.index('--preview') + 1] if '--preview' in sys.argv else None
    block, anims = read_ifp(ifp)
    gun = GunRig(pak)
    if native: k = K
    else:
        sa = SARig(args[2], next(s for n, s, _ in anims if n.lower() == 'horse_idle'))
        k = scale_gun_to_sa(gun, sa, gun.anim('horse_standidle.ska'))
    print('escala SA/Gun: %.4f m por unidade' % k)
    result = []
    for name, seqs, raw in anims:
        key = name.lower()
        if key in CLIPS:
            gname, target = CLIPS[key]
            if native: data, scale = build_native(gun, seqs, name, gname, target, fade=FADE.get(key)); dur = gun.anim(gname)['duracao'] * scale
            else:
                data, frames, times, scale = build(gun, sa, seqs, name, gname, target); dur = times[-1] / 60 * scale
                if prev_dir: os.makedirs(prev_dir, exist_ok=True); preview(sa, frames, os.path.join(prev_dir, key + '.png'))
            gs = gun.ground_speed(gun.anim(gname)) * k / scale
            print('%-18s <- %-22s %.2f s -> %.2f s%s' % (name, gname, gun.anim(gname)['duracao'], dur, ', velocidade do solo %.2f m/s' % gs if gs > 0.3 else ''))
            result.append((name, data))
        else: result.append((name, raw))
    if native:
        tmpl = next(s for n, s, _ in anims if n.lower() == 'horse_idle')
        for key, (gname, target, warp) in EXTRA.items():
            if key not in [n.lower() for n, _ in result]:
                result.append((key, build_native(gun, tmpl, key, gname, target, warp)[0]))
                print('%-18s <- %-22s (animacao nova, %.2f s)' % (key, gname, warp[-1][0]))
    write_ifp(out, block, result)
    check(ifp, out)
    print('escrito', out, os.path.getsize(out), 'bytes (conferido: mesmas animacoes, as do cavaleiro intactas, ciclos fechando, sem NaN)')

def check(orig, out):
    """le o IFP escrito de volta e confere o que nao pode quebrar o jogo"""
    _, a0 = read_ifp(orig); _, a1 = read_ifp(out)
    assert [n.lower() for n, _, _ in a0] + [k for k in EXTRA if '--native' in sys.argv] == [n.lower() for n, _, _ in a1], 'lista de animacoes mudou'
    for (n, s0, r0), (_, s1, r1) in zip(a0, a1):
        if n.lower() in CLIPS:
            assert [b for b, _, _ in s0] == [b for b, _, _ in s1], '%s: ossos diferentes' % n
            for _, _, keys in s1:
                assert all(not any(math.isnan(c) for c in q + (t,)) for t, q, _ in keys), '%s: NaN' % n
                assert all(abs(sum(c * c for c in q) - 1) < 1e-3 for _, q, _ in keys), '%s: quaternio nao unitario' % n
                if n.lower() not in ('horse_kick_forward',):
                    assert abs(sum(a * b for a, b in zip(keys[0][1], keys[-1][1]))) > 0.999, '%s: ciclo nao fecha' % n
        else: assert r0 == r1, '%s: animacao do cavaleiro alterada' % n

if __name__ == '__main__':
    main()
