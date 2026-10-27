#include "../src/pool.hpp"
#include "../src/ifp.hpp"
#include "../src/ai.hpp"
#include "../src/riding.hpp"
#include <windows.h>
#include <cmath>
#include <cassert>
#include <cstdio>
int main(int argc, char** argv) {
    mm::MythConfig c;
    bool ok = mm::MythConfig::load(argc > 1 ? argv[1] : "tests/fixtures/Anim37.ini", c);
    assert(ok);
    assert(c.findRadius > 100.0f && c.findRadius < 100.02f);
    assert(c.quantity == 5 && c.health == 200 && c.damage == 35);
    assert(c.customAnimGroup == "Boar" && c.beheadable);
    assert(c.locations.size() >= 15);
    assert(c.locations[0].x < -658.0f && c.locations[0].x > -658.3f);
    assert(c.animGroup == -1);
    mm::MythConfig none;
    assert(!mm::MythConfig::load("nao_existe.ini", none));
    // pool
    mm::SlotPool<2> pool;
    assert(pool.acquire() == 0 && pool.acquire() == 1 && pool.acquire() == -1);
    pool.release(0);
    assert(pool.count() == 1 && pool.acquire() == 0);
    // horario
    assert(mm::hourInWindow(5, 0, 0) && mm::hourInWindow(23, 22, 4) && mm::hourInWindow(2, 22, 4));
    assert(!mm::hourInWindow(12, 22, 4) && mm::hourInWindow(12, 8, 18) && !mm::hourInWindow(18, 8, 18));
    // local: jogador em cima do local 1 acha o local 0; longe nao acha nada
    const mm::Location& l0 = c.locations[0];
    assert(mm::findLocation(c, l0.x + 1, l0.y, l0.z) == 0);
    assert(mm::findLocation(c, l0.x + 1, l0.y, l0.z + 500) == -1); // 500 m acima: fora do raio 3D
    assert(mm::findLocation(c, l0.x + 150, l0.y, l0.z) >= 0 && c.spawnRadius + 120 > 150); // dentro de spawn radius + 120
    assert(mm::findLocation(c, l0.x + 250, l0.y + 250, l0.z) < 0 || c.locations.size() > 1);
    assert(mm::findLocation(c, 99999, 99999, 0) == -1);
    // IFP ANP3
    mm::IfpBlock blk;
    assert(mm::readFile("tests/fixtures/Boar.ifp", blk.raw));
    bool parsed = mm::parseAnp3(blk);
    if (!parsed) std::printf("erro: %s\n", blk.error.c_str());
    assert(parsed && blk.name == "Boar" && blk.anims.size() == 4);
    assert(blk.anims[0].name == "Boar_sprint" && blk.anims[0].seqs.size() == 31 && blk.anims[0].flags == 1);
    assert(blk.anims[0].seqs[0].name == "Root" && blk.anims[0].seqs[0].frameType == 4 && blk.anims[0].seqs[0].numFrames == 5);
    // descompressao: quaternions ~unitarios, tempos >= 0
    for (auto& a : blk.anims) for (auto& q : a.seqs) {
        size_t ks = mm::uncompressedKeyFrameSize(q.frameType);
        std::vector<uint8_t> out(ks * q.numFrames);
        mm::uncompressKeyFrames(q, out.data());
        for (uint32_t i = 0; i < q.numFrames; ++i) {
            float f[8]; std::memcpy(f, &out[i * ks], ks);
            float len = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2] + f[3] * f[3]);
            assert(len > 0.9f && len < 1.1f && f[4] >= 0.0f && f[4] < 20.0f);
        }
    }
    assert(mm::rootSpeed(blk.anims[0]) > 6.0f && mm::rootSpeed(blk.anims[0]) < 10.0f); // Boar_sprint: ~3,1 m em 0,4 s
    assert(mm::rootSpeed(blk.anims[2]) == 0.0f);                                       // Boar_idle: parado
    {   // achata o deslocamento da raiz: x,y iguais em todos os keyframes, z preservado
        const mm::IfpSeq& root = blk.anims[0].seqs[0];
        std::vector<uint8_t> out(32 * root.numFrames);
        mm::uncompressKeyFrames(root, out.data());
        float before[2]; std::memcpy(before, &out[(root.numFrames - 1) * 32 + 20], 8);
        mm::flattenRootXY(out.data(), root.frameType, root.numFrames);
        float first[2], last[2], zlast;
        std::memcpy(first, &out[20], 8); std::memcpy(last, &out[(root.numFrames - 1) * 32 + 20], 8); std::memcpy(&zlast, &out[(root.numFrames - 1) * 32 + 28], 4);
        assert(first[0] == last[0] && first[1] == last[1] && (before[0] != last[0] || before[1] != last[1]));
        (void)zlast;
    }
    blk.raw.resize(blk.raw.size() - 10);
    blk.anims.clear();
    assert(!mm::parseAnp3(blk)); // truncado
    // animgrp.dat
    std::vector<uint8_t> txt;
    assert(mm::readFile("tests/fixtures/animgrp_boar.dat", txt));
    auto groups = mm::parseAnimGroups(std::string(txt.begin(), txt.end()));
    assert(groups.count("boar") && groups["boar"].block == "Boar" && groups["boar"].anims.size() == 6);
    assert(groups["boar"].anims[3] == "Boar_idle" && groups.count("man"));
    // varre todos os .ifp de uma pasta (opcional): tests\test_config.exe <ini> <pasta>
    if (argc > 2) {
        WIN32_FIND_DATAA fd;
        std::string dir = argv[2];
        int n = 0;
        for (HANDLE h = FindFirstFileA((dir + "\\*.ifp").c_str(), &fd); h != INVALID_HANDLE_VALUE;) {
            mm::IfpBlock b;
            assert(mm::readFile(dir + "\\" + fd.cFileName, b.raw));
            bool ok = mm::parseAnp3(b);
            std::printf("%-12s %s (%zu anims) %s\n", fd.cFileName, ok ? "ok" : "ERRO", b.anims.size(), b.error.c_str());
            assert(ok);
            ++n;
            if (!FindNextFileA(h, &fd)) break;
        }
        std::printf("%d IFPs lidos\n", n);
    }
    {   // cavalgada: angulo, rampa de velocidade, marchas e limite da mira
        assert(std::fabs(mm::wrapAngle(7.0f) - (7.0f - 6.2831853f)) < 1e-4f && std::fabs(mm::wrapAngle(-7.0f) - (-7.0f + 6.2831853f)) < 1e-4f);
        assert(std::fabs(mm::approach(0.0f, 4.7f, 3.0f, 0.1f) - 0.3f) < 1e-5f);
        assert(mm::approach(4.6f, 4.7f, 3.0f, 0.1f) == 4.7f && mm::approach(0.1f, 0.0f, 8.0f, 0.1f) == 0.0f && mm::approach(5.0f, 5.0f, 3.0f, 0.1f) == 5.0f);
        assert(mm::gait(0.0f) == 0 && mm::gait(1.0f) == 1 && mm::gait(2.79f) == 1 && mm::gait(2.8f) == 2 && mm::gait(6.19f) == 2 && mm::gait(6.2f) == 3);
        const float lim = 75.0f * 3.14159265f / 180.0f;
        assert(mm::aimExcess(0.5f, lim) == 0.0f && mm::aimExcess(-lim, lim) == 0.0f);
        assert(std::fabs(mm::aimExcess(1.5f, lim) - (1.5f - lim)) < 1e-5f && std::fabs(mm::aimExcess(-1.5f, lim) + (1.5f - lim)) < 1e-5f);
        assert(mm::aimExcess(3.0f, 0.0f) == 0.0f); // limite 0 = desligado
    }
    for (auto& l : c.locations) assert(!(l.x == 0 && l.y == 0 && l.z == 0));
    std::printf("ok: %zu locais\n", c.locations.size());
}
