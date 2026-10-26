"""Compara a pose de repouso REAL (matrizes inversas do skin) de cada modelo com a pose parada de cada grupo de animacao."""
import glob, math, os, struct, sys
sys.path.insert(0, os.path.dirname(__file__))

D = r'D:\Games\GTA San Andreas\cleo\Animals'
dec = lambda x: x.split(b'\0')[0].decode('latin1')

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

def read_model(path):
    b = open(path, 'rb').read()
    p, s = find(b, 0, len(b), 0x0E)
    kids = list(chunks(b, p, p + s))
    n = struct.unpack_from('<I', b, kids[0][3])[0]
    parent, name, hnodes = [], [], []
    for i in range(n):
        parent.append(struct.unpack_from('<i', b, kids[0][3] + 4 + i * 56 + 48)[0])
    exts = [k for k in kids[1:] if k[0] == 0x03]
    bone_of_frame = [-1] * n
    for i, e in enumerate(exts[:n]):
        for t, s2, v, p2 in chunks(b, e[3], e[3] + e[1]):
            if t == 0x11E:
                ver, nid, cnt = struct.unpack_from('<III', b, p2)
                bone_of_frame[i] = nid
                if cnt:
                    fl, ks = struct.unpack_from('<II', b, p2 + 12)
                    for k in range(cnt):
                        node_id, node_idx, nflags = struct.unpack_from('<iii', b, p2 + 20 + k * 12)
                        hnodes.append(node_id)
    # skin
    inv = None
    gl = find(b, 0, len(b), 0x1A)
    if gl:
        gp, gs = gl
        for t, s2, v, p2 in chunks(b, gp, gp + gs):
            if t != 0x0F: continue
            st = next(chunks(b, p2, p2 + s2))
            nverts = struct.unpack_from('<I', b, st[3] + 8)[0]
            for t3, s3, v3, p3 in chunks(b, p2, p2 + s2):
                if t3 != 0x03: continue
                for t4, s4, v4, p4 in chunks(b, p3, p3 + s3):
                    if t4 == 0x116:
                        nb, nu, mw, _ = struct.unpack_from('<4B', b, p4)
                        off = p4 + 4 + nu + nverts * 4 + nverts * 16
                        inv = [struct.unpack_from('<16f', b, off + i * 64) for i in range(nb)]
                        break
            if inv: break
    return {'parent': parent, 'bone_of_frame': bone_of_frame, 'hnodes': hnodes, 'inv': inv}

def T(m): return [[m[j][i] for j in range(3)] for i in range(3)]
def mul(a, b): return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def angle(a, b):
    r = mul(T(a), b); tr = r[0][0] + r[1][1] + r[2][2]
    return math.degrees(math.acos(max(-1.0, min(1.0, (tr - 1) / 2))))

def q2m(q, conj):
    x, y, z, w = q
    if conj: x, y, z = -x, -y, -z
    return [[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
            [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
            [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]]

def idle_pose(ifp):
    b = open(ifp, 'rb').read()
    na = struct.unpack_from('<I', b, 32)[0]; pos = 36
    for _ in range(na):
        name = dec(b[pos:pos + 24]); ns, ds, fl = struct.unpack_from('<III', b, pos + 24); pos += 36
        pose = {}
        for _s in range(ns):
            ft, nf, bt = struct.unpack_from('<III', b, pos + 24)
            fs = {1: 20, 2: 32, 3: 10, 4: 16}[ft]; fp = pos + 36
            if nf:
                q = tuple(c / 4096.0 for c in struct.unpack_from('<4h', b, fp)) if ft in (3, 4) else struct.unpack_from('<4f', b, fp)
                pose[struct.unpack_from('<i', b, pos + 32)[0]] = q
            pos += 36 + nf * fs
        if name.lower().endswith('_idle'): return pose
    return {}

def bind_world(model):
    """bone id -> rotacao mundial de repouso (inversa da matriz inversa do skin)"""
    out = {}
    for i, nid in enumerate(model['hnodes']):
        if model['inv'] and i < len(model['inv']):
            m = model['inv'][i]
            R = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]]
            out[nid] = T(R)
    return out

def anim_world(model, pose, conj, order):
    """compoe as rotacoes locais da animacao pela hierarquia dos frames -> rotacao mundial por bone id"""
    parent, bof = model['parent'], model['bone_of_frame']
    bone_to_frame = {bof[f]: f for f in range(len(bof)) if bof[f] >= 0}
    cache = {}
    def world(f):
        if f in cache: return cache[f]
        bid = bof[f]
        loc = q2m(pose[bid], conj) if bid in pose else [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        p = parent[f]
        if p < 0 or p == f: res = loc
        else: res = mul(loc, world(p)) if order == 0 else mul(world(p), loc)
        cache[f] = res
        return res
    return {bid: world(f) for bid, f in bone_to_frame.items()}

def score(model, pose, conj, order):
    bw, aw = bind_world(model), anim_world(model, pose, conj, order)
    ref = 1  # Pelvis: rotacoes relativas a ele nao dependem de um deslocamento global de convencao
    if ref not in bw or ref not in aw: return 999
    common = [k for k in bw if k in aw and k in pose and k != ref]
    rb = {k: mul(bw[k], T(bw[ref])) for k in common}
    ra = {k: mul(aw[k], T(aw[ref])) for k in common}
    return sum(angle(rb[k], ra[k]) for k in common) / max(1, len(common))

groups = {'Boar': 'Anim37', 'rabit': 'Anim31', 'deer1': 'Anim32', 'lion': 'Anim42', 'wolf': 'Anim52', 'bear': 'Anim57', 'dog': 'Anim68', 'cat': 'Anim73'}
poses = {g: idle_pose(glob.glob(os.path.join(D, f, g + '.ifp'))[0]) for g, f in groups.items()}
targets = {'cow(4)': 'Anim4/A4_1', 'cow(5)': 'Anim5/Anim5', 'ostrich(6)': 'Anim6/Anim6', 'sheep(7)': 'Anim7/Anim7', 'duck(8)': 'Anim8/Anim8',
           'gorilla(9)': 'Anim9/Anim9', 'capybara(10)': 'Anim10/Anim10', 'fox(11)': 'Anim11/Anim11', 'chicken(14)': 'Anim14/A14_1', 'pig?(2)': 'Anim2/A2_1',
           '[rabbit]': 'Anim27/A27_1', '[boar]': 'Anim37/A37_1', '[deer]': 'Anim32/A32_1', '[lion]': 'Anim42/Anim42', '[wolf]': 'Anim52/A52_1',
           '[bear]': 'Anim57/Anim57', '[dog]': 'Anim68/A68_1', '[cat]': 'Anim73/A73_1'}
models = {n: read_model(os.path.join(D, r.split('/')[0], r.split('/')[1] + '.dff')) for n, r in targets.items()}
print('bones com matriz inversa:', {n: (len(m['inv']) if m['inv'] else 0) for n, m in list(models.items())[:3]}, '| hnodes', len(models['[rabbit]']['hnodes']))

# calibra a convencao pelo coelho no proprio grupo (deve dar ~0)
best = min(((score(models['[rabbit]'], poses['rabit'], c, o), c, o) for c in (0, 1) for o in (0, 1)))
print('calibracao (coelho x rabit): erro %.1f graus, conj=%d, ordem=%d' % best)
_, conj, order = best
print('%-14s' % '' + ''.join('%8s' % g for g in groups))
for n, m in models.items():
    print('%-14s' % n + ''.join('%8.1f' % score(m, poses[g], conj, order) for g in groups))
