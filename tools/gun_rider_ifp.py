r"""Troca as animacoes do cavaleiro (CJ na sela) do horse_animation.ifp pelas do cavaleiro do Gun.

Uso: python gun_rider_ifp.py <global.pak.xbx> <horse_animation.ifp ORIGINAL> <cavalo.dff ORIGINAL> <ifp de trabalho (entra e sai)>

Retarget por orientacao no mundo, como em gun_to_ifp.py, com o esqueleto humano do Gun (47 ossos) para o esqueleto de ped do CJ: a pose parada de
referencia do CJ e a do `sit` original, e a do Gun e a media do ciclo de andar do cavaleiro; cada osso recebe a mudanca de orientacao do Gun em
relacao a essa media. A raiz (posicao e rotacao) fica a do original: ela leva o CJ ate a sela e o codigo do jogo cuida do balanco pelo osso da sela.
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_ifp as G
import gun_to_ifp as X

# osso do CJ -> osso do Gun (indice no .ske 'gun'); ossos fora daqui (raiz, sobrancelhas, peito/barriga) ficam como no `sit`
MAP = {1: 1, 2: 2, 3: 4, 4: 31, 5: 32, 8: 33, 31: 5, 32: 6, 33: 7, 34: 9, 35: 10, 36: 11, 21: 18, 22: 19, 23: 20, 24: 22, 25: 25, 26: 26,
       41: 40, 42: 41, 43: 42, 44: 43, 51: 36, 52: 37, 53: 38, 54: 39}
# animacao do SA -> (grupo e animacao do Gun, duracao em s)
# duracao = a do Gun (a mesma das animacoes do cavalo). Montar e desmontar ficam as originais: o caminho da raiz delas nao combina com o do Gun.
CLIPS = {'sit_walk': ('h_walk', 'gun_h_walk.ska', None), 'sit_run': ('h_walk', 'gun_h_trot.ska', None), 'sit_galop': ('h_walk', 'gun_h_canter.ska', None)}

SEAT_DY = 0.326   # o `sit` original leva a raiz do CJ 0,326 m para tras da origem do ped; a sela nova fica na origem
LOWER = {0, 1, 41, 42, 43, 44, 51, 52, 53, 54}

def mean_pose(gun, an, n=24):
    """orientacao mundial media de cada osso ao longo do ciclo (quaternios alinhados, normalizados)"""
    acc = None
    for k in range(n):
        W = gun.world(an, an['duracao'] * 60 * k / n)
        qs = [G.m2q(X.tp(w)) for w in W]
        if acc is None: acc = [list(q) for q in qs]; continue
        for a, q in zip(acc, qs):
            if sum(x * y for x, y in zip(a, q)) < 0: q = tuple(-c for c in q)
            for i in range(4): a[i] += q[i]
    return [X.tp(G.q2m(X.qnorm(tuple(a)))) for a in acc]

def orig_pos(keys, t):
    """posicao da raiz do original no tempo t (interpolada)"""
    ks = [(k[0], k[2]) for k in keys if k[2] is not None]
    if not ks: return None
    if t <= ks[0][0]: return ks[0][1]
    for (t0, p0), (t1, p1) in zip(ks, ks[1:]):
        if t0 <= t <= t1: f = (t - t0) / max(1e-9, t1 - t0); return tuple(a + (b - a) * f for a, b in zip(p0, p1))
    return ks[-1][1]

def build(gun, sa, orig_seqs, ref, name, grupo, gname, target):
    an = gun.anim(gname, grupo); dur = an['duracao']; scale = target / dur if target else 1.0
    n = max(8, int(round(dur * 30)))
    bones = [bid for bid, _, _ in orig_seqs]; okeys = {bid: k for bid, _, k in orig_seqs}; otype = {bid: t for bid, t, _ in orig_seqs}
    out = {bid: [] for bid in bones}; prev = {}
    odur = max(k[0] for k in okeys[0])
    for k in range(n + 1):
        t60 = dur * 60 * k / n
        Wg = gun.world(an, t60); Wn, loc = {}, {}
        for f in G.children_order(sa.m):
            bid = sa.m.bone_of_frame[f]; p = sa.m.parent[f]
            if bid in MAP:
                g = MAP[bid]; Dg = X.mm(Wg[g], X.tp(ref[g]))
                Ds = X.mm(X.mm(X.C, Dg), X.tp(X.C))
                Wn[f] = X.mm(Ds, sa.W0[f])
            elif p < 0 or p == f: Wn[f] = sa.loc_col[f]
            else: Wn[f] = X.mm(Wn[p], sa.loc_col[f])
            loc[f] = Wn[f] if p < 0 or p == f else X.mm(X.tp(Wn[p]), Wn[f])
        t = t60 / 60 * scale
        for bid in bones:
            f = sa.bone_frame[bid]
            q = G.m2q(X.tp(loc[f]))
            if bid in prev and sum(a * b for a, b in zip(q, prev[bid])) < 0: q = tuple(-c for c in q)
            prev[bid] = q
            p = orig_pos(okeys[bid], t / (dur * scale) * odur)    # so a raiz tem posicao: os ossos do CJ ficam nas posicoes do proprio CJ
            out[bid].append((q, t, p and (p[0], p[1] + SEAT_DY, p[2])))
    return [(bid, otype[bid], out[bid]) for bid in bones]

def shifted(seqs):
    """animacao original do cavaleiro com a raiz levada SEAT_DY para frente (a sela agora fica na origem do cavalo)"""
    return [(bid, typ, [(q, t, p and (p[0], p[1] + SEAT_DY, p[2])) for t, q, p in keys]) for bid, typ, keys in seqs]

def legs(name, seqs):
    """so raiz, pelve e pernas: o jogo toca como parcial por cima da base de mira, e o tronco fica livre para mirar como no Gun"""
    return X.anim_bytes_c('sitl' + name[3:], [s for s in seqs if s[0] in LOWER])

def main():
    pak, orig_ifp, dff, work = sys.argv[1:5]
    _, orig = X.read_ifp(orig_ifp)
    _, cur = X.read_ifp(work)
    sit = next(s for n, s, _ in orig if n.lower() == 'sit')
    sa = X.SARig(dff, sit)
    gun = X.GunRig(pak, 'gun')
    ref = mean_pose(gun, gun.anim('gun_h_walk.ska', 'h_walk'))
    odict = {n.lower(): s for n, s, _ in orig}
    result = []; lower = []
    for name, seqs, raw in cur:
        key = name.lower()
        if key.startswith('sitl'): continue                      # refeitas abaixo
        if key in CLIPS:
            grupo, gname, target = CLIPS[key]
            s = build(gun, sa, odict[key], ref, name, grupo, gname, target)
            if key == 'sit_run':
                s = [(bid, typ, [(q, t, (p[0], p[1] + 0.02, p[2] - 0.01) if p is not None else None) for t,q,p in keys]) for bid,typ,keys in s]
            if key == 'sit_galop':
                s = [(bid, typ, [(q, t, (p[0], p[1] + 0.05, p[2] - 0.02) if p is not None else None) for t,q,p in keys]) for bid,typ,keys in s]
            result.append((name, X.anim_bytes_c(name, s))); lower.append(legs(name, s))
            print('%-14s <- %-26s %.2f s' % (name, gname, gun.anim(gname, grupo)['duracao']))
        elif key.startswith(('sit', 'jump_on', 'exit_')):
            s = shifted(odict[key]); result.append((name, X.anim_bytes_c(name, s)))
            if key == 'sit': lower.append(legs(name, s))
        else: result.append((name, raw))
    for b in lower: result.append((b[:24].split(b'\0')[0].decode(), b))
    X.write_ifp(work, X.read_ifp(work)[0], result)
    _, a1 = X.read_ifp(work)
    assert [n for n, _, _ in a1] == [n for n, _ in result], 'lista de animacoes mudou'
    for n, s, _ in a1:
        if n.lower() in CLIPS: assert all(not any(math.isnan(c) for c in q) for _, _, ks in s for _, q, _ in ks), n + ': NaN'
    print('escrito', work, os.path.getsize(work), 'bytes')

if __name__ == '__main__':
    main()
