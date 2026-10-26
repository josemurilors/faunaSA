// ASI do Myths Maker. Um unico tick para todos os animais, pool fixo de peds e carga de modelo sem bloqueio.
// Baseado no mod Myths Maker 2.7 de Nadalao e Tarzan_3 (formato dos .ini e dos assets).
// O carregamento do modelo repete o que o CLEO+ (JuniorDjjr) faz em LOAD_SPECIAL_CHARACTER_FOR_ID.
#include "plugin.h"
#include "CClock.h"
#include "CFont.h"
#include "CSprite2d.h"
#include "CAnimBlendHierarchy.h"
#include "CAnimManager.h"
#include "ePedAnims.h"
#include "CCivilianPed.h"
#include "CCutsceneMgr.h"
#include "CDirectory.h"
#include "CModelInfo.h"
#include "CPedModelInfo.h"
#include "CPools.h"
#include "CPedIntelligence.h"
#include "CPad.h"
#include "CTxdStore.h"
#include "TxdDef.h"
#include "CPopulation.h"
#include "CTaskManager.h"
#include "CStreaming.h"
#include "CStreamingInfo.h"
#include "CTimer.h"
#include "CWeapon.h"
#include "CWorld.h"
#include "ai.hpp"
#include "ifp.hpp"
#include "pool.hpp"
#include <cstdarg>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace plugin;

namespace {

constexpr size_t kMaxLive = 24;        // teto de animais vivos ao mesmo tempo (todos os mitos)
constexpr unsigned kTickMs = 500;      // intervalo do tick de spawn/despawn
constexpr unsigned kCorpseMs = 10000;  // tempo de cadaver antes de reciclar o slot
constexpr float kDespawnFactor = 1.5f; // despawn acima de findRadius * fator
constexpr int kIdFirst = 15000, kIdLast = 16000; // faixa de IDs livres usada pelo mod original
constexpr unsigned kLoadTimeoutMs = 15000;
constexpr float kMaxWalk = 1.7f, kMaxRun = 3.5f, kMaxSprint = 4.5f; // m/s

struct ModelRef {
    std::string name;
    int id = -1;
    int state = 0; // 0 nao pedido, 1 pedido, 2 carregado, 3 falhou
    unsigned requestedAt = 0;
};

struct Myth {
    std::string name;
    mm::MythConfig cfg;
    std::vector<ModelRef> models;
};

// Animacoes do grupo (Boar, dog...), carregadas por nos a partir do .ifp, sem registrar bloco no jogo.
struct AnimSet {
    CAnimBlendHierarchy* walk = nullptr;
    CAnimBlendHierarchy* run = nullptr;
    CAnimBlendHierarchy* sprint = nullptr;
    CAnimBlendHierarchy* idle = nullptr;
    float walkSpeed = 1.4f, runSpeed = 3.5f, sprintSpeed = 5.0f; // m/s, derivadas do deslocamento da raiz da animacao
    bool human = false; // sem .ifp: toca as animacoes de andar/correr/parado de humano do proprio jogo
};

// Vaca, gorila, raposa, capivara, pato e galinha nao tem grupo que sirva (docs/ESQUELETOS.md): ficam com a animacao de humano
// do grupo do ped, como no pacote original. Velocidades medias do andar/correr/sprint civil do jogo.
const AnimSet gHumanSet = [] { AnimSet a; a.human = true; a.walkSpeed = 1.6f; a.runSpeed = 3.6f; a.sprintSpeed = 5.5f; return a; }();

struct Slot {
    CPed* ped = nullptr;
    const AnimSet* anim = nullptr;
    int myth = -1;
    unsigned deadSince = 0;
    mm::AiBrain brain;
    mm::Move move = mm::Move::Idle;
    float lastHealth = 0;
    float heading = 0;
};

std::vector<Myth> gMyths;
std::array<Slot, kMaxLive> gSlots;
mm::SlotPool<kMaxLive> gPool;
bool gLoaded = false;
unsigned gNextTick = 0;
unsigned gReadySince = 0;
unsigned gLastNowForSpawn = 0;
constexpr unsigned kReadyMs = 5000;

void Log(const char* fmt, ...) {
    static bool first = true; // um log por sessao de jogo
    FILE* f = nullptr;
    if (fopen_s(&f, "FaunaSA.log", first ? "w" : "a") != 0 || !f) return;
    first = false;
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    std::fputc('\n', f); std::fclose(f);
}

// Registra o endereco do crash no log (o CrashInfo nao gerou relatorio): excecao, EIP e retornos do stack dentro do exe.
LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* e) {
    static int logged = 0;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    // So depois do 1o tick do ASI (no boot ha excecoes inofensivas) e sem repetir o mesmo EIP.
    static DWORD lastEip = 0;
    if ((code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO) || logged >= 12 ||
        !gLoaded || e->ContextRecord->Eip == lastEip)
        return EXCEPTION_CONTINUE_SEARCH;
    lastEip = e->ContextRecord->Eip;
    ++logged;
    char mod[MAX_PATH] = "?";
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)e->ContextRecord->Eip, &hm))
        GetModuleFileNameA(hm, mod, MAX_PATH);
    Log("CRASH %08lX em EIP=%08lX (%s +%lX) acesso=%p", code, e->ContextRecord->Eip, mod, (DWORD)((char*)e->ContextRecord->Eip - (char*)hm),
        e->ExceptionRecord->NumberParameters > 1 ? (void*)e->ExceptionRecord->ExceptionInformation[1] : nullptr);
    DWORD* sp = (DWORD*)e->ContextRecord->Esp;
    int n = 0;
    for (int i = 0; i < 256 && n < 12; ++i) {
        if (IsBadReadPtr(sp + i, 4)) break;
        if (sp[i] >= 0x401000 && sp[i] < 0x86A000) { Log("  stack[%d] = %08lX", i, sp[i]); ++n; }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Vigia: se o thread principal parar de chamar o ASI por 8 s, grava onde ele esta (EIP, pilha, etapa do ASI), para
// diagnosticar travamentos sem depender de relatorio de crash. Grava no maximo 3 vezes.
volatile DWORD gBeat = 0, gMainTid = 0;
volatile const char* gStage = "inicio";
volatile int gStageSlot = -1;

DWORD WINAPI Watchdog(LPVOID) {
    int logged = 0;
    for (;;) {
        Sleep(2000);
        DWORD tid = gMainTid;
        if (!tid || logged >= 3 || GetTickCount() - gBeat < 8000) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, tid);
        if (!h) continue;
        SuspendThread(h);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(h, &c)) {
            Log("TRAVADO ha %lu ms: EIP=%08lX ESP=%08lX etapa=%s slot=%d", GetTickCount() - gBeat, c.Eip, c.Esp, gStage, gStageSlot);
            DWORD* sp = (DWORD*)c.Esp;
            int n = 0;
            for (int i = 0; i < 512 && n < 16; ++i) {
                if (IsBadReadPtr(sp + i, 4)) break;
                if (sp[i] >= 0x401000 && sp[i] < 0x86A000) { Log("  retorno[%d] = %08lX", i, sp[i]); ++n; }
            }
        }
        ResumeThread(h);
        CloseHandle(h);
        ++logged;
    }
}

bool FileExists(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Nomes de modelo da pasta: A<n>_<k>/M<n>_<k> (como o script original) ou, se nao houver, o nome da pasta.
void FindModels(Myth& m, const std::string& dir, char prefix) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "*.dff").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::string> all, variants;
    do {
        std::string n = fd.cFileName;
        n.resize(n.size() - 4);
        if (!FileExists(dir + n + ".txd")) continue;
        all.push_back(n);
        if ((n[0] == prefix || n[0] == prefix + 32) && n.find('_') != std::string::npos) variants.push_back(n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    for (auto& n : variants.empty() ? all : variants) m.models.push_back({ n });
}

// Grupos de animacao para animais que nao trazem .ifp proprio (analise em docs/ESQUELETOS.md). O FaunaSA.ini do jogo,
// secao [AnimGroups] (Anim7=Boar, ou Anim7=none para desligar), sobrescreve estes padroes.
std::string DefaultAnimGroup(const std::string& folder) {
    if (folder == "Anim7") return "Boar";   // ovelha: pose de repouso proxima da do javali e do urso
    return "Fauna" + folder;                // gerado por tools/gen_ifp.py (cleo\Animals\FaunaSA); sem ele, cai na animacao de humano
}

void ApplyAnimMap(Myth& m) {
    if (!m.cfg.customAnimGroup.empty()) return;
    static mm::Ini ini;
    static bool loaded = false;
    if (!loaded) { ini.load("FaunaSA.ini"); loaded = true; }
    std::string g = ini.has("AnimGroups", m.name) ? ini.str("AnimGroups", m.name) : DefaultAnimGroup(m.name);
    if (g.empty() || mm::lowerStr(g) == "none") return;
    m.cfg.customAnimGroup = g;
    Log("%s: sem .ifp proprio, usando o grupo de animacao '%s'", m.name.c_str(), g.c_str());
}

void LoadDir(const char* sub, char prefix) {
    WIN32_FIND_DATAA fd;
    std::string base = std::string("cleo\\") + sub + "\\";
    HANDLE h = FindFirstFileA((base + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        Myth m;
        m.name = fd.cFileName;
        std::string dir = base + m.name + "\\";
        if (!mm::MythConfig::load(dir + m.name + ".ini", m.cfg)) continue;
        FindModels(m, dir, prefix);
        ApplyAnimMap(m);
        if (m.models.empty()) { Log("%s: sem .dff/.txd, ignorado", m.name.c_str()); continue; }
        gMyths.push_back(std::move(m));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Cria o CPedModelInfo num ID livre, copiando o basico do modelo 290 (igual ao CLEO+).
int AllocModelId() {
    for (int id = kIdFirst; id <= kIdLast; ++id) {
        if (CModelInfo::GetModelInfo(id)) continue;
        CPedModelInfo* mi = CModelInfo::AddPedModel(id);
        if (!mi) return -1;
        mi->SetColModel((CColModel*)0x968DF0, false);
        mi->m_nPedType = PED_TYPE_CIVMALE;
        mi->m_nStatType = 41;
        if (auto* base = (CPedModelInfo*)CModelInfo::GetModelInfo(290)) {
            mi->m_nAnimType = base->m_nAnimType;
            mi->m_nCarsCanDriveMask = base->m_nCarsCanDriveMask;
            mi->m_nPedFlags = base->m_nPedFlags;
            mi->m_nRadio1 = base->m_nRadio1;
            mi->m_nRace = base->m_nRace;
            mi->m_nPedAudioType = base->m_nPedAudioType;
            mi->m_nVoice1 = base->m_nVoice1;
            mi->m_nVoice2 = base->m_nVoice2;
            mi->m_nVoiceId = base->m_nVoiceId;
        }
        return id;
    }
    return -1;
}

// O jogo resolve texturas pelo nome; os modelos do mod usam nomes de roupa do CJ (head_diff_000_a_whi, uppr_diff_000_a_uni...)
// que podem acabar apontando para a textura de outro dicionario/mod. Reaponta cada material para a textura do TXD do proprio modelo.
struct TexFixCtx { RwTexDictionary* dict; const char* model; int swapped, total; };

RpMaterial* FixMaterialCB(RpMaterial* mat, void* data) {
    auto* c = (TexFixCtx*)data;
    RwTexture* tex = RpMaterialGetTexture(mat);
    if (!tex) return mat;
    ++c->total;
    // Alguns modelos (ovelha) trazem cor de material forte (verde/amarelo) que tinge a textura; o resto usa cinza neutro (0x95).
    RwRGBA col = *RpMaterialGetColor(mat);
    if (std::max({ col.red, col.green, col.blue }) - std::min({ col.red, col.green, col.blue }) > 24) {
        Log("  %s: cor de material (%d,%d,%d) tingia a textura '%s'; trocada por cinza neutro", c->model, col.red, col.green, col.blue, tex->name);
        RwRGBA neutral{ 149, 149, 149, col.alpha };
        RpMaterialSetColor(mat, &neutral);
    }
    RwTexture* own = RwTexDictionaryFindNamedTexture(c->dict, tex->name);
    int w = tex->raster ? RwRasterGetWidth(tex->raster) : 0, h = tex->raster ? RwRasterGetHeight(tex->raster) : 0;
    if (own && own != tex) {
        RpMaterialSetTexture(mat, own);
        ++c->swapped;
        Log("  %s: textura '%s' (%dx%d) vinha de outro dicionario; trocada pela do TXD do modelo", c->model, tex->name, w, h);
    } else if (!own) {
        Log("  %s: textura '%s' (%dx%d) NAO existe no TXD do modelo", c->model, tex->name, w, h);
    }
    return mat;
}

RpAtomic* FixAtomicCB(RpAtomic* atom, void* data) {
    if (RpGeometry* geo = RpAtomicGetGeometry(atom)) RpGeometryForAllMaterials(geo, FixMaterialCB, data);
    return atom;
}

void FixModelTextures(const ModelRef& r) {
    CBaseModelInfo* mi = CModelInfo::GetModelInfo(r.id);
    if (!mi || !mi->m_pRwObject || mi->m_nTxdIndex < 0) return;
    TxdDef* td = CTxdStore::ms_pTxdPool->GetAt(mi->m_nTxdIndex);
    if (!td || !td->m_pRwDictionary) return;
    TexFixCtx ctx{ td->m_pRwDictionary, r.name.c_str(), 0, 0 };
    RpClumpForAllAtomics((RpClump*)mi->m_pRwObject, FixAtomicCB, &ctx);
    Log("%s: %d materiais com textura, %d trocados", r.name.c_str(), ctx.total, ctx.swapped);
}

// Avanca a maquina de estados do modelo sem bloquear; true quando carregado.
bool EnsureLoaded(ModelRef& r, unsigned now) {
    if (r.state == 3) return false;
    if (r.state == 2 && !CStreaming::HasModelLoaded(r.id)) r.state = 0; // o jogo descarregou: pede de novo
    if (r.state == 0) {
        if (r.id < 0) r.id = AllocModelId(); // o CPedModelInfo e criado uma vez por modelo
        if (r.id < 0) { r.state = 3; Log("%s: sem ID livre em %d-%d", r.name.c_str(), kIdFirst, kIdLast); return false; }
        bool inDir = CStreaming::ms_pExtraObjectsDir && CStreaming::ms_pExtraObjectsDir->FindItem(r.name.c_str());
        CStreaming::RequestSpecialModel(r.id, r.name.c_str(), KEEP_IN_MEMORY | MISSION_REQUIRED);
        r.state = 1;
        r.requestedAt = now;
        Log("%s: pedido (id %d, no ExtraObjectsDir: %s)", r.name.c_str(), r.id, inDir ? "sim" : "nao");
    }
    if (r.state == 1) {
        if (CStreaming::HasModelLoaded(r.id)) {
            r.state = 2;
            Log("%s: carregado em %u ms", r.name.c_str(), now - r.requestedAt);
            FixModelTextures(r);
        } else if (now - r.requestedAt > kLoadTimeoutMs) {
            r.state = 3;
            Log("%s: TIMEOUT, o jogo nao carregou o modelo", r.name.c_str());
        }
    }
    return r.state == 2;
}

std::map<std::string, std::string> gIfpPaths;            // nome do bloco (minusculas) -> caminho do .ifp
std::map<std::string, mm::AnimGroupDef> gGroups;          // grupos dos animgrp.dat das pastas dos animais
std::map<std::string, AnimSet> gSets;                     // grupo (minusculas) -> animacoes ja construidas
bool gAnimIndexed = false;

void IndexAnimFiles() {
    gAnimIndexed = true;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("cleo\\Animals\\*", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        std::string dir = std::string("cleo\\Animals\\") + fd.cFileName + "\\";
        WIN32_FIND_DATAA f2;
        HANDLE h2 = FindFirstFileA((dir + "*.ifp").c_str(), &f2);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                std::string n = f2.cFileName;
                gIfpPaths[mm::lowerStr(n.substr(0, n.size() - 4))] = dir + n;
            } while (FindNextFileA(h2, &f2));
            FindClose(h2);
        }
        std::vector<uint8_t> txt;
        if (mm::readFile(dir + "animgrp.dat", txt))
            for (auto& g : mm::parseAnimGroups(std::string(txt.begin(), txt.end()))) gGroups.insert(g);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    Log("ifp indexados: %zu arquivos, %zu grupos de animacao", gIfpPaths.size(), gGroups.size());
}

// Constroi uma CAnimBlendHierarchy como o LoadAnimFile do jogo faz para ANP3, mas fora das tabelas globais
// (ms_aAnimations/ms_aAnimBlocks), que ja estao cheias nesta instalacao. Os keyframes comprimidos do .ifp sao
// descomprimidos aqui, uma vez: o jogo escolhe o formato de update por clump (nao por animacao), entao uma
// animacao "mantida comprimida" misturada com as de humano do ped trava o jogo, e a descompressao do proprio
// jogo (cache + free de blocos) corrompia o heap com animacoes que nao estao nas tabelas dele.
CAnimBlendHierarchy* BuildHierarchy(const mm::IfpAnim& a, int blockId) {
    auto* h = new CAnimBlendHierarchy();
    h->SetName(a.name.c_str());
    h->m_bRunningCompressed = 0; // m_bIsCompressed: os dados ja estao descomprimidos
    h->field_B = 0;              // m_bKeepCompressed
    h->m_nAnimBlockId = blockId;
    h->m_nSeqCount = (unsigned short)a.seqs.size();
    h->m_pSequences = new CAnimBlendSequence[a.seqs.size()];
    size_t total = 0;
    for (const mm::IfpSeq& q : a.seqs) total += mm::uncompressedKeyFrameSize(q.frameType) * q.numFrames;
    auto* chunk = (unsigned char*)std::calloc(total ? total : 1, 1); // nunca liberado: vive ate o jogo fechar
    size_t off = 0;
    for (size_t i = 0; i < a.seqs.size(); ++i) {
        const mm::IfpSeq& q = a.seqs[i];
        CAnimBlendSequence& seq = h->m_pSequences[i];
        seq.SetName(q.name.c_str());
        if (q.boneTag != -1) seq.SetBoneTag(q.boneTag);
        mm::uncompressKeyFrames(q, chunk + off);
        mm::flattenRootXY(chunk + off, q.frameType, q.numFrames); // andar e com a IA; a animacao fica no lugar
        seq.SetNumFrames((int)q.numFrames, q.frameType == 2 || q.frameType == 4, false, chunk + off);
        off += mm::uncompressedKeyFrameSize(q.frameType) * q.numFrames;
    }
    h->RemoveQuaternionFlips();
    h->CalcTotalTime();
    return h;
}

// Animacoes do grupo; devolve nullptr se o grupo ou o .ifp nao puderem ser carregados (logado uma vez).
const AnimSet* GetAnimSet(const std::string& group) {
    if (group.empty()) return nullptr;
    if (!gAnimIndexed) IndexAnimFiles();
    std::string key = mm::lowerStr(group);
    auto cached = gSets.find(key);
    if (cached != gSets.end()) return cached->second.idle ? &cached->second : nullptr;
    AnimSet& set = gSets[key]; // entrada vazia = falhou, para nao tentar de novo
    auto g = gGroups.find(key);
    if (g == gGroups.end() || g->second.anims.size() < 4) { Log("grupo de animacao '%s' nao esta em nenhum animgrp.dat", group.c_str()); return nullptr; }
    auto p = gIfpPaths.find(mm::lowerStr(g->second.block));
    if (p == gIfpPaths.end()) { Log("'%s': %s.ifp nao encontrado em cleo\\Animals", group.c_str(), g->second.block.c_str()); return nullptr; }
    mm::IfpBlock blk;
    if (!mm::readFile(p->second, blk.raw) || !mm::parseAnp3(blk)) { Log("'%s': %s invalido: %s", group.c_str(), p->second.c_str(), blk.error.c_str()); return nullptr; }
    int pedBlock = CAnimManager::GetAnimationBlockIndex("ped"); // so para a contagem de referencias; nunca descarrega
    std::map<std::string, CAnimBlendHierarchy*> byName;
    for (auto& a : blk.anims) byName[mm::lowerStr(a.name)] = BuildHierarchy(a, pedBlock < 0 ? 0 : pedBlock);
    auto pick = [&](size_t i) { auto it = i < g->second.anims.size() ? byName.find(mm::lowerStr(g->second.anims[i])) : byName.end(); return it == byName.end() ? nullptr : it->second; };
    set.walk = pick(0); set.run = pick(1); set.sprint = pick(2); set.idle = pick(3);
    // Velocidade de cada animacao = deslocamento da raiz / duracao do ciclo (sem deslizar os pes).
    auto speedOf = [&](size_t idx, float fallback, float lo, float hi) {
        if (idx >= g->second.anims.size()) return fallback;
        for (auto& a : blk.anims)
            if (mm::lowerStr(a.name) == mm::lowerStr(g->second.anims[idx])) { float v = mm::rootSpeed(a); return v > 0.2f ? std::clamp(v, lo, hi) : fallback; }
        return fallback;
    };
    set.walkSpeed = speedOf(0, 1.4f, 0.4f, 3.0f);
    set.runSpeed = speedOf(1, 5.0f, 2.0f, 14.0f);
    set.sprintSpeed = speedOf(2, 6.0f, 2.0f, 14.0f);
    Log("'%s': %zu animacoes de %s (idle %s, run %s, walk %s)", group.c_str(), blk.anims.size(), p->second.c_str(),
        set.idle ? "ok" : "FALTA", set.run ? "ok" : "falta", set.walk ? "ok" : "falta");
    return set.idle ? &set : nullptr;
}

// Ped vivo e valido mais proximo ao alcance (alvo de ataque). O proprio animal e peds mortos ficam de fora; outros
// animais do mod so contam para quem ataca animais.
CPed* FindTarget(const Slot& me, const CVector& pos, float range, bool attacksAnimals, float* outDist) {
    CPed* best = nullptr;
    float bestD = range;
    auto* pool = CPools::ms_pPedPool;
    for (int i = 0; i < pool->m_nSize; ++i) {
        CPed* p = pool->GetAt(i);
        if (!p || p == me.ped || p->m_fHealth <= 0.0f) continue;
        bool ours = false;
        for (size_t k = 0; k < kMaxLive; ++k) ours |= gPool.used((int)k) && gSlots[k].ped == p;
        if (ours && !attacksAnimals) continue;
        CVector d = p->GetPosition() - pos;
        float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dist < bestD) { bestD = dist; best = p; }
    }
    if (outDist) *outDist = bestD;
    return best;
}

float WrapAngle(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

// Um passo da IA por frame para cada animal: decide, gira, anda (a nossa fisica, na altura do chao) e toca a animacao.
void UpdateAnimals(unsigned now) {
    static unsigned lastNow = 0;
    float dt = lastNow ? std::min(0.1f, (now - lastNow) / 1000.0f) : 0.0f;
    lastNow = now;
    static unsigned lastDiag = 0;
    bool diagFrame = now - lastDiag > 1500;
    if (diagFrame) lastDiag = now;
    CPed* player = FindPlayerPed();
    for (size_t i = 0; i < kMaxLive; ++i) {
        if (!gPool.used((int)i)) continue;
        Slot& s = gSlots[i];
        if (!s.anim || !s.ped || s.ped->m_fHealth <= 0.0f || !CPools::ms_pPedPool->IsObjectValid(s.ped) || !s.ped->m_pRwClump) continue;
        gStageSlot = (int)i;
        // Esbarrao/empurrao: o jogo poe tasks de reacao de humano (cair, desviar). Limpa as de reacao; a anim fica nossa.
        if (s.ped->m_pIntelligence) {
            CTaskManager& tm = s.ped->m_pIntelligence->m_TaskMgr;
            if (tm.m_aPrimaryTasks[TASK_PRIMARY_PHYSICAL_RESPONSE]) tm.SetTask(nullptr, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
            if (tm.m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_TEMP] || tm.m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_NONTEMP]) tm.ClearTaskEventResponse();
        }
        const mm::MythConfig& c = gMyths[s.myth].cfg;
        const std::string grp = mm::lowerStr(c.customAnimGroup);
        mm::AiCfg ac;
        ac.wander = c.walkMode == 1;
        ac.attacksPeds = c.attacksPeds || c.attacksAnimals;
        ac.skittish = grp == "deer1" || grp == "rabit";
        ac.wanderRadius = std::max(8.0f, c.spawnRadius * 0.6f);
        ac.attackIntervalMs = mm::attackInterval(c.attackRate);

        CVector pos = s.ped->GetPosition();
        mm::AiWorld w;
        w.now = now; w.x = pos.x; w.y = pos.y;
        w.hurt = s.lastHealth > 0 && s.ped->m_fHealth < s.lastHealth - 0.5f;
        s.lastHealth = s.ped->m_fHealth;
        if (player) {
            CVector pp = player->GetPosition();
            w.playerX = pp.x; w.playerY = pp.y;
            w.playerDist = std::sqrt((pp.x - pos.x) * (pp.x - pos.x) + (pp.y - pos.y) * (pp.y - pos.y));
        }
        CPed* target = nullptr;
        if (ac.attacksPeds) {
            float td = 0;
            target = FindTarget(s, pos, ac.detectRange, c.attacksAnimals, &td);
            if (target) { CVector tp = target->GetPosition(); w.hasTarget = true; w.targetX = tp.x; w.targetY = tp.y; w.targetDist = td; }
        }
        mm::AiOut out = mm::aiStep(s.brain, ac, w);

        // Gira em direcao ao heading desejado (4 rad/s) e anda na velocidade da animacao atual.
        if (out.hasHeading) {
            float diff = WrapAngle(out.heading - s.heading), maxTurn = 4.0f * dt;
            s.heading = WrapAngle(s.heading + std::clamp(diff, -maxTurn, maxTurn));
        }
        // Velocidade nativa da animacao, limitada para o jogador conseguir fugir (sprint do CJ > 5 m/s); a animacao toca mais devagar na mesma proporcao.
        float native = out.move == mm::Move::Walk ? s.anim->walkSpeed : out.move == mm::Move::Run ? s.anim->runSpeed : out.move == mm::Move::Sprint ? s.anim->sprintSpeed : 0.0f;
        float cap = out.move == mm::Move::Walk ? kMaxWalk : out.move == mm::Move::Run ? kMaxRun : kMaxSprint;
        float speed = std::min(native, cap);
        float animRate = native > 0.01f ? speed / native : 1.0f;
        float nx = pos.x - std::sin(s.heading) * speed * dt, ny = pos.y + std::cos(s.heading) * speed * dt, nz = pos.z;
        if (speed > 0) {
            bool found = false;
            float gz = CWorld::FindGroundZFor3DCoord(nx, ny, pos.z + 1.5f, &found, nullptr);
            if (found && std::fabs((gz + 1.0f) - pos.z) < 2.5f) nz = pos.z + ((gz + 1.0f) - pos.z) * std::min(1.0f, 10.0f * dt);
            else { nx = pos.x; ny = pos.y; s.brain.stateUntil = 0; } // sem chao carregado ou penhasco: nao anda e escolhe outro destino
        }
        s.ped->SetPosition(nx, ny, nz);
        {   // DIAG-MOVE: a cada 1,5 s, ate 80 linhas, mostra o que a IA decidiu e se a posicao colou
            static int lines = 0;
            if (lines < 120 && diagFrame) {
                CVector after = s.ped->GetPosition();
                Log("MOVE slot %zu %s estado=%d move=%d vel=%.2f dt=%.3f pos=(%.2f,%.2f,%.2f) novo=(%.2f,%.2f,%.2f) depois=(%.2f,%.2f,%.2f) heading=%.2f alvo=(%.1f,%.1f)",
                    i, gMyths[s.myth].name.c_str(), (int)s.brain.state, (int)out.move, speed, dt, pos.x, pos.y, pos.z, nx, ny, nz, after.x, after.y, after.z,
                    s.heading, s.brain.tx, s.brain.ty);
                ++lines;
            }
        }
        s.ped->SetHeading(s.heading);
        s.ped->m_fHeadingCurrent = s.ped->m_fHeadingGoal = s.heading;
        s.ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f); // a fisica do jogo nao empurra o animal alem do que a IA decidiu

        if (out.attack && target && CPools::ms_pPedPool->IsObjectValid(target)) {
            CWeapon::GenerateDamageEvent(target, s.ped, WEAPONTYPE_UNARMED, (int)(c.damage * 0.5f), (ePedPieceTypes)3, 0);
            Log("%s ataca (dano %d)", gMyths[s.myth].name.c_str(), (int)(c.damage * 0.5f));
        }

        // Animacao conforme o movimento; sem a de andar/correr do grupo, cai na mais parecida.
        if (s.anim->human) {
            int id = out.move == mm::Move::Idle ? PED_ANIM_IDLE_STANCE : out.move == mm::Move::Walk ? PED_ANIM_WALK_CIVI : out.move == mm::Move::Run ? PED_ANIM_RUN_CIVI : PED_ANIM_SPRINT_PANIC;
            if (auto* a = CAnimManager::BlendAnimation(s.ped->m_pRwClump, (int)s.ped->m_nAnimGroup, id, 4.0f)) a->m_fSpeed = animRate;
            continue;
        }
        CAnimBlendHierarchy* h = s.anim->idle;
        if (out.move == mm::Move::Walk) h = s.anim->walk ? s.anim->walk : s.anim->run ? s.anim->run : h;
        else if (out.move == mm::Move::Run) h = s.anim->run ? s.anim->run : h;
        else if (out.move == mm::Move::Sprint) h = s.anim->sprint ? s.anim->sprint : s.anim->run ? s.anim->run : h;
        CAnimBlendAssociation* assoc = CAnimManager::BlendAnimation(s.ped->m_pRwClump, h, ANIMATION_LOOPED, 4.0f);
        if (assoc) assoc->m_fSpeed = animRate;
        if (diagFrame && assoc) {
            static int alines = 0;
            if (alines++ < 60) Log("ANIM slot %zu %s hier=%p total=%.2fs tempo=%.2f mistura=%.2f vel=%.2f flags=%04X", i, gMyths[s.myth].name.c_str(), (void*)h, h->m_fTotalTime,
                assoc->m_fCurrentTime, assoc->m_fBlendAmount, assoc->m_fSpeed, (unsigned)assoc->m_nFlags);
        }
    }
}

constexpr int kMaxResident = 16; // modelos de mito carregados ao mesmo tempo, sem ped usando

// Solta modelos sem uso quando passam do limite, como o UNLOAD_SPECIAL_CHARACTER_FROM_ID do CLEO+.
void ReleaseUnusedModels() {
    int resident = 0;
    for (auto& m : gMyths) for (auto& r : m.models) resident += r.state == 2;
    for (auto& m : gMyths) {
        for (auto& r : m.models) {
            if (resident <= kMaxResident) return;
            if (r.state != 2) continue;
            CBaseModelInfo* mi = CModelInfo::GetModelInfo(r.id);
            if (!mi || mi->m_nRefCount != 0) continue; // ainda ha animal usando
            CStreaming::SetMissionDoesntRequireModel(r.id);
            CStreaming::RemoveModel(r.id);
            r.state = 0; // pedido de novo se precisar (o ID de modelo ja existe, so o recurso e recarregado)
            --resident;
        }
    }
}

int LiveOf(int myth) {
    int n = 0;
    for (size_t i = 0; i < kMaxLive; ++i) if (gPool.used((int)i) && gSlots[i].myth == myth) ++n;
    return n;
}

void Release(int i) {
    Slot& s = gSlots[i];
    if (s.ped && CPools::ms_pPedPool->IsObjectValid(s.ped)) CPopulation::RemovePed(s.ped);
    s = Slot{};
    gPool.release(i);
}

void Despawn(const CVector& pl, unsigned now) {
    for (size_t i = 0; i < kMaxLive; ++i) {
        if (!gPool.used((int)i)) continue;
        Slot& s = gSlots[i];
        if (!s.ped || !CPools::ms_pPedPool->IsObjectValid(s.ped)) { Release((int)i); continue; } // removido por outro mod/jogo
        if (s.ped->m_fHealth <= 0.0f) {
            if (!s.deadSince) s.deadSince = now;
            if (now - s.deadSince > kCorpseMs) Release((int)i);
            continue;
        }
        float lim = gMyths[s.myth].cfg.findRadius * kDespawnFactor;
        CVector d = s.ped->GetPosition() - pl;
        if (d.x * d.x + d.y * d.y > lim * lim) Release((int)i);
    }
}

bool SpawnAt(int mi, const ModelRef& model, const CVector& pos, float angleDeg) {
    int slot = gPool.acquire();
    if (slot < 0) return false;
    const mm::MythConfig& c = gMyths[mi].cfg;
    Log("spawn %s (modelo %d, animacao '%s')", model.name.c_str(), model.id, c.customAnimGroup.c_str());
    auto* ped = new CCivilianPed(PED_TYPE_MISSION1, model.id);
    if (!ped) { gPool.release(slot); return false; }
    ped->m_nCreatedBy = 2; // PED_MISSION: o gerenciador de populacao nao recicla
    ped->m_fHealth = (float)c.health;
    // m_nAnimGroup fica como o modelo (copiado do 290): o "animgroup=N" do .ini nao e um indice de grupo do jogo, e forcar
    // um grupo cujo bloco nao esta carregado derrubava o jogo em UncompressAnimation (humanos, como o Anim0).
    ped->SetPosition(pos.x, pos.y, pos.z + 1.0f); // pos.z e o chao; +1 m e o centro do ped
    ped->SetHeading(DegToRad(angleDeg));
    ped->m_fHeadingCurrent = ped->m_fHeadingGoal = DegToRad(angleDeg);
    CWorld::Add(ped);
    // Variantes com esqueleto proprio tem grupo por modelo (Fauna<modelo>); senao vale o grupo do mito.
    const AnimSet* set = c.customAnimGroup.rfind("Fauna", 0) == 0 ? GetAnimSet("Fauna" + model.name) : nullptr;
    if (!set) set = GetAnimSet(c.customAnimGroup);
    gSlots[slot] = { ped, set ? set : &gHumanSet, mi, 0 };
    Slot& ns = gSlots[slot];
    ns.heading = DegToRad(angleDeg);
    ns.lastHealth = ped->m_fHealth;
    ns.brain.homeX = pos.x; ns.brain.homeY = pos.y;
    ns.brain.stateUntil = gLastNowForSpawn + 1000 + (unsigned)(3000 * mm::rnd01());
    return true;
}

void TrySpawn(int mi, const CVector& pl, unsigned now) {
    Myth& m = gMyths[mi];
    const mm::MythConfig& c = m.cfg;
    if (!mm::hourInWindow(CClock::ms_nGameClockHours, c.startHour, c.endHour)) return;
    if (LiveOf(mi) >= c.quantity) return;
    int loc = mm::findLocation(c, pl.x, pl.y, pl.z);
    if (loc < 0) return;
    ModelRef& model = m.models[rand() % m.models.size()];
    if (!EnsureLoaded(model, now)) return; // pede e volta no proximo tick: nao trava o frame
    // Como o original: cada animal nasce espalhado em volta do local, na altura real do chao. Se a colisao do
    // ponto ainda nao carregou (local longe), tenta de novo no proximo tick em vez de nascer no ar.
    const mm::Location& l = c.locations[loc];
    float ang = 6.2831853f * (float)rand() / (float)RAND_MAX;
    float dist = c.spawnRadius * 0.5f * std::sqrt((float)rand() / (float)RAND_MAX);
    float x = l.x + std::cos(ang) * dist, y = l.y + std::sin(ang) * dist;
    bool found = false;
    float gz = CWorld::FindGroundZFor3DCoord(x, y, l.z + 50.0f, &found, nullptr);
    if (!found || std::fabs(gz - l.z) > 50.0f) return;
    SpawnAt(mi, model, CVector(x, y, gz), l.angle);
}

// Menu de teste: F9 abre/fecha; setas (ou PgUp/PgDn) escolhem, Enter spawna 4 m a frente do jogador.
int gF9Want = -1; // mito pedido pelo menu, que nasce quando o modelo terminar de carregar
bool gMenuOpen = false;
int gSel = 0;
std::string gToast;
unsigned gToastUntil = 0;

// Nome em portugues do bicho: pela pasta (os sem .ifp) ou pelo grupo de animacao original do mito.
std::string Label(const Myth& m) {
    static const std::map<std::string, const char*> byFolder = { { "Anim4", "Vaca" }, { "Anim5", "Vaca" }, { "Anim6", "Avestruz" }, { "Anim7", "Ovelha" },
        { "Anim8", "Pato" }, { "Anim9", "Gorila" }, { "Anim10", "Capivara" }, { "Anim11", "Raposa" }, { "Anim12", "Raposa" }, { "Anim14", "Galinha" } };
    static const std::map<std::string, const char*> byGroup = { { "boar", "Javali" }, { "rabit", "Coelho" }, { "deer1", "Cervo" }, { "lion", "Leao" },
        { "wolf", "Lobo" }, { "bear", "Urso" }, { "dog", "Cao" }, { "cat", "Gato" } };
    auto f = byFolder.find(m.name);
    if (f != byFolder.end()) return std::string(f->second) + " (" + m.name + ")";
    auto g = byGroup.find(mm::lowerStr(m.cfg.customAnimGroup));
    if (g != byGroup.end()) return std::string(g->second) + " (" + m.name + ")";
    return m.name;
}

void Toast(const std::string& t, unsigned now) { gToast = t; gToastUntil = now + 3000; }

void DebugKey(CPed* player, unsigned now) {
    static bool prev[6] = {};
    const int keys[6] = { VK_F9, VK_UP, VK_DOWN, VK_PRIOR, VK_NEXT, VK_RETURN };
    bool down[6], hit[6];
    for (int k = 0; k < 6; ++k) { down[k] = KeyPressed(keys[k]); hit[k] = down[k] && !prev[k]; prev[k] = down[k]; }
    if (hit[0]) gMenuOpen = !gMenuOpen;
    int n = (int)gMyths.size();
    if (gMenuOpen && n) {
        if (hit[1]) gSel = (gSel + n - 1) % n;
        if (hit[2]) gSel = (gSel + 1) % n;
        if (hit[3]) gSel = std::max(0, gSel - 10);
        if (hit[4]) gSel = std::min(n - 1, gSel + 10);
        if (hit[5]) { gF9Want = gSel; Log("menu: pedindo %s", gMyths[gSel].name.c_str()); }
    }
    if (gF9Want < 0) return;
    ModelRef& r = gMyths[gF9Want].models[0];
    if (r.state == 3) { Toast("Falhou ao carregar o modelo de " + Label(gMyths[gF9Want]), now); gF9Want = -1; return; }
    if (!EnsureLoaded(r, now)) return;
    CVector p = player->TransformFromObjectSpace({ 0.0f, 4.0f, -1.0f }); // -1 m: chao sob o jogador
    bool ok = SpawnAt(gF9Want, r, p, 0.0f);
    Log("menu: %s %s", r.name.c_str(), ok ? "spawnado" : "falhou (pool cheio?)");
    Toast((ok ? "Spawnado: " : "Pool cheio, nao spawnou: ") + Label(gMyths[gF9Want]), now);
    gF9Want = -1;
}

void DrawMenu() {
    float sx = RsGlobal.maximumWidth / 640.0f, sy = RsGlobal.maximumHeight / 448.0f;
    auto text = [&](float x, float y, const std::string& t, CRGBA c, float scale = 0.55f) {
        CFont::SetFontStyle(FONT_MENU);
        CFont::SetProportional(true);
        CFont::SetScale(scale * sx, scale * 1.6f * sy);
        CFont::SetColor(c);
        CFont::SetOrientation(ALIGN_LEFT);
        CFont::SetEdge(1);
        CFont::SetDropColor(CRGBA(0, 0, 0, 255));
        CFont::PrintString(x * sx, y * sy, t.c_str());
    };
    unsigned now = CTimer::m_snTimeInMilliseconds;
    if (!gToast.empty() && now < gToastUntil) text(20, 410, gToast, CRGBA(255, 220, 80, 255), 0.6f);
    if (!gMenuOpen) return;
    constexpr int kRows = 12;
    int n = (int)gMyths.size();
    CSprite2d::DrawRect(CRect(14 * sx, 60 * sy, 300 * sx, (60 + 30 + kRows * 17 + 22) * sy), CRGBA(0, 0, 0, 170));
    text(20, 64, "FaunaSA - spawn de teste", CRGBA(255, 255, 255, 255), 0.6f);
    int first = std::clamp(gSel - kRows / 2, 0, std::max(0, n - kRows));
    for (int i = 0; i < kRows && first + i < n; ++i) {
        int k = first + i;
        bool sel = k == gSel;
        text(24, 90 + i * 17, (sel ? "> " : "  ") + Label(gMyths[k]), sel ? CRGBA(120, 255, 120, 255) : CRGBA(220, 220, 220, 255), 0.5f);
    }
    text(20, 90 + kRows * 17 + 4, "Setas/PgUp/PgDn: escolher   Enter: spawnar   F9: fechar   (" + std::to_string(gSel + 1) + "/" + std::to_string(n) + ")", CRGBA(180, 180, 180, 255), 0.4f);
}

// Teste manual: F8 grava no log a posicao do jogador e os 8 mitos com o local mais proximo.
void DebugNearest(CPed* player) {
    static bool prev = false;
    bool down = KeyPressed(VK_F8);
    if (down && !prev) {
        CVector p = player->GetPosition();
        Log("F8: jogador em (%.0f, %.0f, %.0f), hora %d", p.x, p.y, p.z, CClock::ms_nGameClockHours);
        std::vector<std::pair<float, int>> ranked;
        for (size_t i = 0; i < gMyths.size(); ++i) {
            float d;
            if (mm::nearestLocation(gMyths[i].cfg, p.x, p.y, p.z, &d) >= 0) ranked.push_back({ d, (int)i });
        }
        std::sort(ranked.begin(), ranked.end());
        for (size_t k = 0; k < ranked.size() && k < 8; ++k) {
            const Myth& m = gMyths[ranked[k].second];
            Log("  %s (%s): local mais proximo a %.0f m, ativa a %.0f m, horario %d-%d, %d/%d vivos", m.name.c_str(), m.cfg.customAnimGroup.c_str(),
                ranked[k].first, mm::activationRadius(m.cfg), m.cfg.startHour, m.cfg.endHour, LiveOf(ranked[k].second), m.cfg.quantity);
        }
    }
    prev = down;
}

void OnProcess() {
    gMainTid = GetCurrentThreadId();
    gBeat = GetTickCount();
    gStage = "idle";
    if (!gLoaded) { // modelos so existem depois do jogo carregar; resolve uma vez
        gLoaded = true;
        LoadDir("Animals", 'A');
        LoadDir("Myths", 'M');
        Log("mitos com modelos: %zu", gMyths.size());
    }
    // So age com o jogo jogavel: sem cutscene e com o controle do jogador ligado ha pelo menos kReadyMs. Pedir modelos
    // durante a abertura do jogo novo ocupa a memoria de streaming de que a cutscene inicial precisa.
    unsigned now = CTimer::m_snTimeInMilliseconds;
    gLastNowForSpawn = now;
    CPad* pad = CPad::GetPad(0);
    bool ready = pad && !pad->DisablePlayerControls && !CCutsceneMgr::ms_running && !CCutsceneMgr::ms_cutsceneLoadStatus && !CCutsceneMgr::ms_cutsceneProcessing;
    if (!ready) { gReadySince = 0; return; }
    if (!gReadySince) gReadySince = now ? now : 1;
    if (now - gReadySince < kReadyMs) return;
    CPed* player = FindPlayerPed();
    if (!player) return;
    DebugKey(player, now);
    DebugNearest(player);
    gStage = "UpdateAnimals";
    UpdateAnimals(now);
    gStage = "tick";
    if (now < gNextTick) { gStage = "idle"; return; }
    gNextTick = now + kTickMs;
    CVector pl = player->GetPosition();
    gStage = "Despawn"; Despawn(pl, now);
    gStage = "ReleaseUnusedModels"; ReleaseUnusedModels();
    gStage = "TrySpawn";
    for (size_t i = 0; i < gMyths.size(); ++i) { gStageSlot = (int)i; TrySpawn((int)i, pl, now); } // um spawn por mito por tick
    gStage = "idle";
}

void OnRestart() { // novo jogo/load: os ponteiros antigos e os IDs de modelo nao valem mais
    gSlots = {};
    gPool = mm::SlotPool<kMaxLive>{};
    for (auto& m : gMyths) {
        for (auto& r : m.models) r = ModelRef{ r.name };
    }
}

struct Main {
    Main() {
        AddVectoredExceptionHandler(1, CrashLogger);
        gBeat = GetTickCount();
        CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
        Events::gameProcessEvent += OnProcess;
        Events::drawingEvent += DrawMenu;
        Events::restartGameEvent += OnRestart;
    }
} gMain;

} // namespace
