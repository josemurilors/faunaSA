r"""Troca as animacoes do cavaleiro (CJ na sela) do horse_animation.ifp pelas do cavaleiro do Gun.

Uso: python gun_rider_ifp.py <global.pak.xbx> <horse_animation.ifp ORIGINAL> <ifp de trabalho (entra e sai)>

Retarget do esqueleto humano do Gun (47 ossos) para o CJ (tools/cj_skel.dff, so o esqueleto do player.dff), com a pose ABSOLUTA do Gun:
  1. orientacao: delta de cada osso em relacao a pose de bind do Gun, aplicado ao bind do CJ (eixos Gun->SA);
  2. correcao de direcao: o CJ tem proporcoes/bind diferentes, entao cada segmento (osso -> filho mapeado) e girado para apontar para onde o do Gun
     aponta (a torcao vem do Gun) - e o que faz a inclinacao do tronco no trote/galope sair igual;
  3. posicao: a raiz do CJ e calculada para os quadris dele (media das coxas) ficarem onde ficam os do Gun em relacao ao cavalo (a sela do
     modelo novo e a do Gun, as animacoes do cavaleiro do Gun sao escritas no referencial do cavalo). Posicao constante por clip (media do
     ciclo): o balanco vertical vem do osso da sela em horse.hpp (PlaceRider soma spine.z).
Montar e desmontar ficam as originais (so a raiz sobe SEAT_DY).
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_ifp as G
import gun_to_ifp as X

HERE = os.path.dirname(os.path.abspath(__file__))
# osso do CJ -> osso do Gun (indice no .ske 'gun'); ossos fora daqui (raiz, sobrancelhas) ficam como no `sit`
MAP = {1: 1, 2: 2, 3: 4, 4: 31, 5: 32, 8: 33, 31: 5, 32: 6, 33: 7, 34: 9, 35: 10, 36: 11, 21: 18, 22: 19, 23: 20, 24: 22, 25: 25, 26: 26,
       41: 40, 42: 41, 43: 42, 44: 43, 51: 36, 52: 37, 53: 38, 54: 39}
# animacao do SA -> (grupo e animacao do Gun); duracao = a do Gun (a mesma das animacoes do cavalo)
CLIPS = {'sit': ('h_idles', 'gun_h_standidle.ska'), 'sit_walk': ('h_walk', 'gun_h_walk.ska'), 'sit_run': ('h_walk', 'gun_h_trot.ska'),
         'sit_galop': ('h_walk', 'gun_h_gallop.ska')}
PLACE_DX, PLACE_DZ = 0.05, 0.05      # horse.hpp: PlaceRider(dx=-0.05) e +0.05 em z; a raiz da animacao compensa
SEAT_DY = 0.326      # `sit` original leva a raiz 0,326 m para tras; montar/sair originais sobem isso para a sela nova (na origem)
LOWER = {0, 1, 41, 42, 43, 44, 51, 52, 53, 54}
MIN_SEG = 0.03       # segmentos mais curtos que isso (m) nao dao direcao confiavel

def mv(m, v): return [sum(m[i][k] * v[k] for k in range(3)) for i in range(3)]

def swing(a, b):
    """rotacao minima que leva o vetor a ao b"""
    la, lb = math.sqrt(sum(c * c for c in a)), math.sqrt(sum(c * c for c in b))
    a, b = [c / la for c in a], [c / lb for c in b]
    c = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    d = sum(x * y for x, y in zip(a, b))
    if d < -0.9999: return [[1, 0, 0], [0, 1, 0], [0, 0, 1]]      # oposto: ambiguo, nao corrige
    K_ = [[0, -c[2], c[1]], [c[2], 0, -c[0]], [-c[1], c[0], 0]]
    K2 = X.mm(K_, K_)
    return [[(i == j) + K_[i][j] + K2[i][j] / (1 + d) for j in range(3)] for i in range(3)]

class Rider:
    def __init__(self, pak, sit):
        self.sa = sa = X.SARig(os.path.join(HERE, 'cj_skel.dff'), sit)
        self.m = m = sa.m
        self.gun = g = X.GunRig(pak, 'gun')
        self.org = X.origin(X.GunRig(pak))
        self.order = G.children_order(m)
        f0 = sa.bone_frame[0]
        loc = [sa.loc_col[f] if f == f0 else X.row2col(m.rot[f]) for f in range(sa.frames)]   # bind do CJ com a raiz da animacao
        self.Wb, _ = sa.fk(loc)
        self.Gb = [None] * g.n                                                                 # bind do Gun (mundo)
        for i in range(g.n): self.Gb[i] = g.L[i] if g.par[i] < 0 or g.par[i] == i else X.mm(self.Gb[g.par[i]], g.L[i])
        self.child = {}      # filho de referencia de cada osso mapeado do CJ: o mais longe entre os mapeados
        for f in range(sa.frames):
            bid = m.bone_of_frame[f]
            if bid not in MAP: continue
            kids = [(math.sqrt(sum(c * c for c in m.pos[c_])), c_, m.bone_of_frame[c_]) for c_ in range(sa.frames)
                    if m.parent[c_] == f and c_ != f and m.bone_of_frame[c_] in MAP]
            kids = [k for k in kids if k[0] > MIN_SEG]
            if kids: self.child[bid] = max(kids)[1:]

    def sa_point(self, p): return [sum(X.C[i][k] * p[k] for k in range(3)) * X.K + self.org[i] for i in range(3)]

    def pose(self, an, t60):
        """-> (rotacao local por frame do CJ, deslocamento da raiz que poe os quadris no lugar dos do Gun)"""
        sa, m, g = self.sa, self.m, self.gun
        Wg, Pg = g.world(an, t60, True)
        Wn, loc, P = {}, {}, {}
        for f in self.order:
            bid = m.bone_of_frame[f]; p = m.parent[f]
            if bid in MAP:
                gi = MAP[bid]
                Dg = X.mm(Wg[gi], X.tp(self.Gb[gi]))
                w = X.mm(X.mm(X.mm(X.C, Dg), X.tp(X.C)), self.Wb[f])
                if bid in self.child:
                    cf, cb = self.child[bid]
                    vg = mv(X.C, [Pg[MAP[cb]][k] - Pg[gi][k] for k in range(3)])
                    w = X.mm(swing(mv(w, m.pos[cf]), vg), w)
                Wn[f] = w
            elif p < 0 or p == f: Wn[f] = sa.loc_col[f]
            else: Wn[f] = X.mm(Wn[p], sa.loc_col[f])
            loc[f] = Wn[f] if p < 0 or p == f else X.mm(X.tp(Wn[p]), Wn[f])
            P[f] = [0.0, 0.0, 0.0] if p < 0 or p == f else [a + b for a, b in zip(P[p], mv(Wn[p], m.pos[f]))]
        hip_cj = [(P[sa.bone_frame[41]][k] + P[sa.bone_frame[51]][k]) / 2 for k in range(3)]
        hip_g = self.sa_point([(Pg[40][k] + Pg[36][k]) / 2 for k in range(3)])
        return loc, [hip_g[k] - hip_cj[k] for k in range(3)]

    def build(self, orig_seqs, gname, grupo):
        an = self.gun.anim(gname, grupo); dur = an['duracao']
        n = max(8, int(round(dur * 30)))
        bones = [bid for bid, _, _ in orig_seqs]; otype = {bid: t for bid, t, _ in orig_seqs}
        frames = [self.pose(an, dur * 60 * k / n) for k in range(n + 1)]
        mean = [sum(f[1][i] for f in frames[:-1]) / n for i in range(3)]
        root = (mean[0] + PLACE_DX, mean[1], mean[2] - PLACE_DZ)
        out = {bid: [] for bid in bones}; prev = {}
        for k, (loc, _) in enumerate(frames):
            t = dur * k / n
            for bid in bones:
                q = G.m2q(X.tp(loc[self.sa.bone_frame[bid]]))
                if bid in prev and sum(a * b for a, b in zip(q, prev[bid])) < 0: q = tuple(-c for c in q)
                prev[bid] = q
                out[bid].append((q, t, root if bid == 0 else None))
        return [(bid, otype[bid], out[bid]) for bid in bones]

def shifted(seqs):
    """animacao original com a raiz levada SEAT_DY para frente (a sela nova fica na origem do cavalo)"""
    return [(bid, typ, [(q, t, p and (p[0], p[1] + SEAT_DY, p[2])) for t, q, p in keys]) for bid, typ, keys in seqs]

def legs(name, seqs):
    """so raiz, pelve e pernas: tocado como parcial por cima da base de mira, o tronco fica livre para mirar como no Gun"""
    return X.anim_bytes_c('sitl' + name[3:], [s for s in seqs if s[0] in LOWER])

def main():
    pak, orig_ifp, work = sys.argv[1:4]
    _, orig = X.read_ifp(orig_ifp)
    sit = next(s for n, s, _ in orig if n.lower() == 'sit')
    odict = {n.lower(): s for n, s, _ in orig}
    rider = Rider(pak, sit)
    _, cur = X.read_ifp(work)
    result = []; lower = []
    for name, seqs, raw in cur:
        key = name.lower()
        if key.startswith('sitl'): continue                      # refeitas abaixo
        if key in CLIPS:
            grupo, gname = CLIPS[key]
            s = rider.build(odict[key], gname, grupo)
            result.append((name, X.anim_bytes_c(name, s))); lower.append(legs(name, s))
            print('%-10s <- %-20s %.2f s  raiz %s' % (name, gname, rider.gun.anim(gname, grupo)['duracao'], [round(c, 3) for c in s[0][2][0][2]]))
        elif key.startswith(('jump_on', 'exit_')):
            result.append((name, X.anim_bytes_c(name, shifted(odict[key]))))
        else: result.append((name, raw))
    for b in lower: result.append((b[:24].split(b'\0')[0].decode(), b))
    X.write_ifp(work, X.read_ifp(work)[0], result)
    _, a1 = X.read_ifp(work)
    for n, s, _ in a1:
        if n.lower() in CLIPS: assert all(not any(math.isnan(c) for c in q) for _, _, ks in s for _, q, _ in ks), n + ': NaN'
    print('escrito', work, os.path.getsize(work), 'bytes')

if __name__ == '__main__':
    main()
