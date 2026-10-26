// Leitor de IFP ANP3 (formato de animacao do GTA SA) e de grupos do animgrp.dat. Sem dependencias do jogo.
// Layout conferido contra o loader do jogo reimplementado em gta-reversed (AnimManager.cpp, LoadAnimFile_ANP23).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace mm {

struct IfpSeq {
    std::string name;
    uint32_t frameType = 0, numFrames = 0;
    int32_t boneTag = -1;
    const uint8_t* frames = nullptr; // aponta para IfpBlock::raw
    size_t frameBytes = 0;
};

struct IfpAnim {
    std::string name;
    uint32_t flags = 0, dataSize = 0; // flags & 1: a animacao roda comprimida
    std::vector<IfpSeq> seqs;
};

struct IfpBlock {
    std::string name;
    std::vector<IfpAnim> anims;
    std::vector<uint8_t> raw; // nao copiar depois de parse: os ponteiros das sequencias apontam para ca
    std::string error;
};

inline bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Tamanho de um keyframe por tipo: 1 rot, 2 rot+pos, 3 rot comprimida, 4 rot+pos comprimidas.
inline size_t keyFrameSize(uint32_t type) {
    switch (type) { case 1: return 20; case 2: return 32; case 3: return 10; case 4: return 16; default: return 0; }
}

// Tamanho de um keyframe descomprimido (rot float x4 + tempo float [+ pos float x3]).
inline size_t uncompressedKeyFrameSize(uint32_t type) { return (type == 1 || type == 3) ? 20 : 32; }

// Escreve em out (frames * uncompressedKeyFrameSize bytes) os keyframes da sequencia no formato que o jogo usa
// depois de descomprimir: rot = int16/4096, tempo = int16/60 s, pos = int16/1024 (mesma conta do CAnimBlendSequence).
inline void uncompressKeyFrames(const IfpSeq& q, uint8_t* out) {
    if (q.frameType == 1 || q.frameType == 2) { std::memcpy(out, q.frames, q.frameBytes); return; }
    bool trans = q.frameType == 4;
    size_t in = trans ? 16 : 10, o = trans ? 32 : 20;
    for (uint32_t i = 0; i < q.numFrames; ++i) {
        int16_t v[8] = {};
        std::memcpy(v, q.frames + i * in, in);
        float f[8] = { v[0] / 4096.0f, v[1] / 4096.0f, v[2] / 4096.0f, v[3] / 4096.0f, v[4] / 60.0f, v[5] / 1024.0f, v[6] / 1024.0f, v[7] / 1024.0f };
        std::memcpy(out + i * o, f, o);
    }
}

// Zera o deslocamento horizontal da raiz (x,y fixos no do 1o keyframe) em keyframes ja descomprimidos com posicao (32 bytes).
// Sem isso o modelo anda para frente durante o ciclo e volta ao reiniciar o loop ("rollback"); o movimento e feito pela IA.
inline void flattenRootXY(uint8_t* out, uint32_t frameType, uint32_t numFrames) {
    if ((frameType != 2 && frameType != 4) || numFrames < 2) return;
    float x0, y0;
    std::memcpy(&x0, out + 20, 4);
    std::memcpy(&y0, out + 24, 4);
    for (uint32_t i = 1; i < numFrames; ++i) {
        std::memcpy(out + i * 32 + 20, &x0, 4);
        std::memcpy(out + i * 32 + 24, &y0, 4);
    }
}

// Velocidade (m/s) do movimento da raiz da animacao: deslocamento horizontal entre o 1o e o ultimo keyframe da primeira
// sequencia com posicao (a raiz), dividido pela duracao. 0 se a animacao nao tem deslocamento (idle).
inline float rootSpeed(const IfpAnim& a) {
    for (const IfpSeq& q : a.seqs) {
        if (q.frameType != 4 && q.frameType != 2) continue;
        if (q.numFrames < 2) return 0;
        auto pos = [&](uint32_t f, float* xy) {
            if (q.frameType == 4) { int16_t v[8]; std::memcpy(v, q.frames + f * 16, 16); xy[0] = v[5] / 1024.0f; xy[1] = v[6] / 1024.0f; }
            else { float v[8]; std::memcpy(v, q.frames + f * 32, 32); xy[0] = v[5]; xy[1] = v[6]; }
        };
        float p0[2], p1[2], dur = 0;
        pos(0, p0); pos(q.numFrames - 1, p1);
        // os tempos dos keyframes sao absolutos (0, 2, 4, ... em 1/60 s, como no ped.ifp do jogo): duracao = tempo do ultimo
        if (q.frameType == 4) { int16_t t; std::memcpy(&t, q.frames + (q.numFrames - 1) * 16 + 8, 2); dur = t / 60.0f; }
        else { float t; std::memcpy(&t, q.frames + (q.numFrames - 1) * 32 + 16, 4); dur = t; }
        float dx = p1[0] - p0[0], dy = p1[1] - p0[1];
        return dur > 0 ? std::sqrt(dx * dx + dy * dy) / dur : 0;
    }
    return 0;
}

inline std::string fixedStr(const uint8_t* p, size_t n) {
    size_t l = 0;
    while (l < n && p[l]) ++l;
    return std::string((const char*)p, l);
}

// Interpreta b.raw como ANP3. false + b.error se o arquivo estiver truncado ou inconsistente.
inline bool parseAnp3(IfpBlock& b) {
    const std::vector<uint8_t>& d = b.raw;
    size_t pos = 0;
    auto need = [&](size_t n) { return pos + n <= d.size(); };
    auto u32 = [&]() { uint32_t v; std::memcpy(&v, &d[pos], 4); pos += 4; return v; };
    if (!need(36) || std::memcmp(d.data(), "ANP3", 4) != 0) { b.error = "nao e ANP3"; return false; }
    pos = 8;
    b.name = fixedStr(&d[pos], 24); pos += 24;
    uint32_t numAnims = u32();
    for (uint32_t i = 0; i < numAnims; ++i) {
        if (!need(36)) { b.error = "cabecalho de animacao truncado"; return false; }
        IfpAnim a;
        a.name = fixedStr(&d[pos], 24); pos += 24;
        uint32_t numSeq = u32();
        a.dataSize = u32();
        a.flags = u32();
        size_t total = 0;
        for (uint32_t s = 0; s < numSeq; ++s) {
            if (!need(36)) { b.error = "cabecalho de sequencia truncado"; return false; }
            IfpSeq q;
            q.name = fixedStr(&d[pos], 24); pos += 24;
            q.frameType = u32();
            q.numFrames = u32();
            q.boneTag = (int32_t)u32();
            size_t ks = keyFrameSize(q.frameType);
            if (!ks) { b.error = "tipo de keyframe invalido em " + a.name; return false; }
            q.frameBytes = ks * q.numFrames;
            if (!need(q.frameBytes)) { b.error = "keyframes truncados em " + a.name; return false; }
            q.frames = &d[pos];
            pos += q.frameBytes;
            total += q.frameBytes;
            a.seqs.push_back(q);
        }
        if (total != a.dataSize) { b.error = "dataSize != soma dos keyframes em " + a.name; return false; }
        b.anims.push_back(std::move(a));
    }
    return true;
}

// ---- animgrp.dat: "nome, bloco, walkcycle, N" + N nomes de animacao + "end" ----
struct AnimGroupDef {
    std::string name, block;
    std::vector<std::string> anims; // [0] walk, [1] run, [2] sprint, [3] idle, [4] roadcross, [5] walk_start
};

inline std::string lowerStr(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Devolve os grupos do texto, indexados por nome em minusculas.
inline std::map<std::string, AnimGroupDef> parseAnimGroups(const std::string& text) {
    std::map<std::string, AnimGroupDef> out;
    std::istringstream in(text);
    std::string line;
    AnimGroupDef cur;
    bool inGroup = false;
    while (std::getline(in, line)) {
        line = trimStr(line);
        if (line.empty() || line[0] == '#') continue;
        if (!inGroup) {
            std::vector<std::string> f;
            std::istringstream ls(line);
            std::string tok;
            while (std::getline(ls, tok, ',')) f.push_back(trimStr(tok));
            if (f.size() == 4 && !f[0].empty() && !f[1].empty()) { cur = {}; cur.name = f[0]; cur.block = f[1]; inGroup = true; }
        } else if (lowerStr(line) == "end") {
            out[lowerStr(cur.name)] = cur;
            inGroup = false;
        } else {
            cur.anims.push_back(line);
        }
    }
    return out;
}

} // namespace mm
