r"""Le as texturas dos blocos .tex (tipo stex, 0x2b0a3095) do Gun e as grava como TXD do SA (D3D9, DXT1/DXT5 sem recodificar).

Uso: python gun_tex.py <pak> <parte do nome, ex. horse.tex> [saida.txd nome_da_textura] [indice]

O bloco comeca com o cabecalho 0xfacecaa2 e uma tabela de descritores de 0x28 bytes (marca 0x00280006): hash da textura, largura e altura
(u16, u16), formato (byte 0 = niveis de mipmap, byte 3 = 1 DXT1 / 5 DXT5), e no ultimo dword o offset dos dados no bloco. Os dados sao blocos
DXT lineares (como no PC), todos os mipmaps em sequencia. Em horse.tex as 3 texturas sao normal, difusa (DXT1) e especular.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gun_pak, gun_model

def descritores(b):
    out = []
    for i in range(0, min(len(b), 0x400) - 40, 4):
        if struct.unpack_from('<I', b, i)[0] == 0x00280006 and struct.unpack_from('<I', b, i + 28)[0] == 0xffffffff:
            h, wh, _, fmt, _, _, _, _, off = struct.unpack_from('<IIIIIIIII', b, i + 4)
            out.append({'hash': h, 'w': wh & 0xffff, 'h': wh >> 16, 'mips': fmt & 0xff, 'kind': fmt >> 24, 'off': off})
    for d, n in zip(out, out[1:] + [{'off': len(b)}]): d['size'] = n['off'] - d['off']
    return out

def mip_sizes(w, h, mips, kind):
    bs = 8 if kind == 1 else 16
    return [max(1, (w >> m) + 3 >> 2) * max(1, (h >> m) + 3 >> 2) * bs for m in range(mips)]

def le_tex(pak, trecho):
    es, dados = gun_pak.le_pak(pak); tab = gun_pak.nomes(gun_pak.raiz(pak))
    r = [(o, s) for t, o, s, h in es if t == 0x2b0a3095 and (tab.get(h) or '').replace('\\', '/').endswith(trecho)]
    if len(r) != 1: sys.exit('%d texturas casam com %r' % (len(r), trecho))
    b = dados[r[0][0]:r[0][0] + r[0][1]]
    ds = descritores(b)
    for d in ds:
        sz = mip_sizes(d['w'], d['h'], d['mips'], d['kind'])
        assert sum(sz) <= d['size'], 'dados menores que os mipmaps de %08x' % d['hash']
        d['levels'] = [b[d['off'] + sum(sz[:k]):d['off'] + sum(sz[:k + 1])] for k in range(d['mips'])]
    return ds

def txd(textures):
    """textures: [(nome, descritor)] -> bytes de um TXD D3D9"""
    def chunk(t, p): return struct.pack('<III', t, len(p), 0x1803ffff) + p
    tex = b''
    for nome, d in textures:
        dxt5 = d['kind'] == 5
        body = struct.pack('<II', 9, 0x1106) + nome.encode().ljust(32, b'\0') + b'\0' * 32
        body += struct.pack('<II', 0x8300 if dxt5 else 0x8200, 0x35545844 if dxt5 else 0x31545844)
        body += struct.pack('<HHBBBB', d['w'], d['h'], 32 if dxt5 else 16, d['mips'], 4, 0x09 if dxt5 else 0x08)
        for lv in d['levels']: body += struct.pack('<I', len(lv)) + lv
        tex += chunk(0x15, chunk(0x01, body) + chunk(0x03, b''))
    return chunk(0x16, chunk(0x01, struct.pack('<HH', len(textures), 2)) + tex + chunk(0x03, b''))

if __name__ == '__main__':
    pak, trecho = sys.argv[1:3]
    ds = le_tex(pak, trecho)
    for i, d in enumerate(ds): print(i, '%08x' % d['hash'], '%dx%d' % (d['w'], d['h']), 'mips', d['mips'], 'DXT%d' % d['kind'], 'off 0x%x' % d['off'], 'bytes', d['size'])
    if len(sys.argv) > 4:
        open(sys.argv[3], 'wb').write(txd([(sys.argv[4], ds[int(sys.argv[5]) if len(sys.argv) > 5 else 0])]))
