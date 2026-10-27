r"""Despeja a imagem de um .exe ja desempacotado em memoria e remonta um PE que o Ghidra abre. So le o processo (ReadProcessMemory).

Uso: python dump_pe.py <pid ou nome.exe> <saida.exe>

Cada secao e gravada onde esta na memoria (PointerToRawData = VirtualAddress, FileAlignment = SectionAlignment). A tabela de importacao e o
ponto de entrada ficam como estavam na memoria: se o empacotador os apagou, a IAT nao aparece e o OEP precisa ser achado na mao.
"""
import ctypes, struct, subprocess, sys
from ctypes import wintypes as w

k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = w.HANDLE
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]

def pid_de(a):
    if a.isdigit(): return int(a)
    out = subprocess.run(['tasklist', '/fi', 'imagename eq ' + a, '/fo', 'csv', '/nh'], capture_output=True, text=True).stdout
    return int(out.split(',')[1].strip('"'))

def main():
    if len(sys.argv) != 3: sys.exit(__doc__)
    h = k.OpenProcess(0x0410, False, pid_de(sys.argv[1]))   # QUERY_INFORMATION | VM_READ
    if not h: sys.exit('OpenProcess falhou: %d' % ctypes.get_last_error())
    def rd(a, n):
        b = ctypes.create_string_buffer(n); r = ctypes.c_size_t()
        k.ReadProcessMemory(h, a, b, n, ctypes.byref(r))
        return b.raw[:r.value] + b'\0' * (n - r.value)
    base = 0x400000
    hdr = bytearray(rd(base, 0x1000))
    pe = struct.unpack_from('<I', hdr, 0x3c)[0]
    if hdr[pe:pe + 4] != b'PE\0\0': sys.exit('cabecalho PE invalido na memoria')
    ns, so = struct.unpack_from('<H', hdr, pe + 6)[0], struct.unpack_from('<H', hdr, pe + 20)[0]
    opt = pe + 24
    size = struct.unpack_from('<I', hdr, opt + 56)[0]
    struct.pack_into('<I', hdr, opt + 36, 0x1000)               # FileAlignment = SectionAlignment (0x1000)
    img = bytearray(size)
    img[:0x1000] = hdr
    for i in range(ns):
        o = pe + 24 + so + i * 40
        vs, va = struct.unpack_from('<II', hdr, o + 8)
        n = (max(vs, 1) + 0xfff) & ~0xfff
        struct.pack_into('<II', hdr, o + 16, n, va)             # SizeOfRawData, PointerToRawData
        img[va:va + n] = rd(base + va, n)
        print('%-8s va %06x tamanho %06x' % (hdr[o:o + 8].rstrip(b'\0').decode(), va, n))
    img[:0x1000] = hdr
    open(sys.argv[2], 'wb').write(img)
    print('entrada', hex(base + struct.unpack_from('<I', hdr, opt + 16)[0]), '->', sys.argv[2], len(img), 'bytes')

if __name__ == '__main__':
    main()
