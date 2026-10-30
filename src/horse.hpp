#include "horse_rear.hpp"
// Depuracao do tiro: guarda o ultimo disparo para desenhar marcadores na tela (DrawShotDebug, chamado de DrawMenu) por 4 s.
struct ShotDbg { CVector muzzle, tgt, gameTgt; unsigned at = 0; } gShotDbg;
void DrawShotDebug();
float gAimFov = 0.0f; // campo de visao desenhado (apos o zoom da mira), para tracar a mira do tiro
constexpr float kRearBack = 0.3f; // fracao do recuo da tabela do Gun (horse_rear.hpp) aplicada ao CJ no empinar
// Cavalo: adaptacao do "New Horse Mod" de nadalao (cavalo.cs) para o ASI. Incluido DENTRO do namespace anonimo de main.cpp
// (usa Log, ModelRef, EnsureLoaded, BuildHierarchy, Toast...). Assets: modloader\FaunaSA\gta3.img\cavalo.dff/.txd (modelo) e
// FaunaSA\horse\horse_animation.ifp + cavalo*.mp3. Regras e numeros tirados do script original (decompilado):
// 9 cavalos em locais fixos (nascem a 100 m do jogador, somem a 140 m depois de 60 s), vida 1500, montar a 3 m com a tecla de
// entrar no veiculo, esticar o analogico para andar (anda -> corre), tecla de correr = galope, pulo, sons de casco e relincho.

struct HorseSpot { float x, y, z, heading; };
constexpr HorseSpot kHorseSpots[] = {
    { 2562.5f, -957.2f, 82.2f, 180.0f }, { -349.2f, -1062.4f, 58.7f, 0.0f }, { 1070.7f, -287.6f, 73.4f, 180.0f },
    { 1374.7f, 331.6f, 18.8f, 310.7f }, { -79.1f, 47.0f, -100.0f, 157.7f }, { -1110.8f, -1660.2f, 75.8f, 263.7f },
    { -368.0f, 2228.3f, 41.9f, 106.5f }, { -1506.7f, 1976.2f, 47.9f, 180.0f }, { -803.9f, 2760.8f, 45.3f, 180.0f } };
constexpr int kHorseSpotN = 9, kHorseManual = 9, kHorseN = 10;
constexpr float kHorseSpawnDist = 100.0f, kHorseDespawnDist = 140.0f, kHorseMountDist = 3.0f;
constexpr float kHorseCenter = 0.95f; // centro do ped acima do chao (os cascos ficam em -0,93 na pose parada)
constexpr unsigned kHorseFarMs = 60000, kMountMs = 1070, kDismountMs = 1170;
constexpr float kHorseWalk = 0.93f, kHorseRun = 4.0f, kHorseSprint = 7.23f; // m/s, velocidade do solo das animacoes do Gun (tools/gun_to_ifp.py --native)
// Velocidade real do cavalo: mais rapida que a do ciclo; a animacao toca acelerada (ate 1,6x) e o pe escorrega um pouco, melhor que um cavalo lento
constexpr float kHorseRunSpeed = 5.5f, kHorseSprintSpeed = 11.0f;
const char* const kHorseDir = "FaunaSA\\horse\\";

enum class HState { Idle, Mounting, Riding, Dismounting };

struct Horse {
    CPed* ped = nullptr;
    HState st = HState::Idle;
    float x = 0, y = 0, z = 0, heading = 0, speed = 0, vz = 0, groundZ = 0, boneRest = 0, boneRestFwd = 0, tnx = 0, tny = 0, tnz = 1; // tn: normal do terreno (suavizada)
    bool boneRestSet = false, boneMoves = false, approaching = false;
    bool jumping = false, stepSounded = false;
    unsigned t0 = 0, jumpT0 = 0, idleT0 = 0, nextKick = 0, kickUntil = 0, deadSince = 0, farSince = 0, exitT0 = 0;
};

std::array<Horse, kHorseN> gHorses;
// Pelagens do Gun (mesma malha e esqueleto, outro TXD): cinza, appaloosa, baio e pinto. As 4 ultimas sao opcionais (se faltarem no img, usa a cinza).
constexpr int kHorseCoats = 5; // a ultima (cavalo do Reverendo Reed, modelo a parte) so sai pelo F9
const char* const kHorseCoatName[kHorseCoats] = { "cavalo", "cavalo1", "cavalo2", "cavalo3", "cavalo4" };
const char* const kHorseCoatLabel[kHorseCoats] = { "Cinza", "Appaloosa", "Baio", "Pinto", "do Reed" };
std::array<ModelRef, kHorseCoats> gHorseModels{ ModelRef{ "cavalo" }, ModelRef{ "cavalo1" }, ModelRef{ "cavalo2" }, ModelRef{ "cavalo3" }, ModelRef{ "cavalo4" } };
int gManualCoat = 0;
std::map<std::string, CAnimBlendHierarchy*> gHorseAnim; // nome do bloco em minusculas
bool gHorseOn = true, gHorseInit = false, gRiding = false, gAiming = false;
float gRideSpeed = 0.0f; // m/s do cavalo montado (0 sem cavaleiro): o FOV abre com a velocidade
bool gAlignGun = true; // FaunaSA.ini [Horse] AlignGun=0 desliga a correcao do cano (veja AlignGun)
float gGunOff = 0.0f;
float gAimLimit = 0.0f; // rad para cada lado do cavalo; FaunaSA.ini [Horse] AimLimit = graus no total (150). 0 (padrao) = nao mexe na camera
unsigned gHintAt = 0; // proxima vez que a dica de montar pode aparecer
int gRider = -1;

CAnimBlendHierarchy* HA(const char* n) { auto it = gHorseAnim.find(n); return it == gHorseAnim.end() ? nullptr : it->second; }

void InitHorse() {
    gHorseInit = true;
    mm::Ini ini;
    ini.load("FaunaSA.ini");
    gHorseOn = ini.num("Horse", "Enabled", 1) != 0;
    gAlignGun = ini.num("Horse", "AlignGun", 1) != 0;
    gAimLimit = ini.num("Horse", "AimLimit", 0) * 0.5f * 3.14159265f / 180.0f;
    if (!gHorseOn) { Log("cavalo: desligado no FaunaSA.ini"); return; }
    mm::IfpBlock blk;
    std::string path = std::string(kHorseDir) + "horse_animation.ifp";
    if (!mm::readFile(path, blk.raw) || !mm::parseAnp3(blk)) { Log("cavalo: %s nao carregou (%s); cavalo desligado", path.c_str(), blk.error.c_str()); gHorseOn = false; return; }
    if (!CStreaming::ms_pExtraObjectsDir || !CStreaming::ms_pExtraObjectsDir->FindItem("cavalo")) {
        Log("cavalo: modelo 'cavalo' nao esta registrado (modloader\\FaunaSA\\gta3.img\\cavalo.dff/.txd); cavalo desligado");
        gHorseOn = false;
        return;
    }
    int pedBlock = CAnimManager::GetAnimationBlockIndex("ped");
    for (auto& a : blk.anims) {
        std::string n = mm::lowerStr(a.name);
        bool boarding = n.rfind("jump_on", 0) == 0 || n.rfind("exit_", 0) == 0; // so estes deslocam a raiz de verdade
        bool rider = boarding || (n.rfind("sit", 0) == 0 && n.rfind("sitl", 0) != 0); // sitl_* = so as pernas, a mira usa por cima da base do jogo: sem completar ossos
        mm::IfpAnim use = a;
        // As animacoes do cavaleiro nao tem os ossos do rosto (6 sobrancelha E, 7 sobrancelha D, 8 mandibula), nem 26/36 (dedos),
        // 201 (barriga) e 301/302 (peito). Com as animacoes normais do CJ zeradas, esses ossos ficariam sem nenhuma animacao e o rosto
        // deforma: completa com a pose de repouso do esqueleto padrao (constante durante o ciclo).
        static const struct { int tag; const char* name; float q[4]; } kFill[] = {
            { 6, "L Brow", { 0.039f, -0.068f, 0.649f, 0.756f } }, { 7, "R Brow", { -0.040f, 0.046f, 0.650f, 0.757f } }, { 8, "Jaw", { 0.0f, 0.0f, 0.823f, 0.568f } },
            { 26, "R Finger01", { 0.0f, 0.0f, 0.334f, 0.942f } }, { 36, "L Finger01", { 0.0f, 0.0f, 0.334f, 0.942f } }, { 201, "Belly", { 0.0f, 0.0f, 0.695f, 0.719f } },
            { 301, "R breast", { -0.051f, -0.022f, 0.670f, 0.740f } }, { 302, "L breast", { 0.110f, 0.073f, 0.646f, 0.752f } } };
        std::vector<std::array<float, 10>> fillData;
        if (rider) {
            float dur = 0.0f;
            for (const mm::IfpSeq& q : a.seqs) {
                size_t ks = mm::keyFrameSize(q.frameType);
                if (q.numFrames && (q.frameType == 3 || q.frameType == 4)) { int16_t t; std::memcpy(&t, q.frames + (q.numFrames - 1) * ks + 8, 2); dur = std::max(dur, t / 60.0f); }
            }
            fillData.reserve(8);
            for (const auto& f : kFill) {
                bool has = false;
                for (const mm::IfpSeq& q : a.seqs) has |= q.boneTag == f.tag;
                if (has) continue;
                fillData.push_back({ f.q[0], f.q[1], f.q[2], f.q[3], 0.0f, f.q[0], f.q[1], f.q[2], f.q[3], dur });
                mm::IfpSeq sq;
                sq.name = f.name; sq.frameType = 1; sq.numFrames = 2; sq.boneTag = f.tag; sq.frameBytes = 40;
                sq.frames = (const uint8_t*)fillData.back().data();
                use.seqs.push_back(sq);
            }
        }
        gHorseAnim[n] = BuildHierarchy(use, pedBlock < 0 ? 0 : pedBlock, !boarding);
    }
    for (const char* need : { "horse_idle", "horse_idle_1", "horse_walk", "horse_run", "horse_sprint", "horse_kick_forward", "sit", "sit_walk", "sit_run", "sit_galop", "jump_on_right", "exit_right" })
        if (!HA(need)) Log("cavalo: animacao '%s' faltando em horse_animation.ifp", need);
    Log("cavalo: %zu animacoes carregadas, %d locais", gHorseAnim.size(), kHorseSpotN);
}

void ClearReactionTasks(CPed* p) {
    if (!p->m_pIntelligence) return;
    CTaskManager& tm = p->m_pIntelligence->m_TaskMgr;
    if (tm.m_aPrimaryTasks[TASK_PRIMARY_PHYSICAL_RESPONSE]) tm.SetTask(nullptr, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    if (tm.m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_TEMP] || tm.m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_NONTEMP]) tm.ClearTaskEventResponse();
}

// Som do cavalo: volume do menu de efeitos x distancia (cheio ate 8 m, zero em 100 m, como o original).
void HorseSound(const char* file, const CVector& pos) {
    CPed* pl = FindPlayerPed();
    if (!pl) return;
    CVector d = pos - pl->GetPosition();
    snd::play(std::string(kHorseDir) + file, snd::sfxVolume() * snd::falloff(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 8.0f, 100.0f));
}

// O controle do jogador fica LIGADO enquanto monta (camera livre, mira e tiro nativos); so a posicao e a sela sao nossas.
bool IsRiderHier(const CAnimBlendHierarchy* h);
void FireMounted(CPed* player, CPad* pad, unsigned now);
void AimPose(CPed* player, unsigned now);
void AimPoseOff(CPed* player);

void SetRiderFree(CPed* player) {
    if (player->m_pRwClump)
        for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(player->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a))
            if (IsRiderHier(a->m_pHierarchy)) { a->m_nFlags &= ~ANIMATION_LOOPED; a->m_fBlendDelta = -8.0f; }
    // KeepOnly zerou as animacoes do proprio CJ: sem uma base entrando, o peso total cai abaixo de 1 e o corpo desaba ("deita") ao sair da sela
    if (player->m_pRwClump && player->m_fHealth > 0.0f) CAnimManager::BlendAnimation(player->m_pRwClump, player->m_nAnimGroup, PED_ANIM_IDLE_STANCE, 8.0f);
    player->bUsesCollision = true;
    player->bApplyGravity = true;
    player->bDontApplySpeed = false;
    player->bNotAllowedToDuck = false;
    CPad::GetPad(0)->bDisablePlayerDuck = 0;
    gAiming = false;
    gRiding = false; gRider = -1;
}

void RemoveHorse(int i) {
    Horse& h = gHorses[i];
    if (h.ped && CPools::ms_pPedPool->IsObjectValid(h.ped)) CPopulation::RemovePed(h.ped);
    if (gRider == i) { if (CPed* pl = FindPlayerPed()) SetRiderFree(pl); else { gRiding = false; gRider = -1; } } // nunca deixa o jogador preso
    h = Horse{};
}

bool SpawnHorse(int i, float x, float y, float groundZ, float headingDeg, unsigned now, int coat = 0) {
    if (coat < 0 || coat >= kHorseCoats || !CStreaming::ms_pExtraObjectsDir || !CStreaming::ms_pExtraObjectsDir->FindItem(kHorseCoatName[coat])) coat = 0;
    ModelRef& model = gHorseModels[coat];
    if (!EnsureLoaded(model, now)) return false;
    auto* ped = new CCivilianPed(PED_TYPE_MISSION1, model.id);
    if (!ped) return false;
    ped->m_nCreatedBy = 2; // PED_MISSION: o gerenciador de populacao nao recicla
    ped->m_fHealth = 1500.0f;
    // dano nao pode disparar as reacoes de ped humano (cair, ragdoll): o cavalo deformava. Fica imune a tudo.
    ped->bNeverEverTargetThisPed = true; // o cavalo e um ped colado no CJ: sem isso a mira trava nele (mira torta) e o botao de tiro vira soco
    ped->bBulletProof = ped->bFireProof = ped->bExplosionProof = ped->bCollisionProof = ped->bMeleeProof = true;
    ped->SetPosition(x, y, groundZ + kHorseCenter);
    ped->SetHeading(DegToRad(headingDeg));
    ped->m_fHeadingCurrent = ped->m_fHeadingGoal = DegToRad(headingDeg);
    CWorld::Add(ped);
    Horse& h = gHorses[i];
    h = Horse{};
    h.ped = ped; h.x = x; h.y = y; h.z = groundZ + kHorseCenter; h.groundZ = groundZ; h.heading = DegToRad(headingDeg);
    h.idleT0 = now; h.nextKick = now + 8000 + (unsigned)(20000 * mm::rnd01());
    Log("cavalo %d: nasceu em (%.1f, %.1f, %.1f)", i, x, y, groundZ);
    return true;
}

// Poe o jogador ao lado/na sela do cavalo: origem no cavalo (+ dx para a direita dele); a raiz das animacoes do pacote ja sobe e
// recua o corpo ate a sela (sit: 0,55 m acima e 0,33 m atras).
// Eixos do cavalo inclinados pelo terreno: direita, frente e cima (cima = normal suavizada do chao; plano, e o rumo puro).
struct HorseAxes { float r[3], f[3], u[3]; };
HorseAxes AxesOf(const Horse& h, float heading) {
    float fx = -std::sin(heading), fy = std::cos(heading);
    HorseAxes a{};
    a.u[0] = h.tnx; a.u[1] = h.tny; a.u[2] = h.tnz;
    a.r[0] = fy * a.u[2]; a.r[1] = -fx * a.u[2]; a.r[2] = fx * a.u[1] - fy * a.u[0]; // frente0 x cima
    float l = std::sqrt(a.r[0] * a.r[0] + a.r[1] * a.r[1] + a.r[2] * a.r[2]);
    for (float& c : a.r) c /= l;
    a.f[0] = a.u[1] * a.r[2] - a.u[2] * a.r[1]; a.f[1] = a.u[2] * a.r[0] - a.u[0] * a.r[2]; a.f[2] = a.u[0] * a.r[1] - a.u[1] * a.r[0]; // cima x direita
    return a;
}
void TiltPed(CPed* p, const HorseAxes& a) {
    CMatrix& m = *p->m_matrix;
    m.right = CVector(a.r[0], a.r[1], a.r[2]); m.up = CVector(a.f[0], a.f[1], a.f[2]); m.at = CVector(a.u[0], a.u[1], a.u[2]);
}

void PlaceRider(CPed* player, const Horse& h, float dx, float lift = 0.0f, float back = 0.0f) {
    // dx: para a direita do cavalo; back: para tras; lift: para cima (no referencial inclinado do cavalo)
    HorseAxes a = AxesOf(h, h.heading);
    float up = 0.05f + lift;
    player->SetPosition(h.x + a.r[0] * dx - a.f[0] * back + a.u[0] * up, h.y + a.r[1] * dx - a.f[1] * back + a.u[1] * up, h.z + a.r[2] * dx - a.f[2] * back + a.u[2] * up);
    if (!gAiming) { // mirando, o CJ gira pela camera; senao olha para onde o cavalo vai
        player->SetHeading(h.heading);
        player->m_fHeadingCurrent = player->m_fHeadingGoal = h.heading;
        TiltPed(player, a);
    } else {
        // mirando o jogo so atualiza o rumo (m_fHeadingCurrent); a matriz do ped so e refeita quando ele esta "no controle", e com colisao,
        // gravidade e velocidade desligados isso pode nao acontecer: o corpo ficaria no rumo antigo e o braco apontaria para o lado
        player->SetHeading(player->m_fHeadingCurrent);
    }
    player->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
}

CAnimBlendAssociation* Play(CPed* p, CAnimBlendHierarchy* a, float blend = 6.0f) {
    return a ? CAnimManager::BlendAnimation(p->m_pRwClump, a, ANIMATION_LOOPED, blend) : nullptr;
}

// O cavalo e um ped: o jogo toca por baixo a animacao de ped dele (idle do CJ), que com o esqueleto do cavalo deforma a malha. Zera tudo que
// nao e nosso, e a primeira animacao entra com peso cheio (com o idle do CJ zerado, o peso total seria menor que 1 e o corpo desabaria).
bool IsHorseAnim(const CAnimBlendHierarchy* h) {
    for (const auto& kv : gHorseAnim) if (kv.second == h) return true;
    return false;
}
CAnimBlendAssociation* PlayHorse(CPed* p, CAnimBlendHierarchy* a, float blend = 4.0f) {
    bool first = true;
    for (CAnimBlendAssociation* o = RpAnimBlendClumpGetFirstAssociation(p->m_pRwClump); o; o = RpAnimBlendGetNextAssociation(o))
        if (IsHorseAnim(o->m_pHierarchy)) first = false;
    CAnimBlendAssociation* as = Play(p, a, first ? 1000.0f : blend);
    if (as && first) as->m_fBlendAmount = 1.0f;
    for (CAnimBlendAssociation* o = RpAnimBlendClumpGetFirstAssociation(p->m_pRwClump); o; o = RpAnimBlendGetNextAssociation(o))
        if (o != as && !IsHorseAnim(o->m_pHierarchy)) { o->m_fBlendAmount = 0.0f; o->m_fBlendDelta = 0.0f; }
    return as;
}

// Com o controle ligado a tarefa do jogador volta a tocar andar/correr a cada frame e as pernas do CJ "caminham" na sela. Depois de
// tocar a nossa animacao, zera o peso das demais (mantem as parciais: mira e tiro).
// Animacoes proprias do CJ que nao combinam com a sela: pulo e queda (o CJ "pulava" sozinho quando o cavalo pulava).
bool IsBlockedAnim(unsigned h) {
    static const unsigned kBlocked[] = { 0x6849FD6C, 0x17446AFD, 0x634C3C4E, 0xB202B300, 0xF9D30C03, 0xFCBA330B, 0xAFEA39DF, 0x9BE6FB94, 0x89F4D2FC, 0x6A242D30, 0x90806F2A, 0xA8D8677D, 0x0AE1D185 };
    for (unsigned b : kBlocked) if (b == h) return true;
    return false;
}

// Nossas animacoes de cavaleiro que ainda estao saindo na mistura: zera-las afundaria o CJ no cavalo (o peso total cai abaixo de 1 e a
// raiz, que o poe na sela, encolhe junto) nos primeiros frames de cada animacao nova.
bool IsRiderHier(const CAnimBlendHierarchy* h) {
    for (const auto& kv : gHorseAnim)
        if (kv.second == h && (kv.first.rfind("sit", 0) == 0 || kv.first.rfind("jump_on", 0) == 0 || kv.first.rfind("exit_", 0) == 0)) return true;
    return false;
}

bool IsLegsHier(const CAnimBlendHierarchy* h) {
    for (const auto& kv : gHorseAnim) if (kv.second == h && kv.first.rfind("sitl", 0) == 0) return true;
    return false;
}

// Zera o peso das animacoes do CJ que brigam com a sela. onlyMovement: so as de andar/correr e as bloqueadas (mirando, o resto fica).
void Suppress(CPed* p, CAnimBlendAssociation* keep, bool onlyMovement) {
    if (!p->m_pRwClump) return;
    for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(p->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a)) {
        bool blocked = a->m_pHierarchy && IsBlockedAnim(a->m_pHierarchy->m_hashKey);
        bool partial = (a->m_nFlags & ANIMATION_PARTIAL) != 0, moving = (a->m_nFlags & ANIMATION_MOVEMENT) != 0;
        if (a == keep || IsRiderHier(a->m_pHierarchy)) continue;
        // sem mirar, as parciais da arma (Gun_2_IDLE etc.) sobrepoem a sela e deitam o CJ ao trocar de arma: zeradas tambem
        if (onlyMovement ? !(moving || blocked) : (gAiming && !blocked && partial && !moving)) continue;
        a->m_fBlendAmount = 0.0f;
        a->m_fBlendDelta = 0.0f;
    }
}
void KeepOnly(CPed* p, CAnimBlendAssociation* keep) { if (keep) Suppress(p, keep, false); }
void ZeroBlocked(CPed* p) {
    if (!p->m_pRwClump) return;
    for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(p->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a))
        if (a->m_pHierarchy && IsBlockedAnim(a->m_pHierarchy->m_hashKey)) { a->m_fBlendAmount = 0.0f; a->m_fBlendDelta = 0.0f; }
}

// Diagnostico (ate 300 linhas): o que o jogo esta tocando no CJ montado, para achar quem deita/derruba o corpo.
void LogRiderDiag(unsigned now, CPed* p, const Horse& h, bool aiming) {
    static unsigned lastLog = 0;
    static int lines = 0;
    if (lines > 300 || now - lastLog < 300 || !p->m_pRwClump) return;
    lastLog = now;
    CVector pp = p->GetPosition();
    Log("RIDER t=%u aim=%d speed=%.1f dz(cj-cavalo)=%.2f estado=%d move=%d", now, (int)aiming, h.speed, pp.z - h.z, (int)p->m_ePedState, (int)p->m_nMoveState);
    for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(p->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a)) {
        if (a->m_fBlendAmount < 0.01f && a->m_fBlendDelta <= 0.0f) continue;
        Log("   assoc hash=%08X flags=%04X blend=%.2f delta=%.2f tempo=%.2f vel=%.2f", a->m_pHierarchy ? a->m_pHierarchy->m_hashKey : 0, (unsigned)a->m_nFlags, a->m_fBlendAmount, a->m_fBlendDelta, a->m_fCurrentTime, a->m_fSpeed);
        ++lines;
    }
    ++lines;
}

// Mirando parado o jogo toca a base de mira (gun_stand) + o tiro parcial; mirando andando troca a base por animacoes de passo (gunmove, nao
// parciais, flag de movimento) que mexem as pernas. Aqui, andando, continuamos tocando a ultima base de mira PARADA (ou gun_stand) e zerando as de
// passo: a pose do tronco e a mesma e as pernas ficam paradas. O cavalo ja anda por conta propria.
void KeepAimBase(CPed* p, bool aiming) {
    static CAnimBlendHierarchy* base = nullptr;
    static int wtype = 0;
    if (!aiming || !p->m_pRwClump) { base = nullptr; return; }
    int wt = (int)p->GetWeapon()->m_eWeaponType;
    if (wt != wtype) { base = nullptr; wtype = wt; } // outra arma, outro bloco de animacao
    CAnimBlendAssociation* best = nullptr;
    bool moving = false;
    for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(p->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a)) {
        if (a->m_nFlags & ANIMATION_PARTIAL) continue;
        if (a->m_nFlags & ANIMATION_MOVEMENT) { if (a->m_fBlendAmount > 0.05f) moving = true; continue; }
        if (IsRiderHier(a->m_pHierarchy) || (a->m_pHierarchy && IsBlockedAnim(a->m_pHierarchy->m_hashKey))) continue;
        if (a->m_fBlendAmount > 0.9f && (!best || a->m_fBlendAmount > best->m_fBlendAmount)) best = a;
    }
    if (!moving) { if (best && best->m_pHierarchy) base = best->m_pHierarchy; return; }
    static CAnimBlendHierarchy* gunStand = nullptr;
    static bool tried = false;
    if (!tried) { tried = true; if (CAnimBlock* b = CAnimManager::GetAnimationBlock("ped")) gunStand = CAnimManager::GetAnimation(0x02E0E120u, b); } // "Gun_stand"
    CAnimBlendHierarchy* use = base ? base : gunStand;
    if (!use) return;
    CAnimManager::BlendAnimation(p->m_pRwClump, use, ANIMATION_LOOPED, 8.0f);
    Suppress(p, nullptr, true);
}

// Compatibilidade com pacotes de animacao de arma (Enhanced Pistol/Shotgun/Rifle/Smg Animation etc.): as animacoes de tiro deles apontam o cano
// alguns graus para o lado em relacao a pose original (medido: a do python do Enhanced Pistol aponta 45 graus a esquerda da original). No jogo a
// correcao vem do IK do braco; montado, o cano aparece torto (a bala sai certa, a mira do jogo nao muda). Aqui se mede para onde o cano aponta
// (da mao ate a boca, como o jogo calcula o tiro) em relacao ao corpo e se gira SO o desenho do corpo (matriz) para o cano coincidir com a mira.
void AlignGun(CPed* player, bool aiming) {
    static float off = 0.0f;
    static bool had = false;
    if (!aiming || !gAlignGun) { had = false; gGunOff = 0.0f; return; }
    int type = (int)player->GetWeapon()->m_eWeaponType;
    CWeaponInfo* wi = type ? CWeaponInfo::GetWeaponInfo(player->GetWeapon()->m_eWeaponType, 0) : nullptr;
    if (!wi || !player->m_matrix) return;
    RwV3d hand{}, muzzle{ wi->m_vecFireOffset.x, wi->m_vecFireOffset.y, wi->m_vecFireOffset.z };
    player->GetBonePosition(hand, 24, true);
    player->GetTransformedBonePosition(muzzle, 24, false);
    float dx = muzzle.x - hand.x, dy = muzzle.y - hand.y;
    if (std::hypot(dx, dy) < 0.05f) return;
    float gunYaw = std::atan2(-dx, dy);
    float body = std::atan2(-player->m_matrix->up.x, player->m_matrix->up.y);
    float rel = WrapAngle(gunYaw - body); // desvio do cano em relacao ao corpo
    off = had ? off + WrapAngle(rel - off) * 0.3f : rel;
    had = true;
    gGunOff = off = std::clamp(off, -1.6f, 1.6f);
    if (std::fabs(off) > 0.05f) player->SetHeading(WrapAngle(player->m_fHeadingGoal - off));
}

// Mira do cavalo limitada a 150 graus na frente (75 para cada lado do cavalo). O jogo gira o CJ pela camera; aqui se aprende a relacao
// angulo-da-camera x rumo-do-CJ (sentido incluso) e, se o rumo passar do limite, empurra a camera de volta. Registra a calibracao no log.
void ClampAim(CPed* player, float horseHeading, unsigned now, bool aiming) {
    static float prevCam = 0, prevHead = 0, sign = 0;
    static bool havePrev = false, was = false;
    static unsigned startAt = 0;
    static int logged = 0;
    if (!aiming) { was = false; havePrev = false; return; }
    CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
    {   // diagnostico da mira (ate 200 linhas, a cada 250 ms): camera, rumo do CJ e do cavalo, FOV
        static unsigned lastDiag = 0;
        static int diagLines = 0;
        if (diagLines < 200 && now - lastDiag > 250) {
            lastDiag = now; ++diagLines;
            // rumo real do corpo (da matriz) e para onde o braco direito aponta (ombro -> mao), relativos ao rumo do jogo
            float matHead = player->m_matrix ? std::atan2(-player->m_matrix->up.x, player->m_matrix->up.y) : 0.0f;
            Log("AIMDIAG arma=%d cam=%.2f cjGoal=%.2f cjCur=%.2f matriz=%.2f cavalo=%.2f rel=%.2f desvio_do_cano=%.2f modo=%d fov=%.1f", (int)player->GetWeapon()->m_eWeaponType, cam.m_fHorizontalAngle,
                player->m_fHeadingGoal, player->m_fHeadingCurrent, matHead, horseHeading, WrapAngle(player->m_fHeadingGoal - horseHeading), gGunOff, (int)cam.m_nMode, CDraw::ms_fFOV);
        }
    }
    // Escrever o angulo da camera depois que o jogo ja calculou o alvo da mira deixa o braco do CJ apontando para um lado e a camera para outro
    // (o braco usa o alvo calculado antes do ajuste). Por isso so mexe se AimLimit > 0 (experimental).
    if (gAimLimit <= 0.0f) return;
    // Ao comecar a mirar a camera do jogo girava ate ~80 graus nos primeiros frames (visto no log: camera - rumo do CJ = -1,48 rad, constante).
    // Por 0,4 s a mira e mantida na frente do cavalo; depois o mouse manda.
    if (!was) { was = true; startAt = now; }
    if (now - startAt < 400) {
        cam.m_fHorizontalAngle = WrapAngle(horseHeading - 1.48f);
        player->m_fHeadingCurrent = player->m_fHeadingGoal = horseHeading;
        havePrev = false;
        return;
    }
    float c = cam.m_fHorizontalAngle, hd = player->m_fHeadingGoal;
    if (havePrev) {
        float dc = WrapAngle(c - prevCam), dh = WrapAngle(hd - prevHead);
        if (sign == 0.0f && std::fabs(dc) > 0.03f && std::fabs(dh) > 0.03f) { sign = dc * dh > 0 ? 1.0f : -1.0f; Log("mira: sentido camera/CJ = %+.0f (dcam=%.2f dcj=%.2f)", sign, dc, dh); }
    }
    havePrev = true; prevCam = c; prevHead = hd;
    float rel = WrapAngle(hd - horseHeading);
    if (logged < 40 && std::fabs(rel) > 0.2f) { ++logged; Log("mira: cam=%.2f cj=%.2f cavalo=%.2f rel=%.2f sinal=%+.0f", c, hd, horseHeading, rel, sign); }
    if (sign != 0.0f) cam.m_fHorizontalAngle = WrapAngle(c - sign * mm::aimExcess(rel, gAimLimit));
}

CAnimBlendAssociation* PlayRider(CPed* p, CAnimBlendHierarchy* a, float blend) {
    // Entrando com outra animacao de cavaleiro ja tocando (troca de marcha), mistura de verdade: o peso total fica em 1 e o CJ nao afunda.
    // Sem nenhuma, quase instantanea: com o peso total abaixo de 1 o corpo afundaria no cavalo.
    bool haveRider = false;
    for (CAnimBlendAssociation* o = RpAnimBlendClumpGetFirstAssociation(p->m_pRwClump); o; o = RpAnimBlendGetNextAssociation(o))
        if (IsRiderHier(o->m_pHierarchy) && o->m_pHierarchy != a && o->m_fBlendAmount > 0.5f) haveRider = true;
    CAnimBlendAssociation* as = Play(p, a, haveRider ? blend : 30.0f);
    KeepOnly(p, as);
    return as;
}

void UpdateHorse(int i, unsigned now, float dt, CPed* player, CPad* pad, bool useHit, bool jumpHit, bool kickHit) {
    Horse& h = gHorses[i];
    CPed* ped = h.ped;
    if (!ped || !CPools::ms_pPedPool->IsObjectValid(ped) || !ped->m_pRwClump) { if (ped) RemoveHorse(i); return; }
    CVector hp = ped->GetPosition();
    if (ped->m_fHealth <= 0.0f) { // morreu: solta o cavaleiro e some depois de 10 s
        if (gRider == i) SetRiderFree(player);
        if (!h.deadSince) h.deadSince = now;
        if (now - h.deadSince > kCorpseMs) RemoveHorse(i);
        return;
    }
    ClearReactionTasks(ped);
    ped->bUsesCollision = h.st == HState::Idle; // montado, o cavalo sai da colisao: senao a camera de mira encosta nele e chega muito perto
    float pd = std::hypot(hp.x - player->GetPosition().x, hp.y - player->GetPosition().y);

    // ---- sem cavaleiro: fica no lugar, alterna idle/idle_1 e de vez em quando coiceia
    if (h.st == HState::Idle) {
        CAnimBlendHierarchy* a = HA("horse_idle");
        if (now < h.kickUntil) a = HA("horse_kick_forward");
        else if ((now - h.idleT0) % 5700 >= 4500) a = HA("horse_idle_1");
        if (now >= h.nextKick) {
            h.kickUntil = now + 1130;
            h.nextKick = now + 15000 + (unsigned)(25000 * mm::rnd01());
            HorseSound("cavalo2.mp3", hp);
            a = HA("horse_kick_forward");
        }
        PlayHorse(ped, a);
        ped->SetPosition(h.x, h.y, h.z);
        ped->SetHeading(h.heading);
        ped->m_fHeadingCurrent = ped->m_fHeadingGoal = h.heading;
        ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        // montar: a pe, a 3 m, tecla de entrar no veiculo
        if (!gRiding && !player->bInVehicle && player->m_fHealth > 0.0f && pd < kHorseMountDist) {
            if (!h.approaching && now >= gHintAt) { // caixa de ajuda preta do proprio jogo; repete a cada 9 s enquanto estiver perto
                CHud::SetHelpMessage("Aperte ~k~~VEHICLE_ENTER_EXIT~ para montar no cavalo.", true, false, false);
                gHintAt = now + 9000;
            }
            // O CJ anda ate o ponto de subida (1 m ao lado do cavalo, o mesmo onde a animacao de montar comeca) em vez de se teletransportar
            if (useHit && !h.approaching) { h.approaching = true; h.t0 = now; gHintAt = 0; Log("cavalo %d: CJ indo ate o cavalo", i); }
            bool arrived = false;
            if (h.approaching) {
                CVector pp2 = player->GetPosition();
                float tx = h.x + std::cos(h.heading), ty = h.y + std::sin(h.heading);
                float ex = tx - pp2.x, ey = ty - pp2.y, ed = std::hypot(ex, ey);
                if (ed < 0.3f || now - h.t0 > 4000) arrived = true;
                else {
                    pad->NewState.LeftStickX = 0; pad->NewState.LeftStickY = 0; // o jogo nao empurra o CJ para outro lado
                    float step = std::min(ed, 1.7f * dt), hd = std::atan2(-ex, ey);
                    player->SetPosition(pp2.x + ex / ed * step, pp2.y + ey / ed * step, pp2.z);
                    player->SetHeading(hd); player->m_fHeadingCurrent = player->m_fHeadingGoal = hd;
                    // A tarefa do jogador volta a tocar o idle a cada frame (peso dividido, o CJ deslizava parado): andar entra com peso cheio
                    // e as demais animacoes do CJ ficam zeradas
                    if (CAnimBlendAssociation* wa = CAnimManager::BlendAnimation(player->m_pRwClump, player->m_nAnimGroup, 0 /* ANIM_ID_WALK */, 8.0f)) {
                        wa->m_fBlendAmount = 1.0f; wa->m_fBlendDelta = 0.0f;
                        KeepOnly(player, wa);
                    }
                }
            }
            if (arrived) {
                h.approaching = false;
                player->bUsesCollision = false;
                player->bApplyGravity = false; // senao o CJ "quica" para baixo do cavalo: a gravidade puxa entre um SetPosition e outro
                player->bDontApplySpeed = true;
                gRiding = true; gRider = i;
                h.st = HState::Mounting; h.t0 = now;
                gHintAt = 0;
                Log("cavalo %d: montando", i);
            }
        } else {
            h.approaching = false;
            if (pd > 6.0f) gHintAt = 0; // longe: a dica volta a aparecer quando chegar perto de novo
        }
        return;
    }

    // ---- com cavaleiro
    if (player->m_fHealth <= 0.0f || player->bInVehicle) { SetRiderFree(player); h.st = HState::Idle; h.speed = 0; return; }
    // agachar montado "deita" o CJ e o jogo para de criar o comando de tiro (log: agachado=1, cmd=255): a tecla e a tarefa de agachar ficam desligadas
    player->bNotAllowedToDuck = true;
    CPad::GetPad(0)->bDisablePlayerDuck = 1;
    if (player->bIsDucking) { player->m_pIntelligence->ClearTaskDuckSecondary(); player->bIsDucking = false; }
    // montado so se usa a mao (slot 0) e a pistola (slot 2): outra arma escolhida volta para a pistola (ou para a mao, sem pistola)
    if (player->m_pPlayerData && player->m_nSelectedWepSlot != 0 && player->m_nSelectedWepSlot != 2) {
        int to = player->m_aWeapons[2].m_eWeaponType ? 2 : 0;
        player->SetCurrentWeapon(to);
        player->m_pPlayerData->m_nChosenWeapon = (unsigned char)to;
    }
    player->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    if (player->m_pPlayerData) player->m_pPlayerData->m_fTimeCanRun = 1000.0f; // correr/galopar nao cansa o CJ (ele caia da sela)
    if (h.st == HState::Mounting) {
        Play(ped, HA("horse_idle"));
        ped->SetPosition(h.x, h.y, h.z);
        PlaceRider(player, h, 1.0f);
        PlayRider(player, HA("jump_on_right"), 8.0f);
        if (now - h.t0 >= kMountMs) {
            h.st = HState::Riding; h.t0 = now; Log("cavalo %d: montado", i);
            CHud::SetHelpMessage("~k~~VEHICLE_ENTER_EXIT~ desmontar~n~~k~~PED_SPRINT~ galopar~n~~k~~PED_JUMPING~ pular~n~~k~~PED_DUCK~ coice (parado)~n~Mire e atire normalmente.", true, false, false);
        }
        return;
    }
    if (h.st == HState::Dismounting) {
        Play(ped, HA("horse_idle"));
        ped->SetPosition(h.x, h.y, h.z);
        PlaceRider(player, h, 1.0f);
        // a saida toca uma vez e fica no ultimo quadro: tocar de novo a cada frame reiniciava a animacao quando ela acabava antes do tempo (CJ em loop)
        CAnimBlendHierarchy* ex = HA("exit_right");
        CAnimBlendAssociation* as = nullptr;
        for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(player->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a))
            if (ex && a->m_pHierarchy == ex) as = a;
        if (!as && ex && h.exitT0 != h.t0) { as = CAnimManager::BlendAnimation(player->m_pRwClump, ex, ANIMATION_FREEZE_LAST_FRAME, 30.0f); h.exitT0 = h.t0; }
        KeepOnly(player, as);
        if (now - h.t0 >= kDismountMs) {
            bool found = false;
            float gz = CWorld::FindGroundZFor3DCoord(player->GetPosition().x, player->GetPosition().y, player->GetPosition().z + 1.0f, &found, nullptr);
            if (found) player->SetPosition(player->GetPosition().x, player->GetPosition().y, gz + 1.0f);
            SetRiderFree(player);
            h.st = HState::Idle; h.idleT0 = now;
            Log("cavalo %d: desmontou", i);
        }
        return;
    }

    bool aiming = player->bIsAimingGun || player->bFiringWeapon || (pad->NewState.RightShoulder1 > 0 && (int)player->GetWeapon()->m_eWeaponType != 0);
    gAiming = aiming;
    // Riding: analogico = rumo e marcha. Esticar anda e depois corre (acelera); a tecla de correr da galope.
    if (useHit) { h.st = HState::Dismounting; h.t0 = now; h.speed = 0.0f; return; }
    float sx = pad->NewState.LeftStickX, sy = pad->NewState.LeftStickY;
    // o analogico guia so o cavalo: o jogo (que roda depois deste gancho) usava o mesmo analogico para virar o CJ por conta propria, fora do rumo do cavalo
    pad->NewState.LeftStickX = pad->NewState.LeftStickY = 0;
    bool forward = sy < -10.0f, back = sy > 10.0f, sprint = pad->NewState.ButtonCross > 0;
    float turn = (h.speed > 7.0f ? 0.8f : h.speed > 3.5f ? 1.2f : 1.6f);
    if (std::fabs(sx) > 10.0f && !h.jumping) h.heading = WrapAngle(h.heading - (sx / 128.0f) * turn * dt);
    float target = forward ? (sprint ? kHorseSprintSpeed : kHorseRunSpeed) : 0.0f;
    float accel = target > h.speed ? (sprint ? 6.0f : 4.0f) : (back ? 14.0f : 8.0f);
    if (h.jumping) { target = h.speed; accel = 0; }
    h.speed = mm::approach(h.speed, target, accel, dt);
    gRideSpeed = h.speed;

    float nx = h.x, ny = h.y;
    if (h.speed > 0.05f) {
        float fx = -std::sin(h.heading), fy = std::cos(h.heading);
        CVector from(h.x, h.y, h.z + 0.3f), to(h.x + fx * 1.6f, h.y + fy * 1.6f, h.z + 0.3f);
        bool blocked = !CWorld::GetIsLineOfSightClear(from, to, true, true, false, true, false, false, false);
        nx = h.x + fx * h.speed * dt; ny = h.y + fy * h.speed * dt;
        bool found = false;
        float gz = CWorld::FindGroundZFor3DCoord(nx, ny, h.z + 1.2f, &found, nullptr);
        float water = 0.0f;
        bool wet = CWaterLevel::GetWaterLevelNoWaves(nx, ny, gz + 1.0f, &water) && water > gz + 0.8f;
        if (blocked || !found || wet || (!h.jumping && std::fabs(gz - h.groundZ) > 1.3f)) { h.speed = 0.0f; nx = h.x; ny = h.y; }
        else h.groundZ += (gz - h.groundZ) * std::min(1.0f, 12.0f * dt);
    }
    h.x = nx; h.y = ny;
    if (jumpHit && !h.jumping && h.speed > 1.0f) { h.jumping = true; h.vz = 5.6f; h.jumpT0 = now; Log("cavalo %d: pulo", i); }
    // coice: tecla de agachar, so com o cavalo parado
    if (kickHit && !h.jumping && h.speed < 0.25f && now >= h.kickUntil) { h.kickUntil = now + 1130; HorseSound("cavalo2.mp3", hp); Log("cavalo %d: coice", i); }
    if (h.jumping) {
        h.z += h.vz * dt; h.vz -= 16.0f * dt;
        if (h.vz < 0.0f && h.z <= h.groundZ + kHorseCenter) { h.jumping = false; h.z = h.groundZ + kHorseCenter; }
    } else {
        h.z = h.groundZ + kHorseCenter;
    }
    {   // diagnostico: a matriz do cavalo que o jogo deixou no frame anterior x a inclinacao que gravamos (ate 40 linhas, so em ladeira)
        static int lines = 0;
        if (lines < 40 && ped->m_matrix && h.tnz < 0.97f) { ++lines; Log("TILTDIAG gravado.z=%.3f lido.z=%.3f frente.z=%.3f", h.tnz, ped->m_matrix->at.z, ped->m_matrix->up.z); }
    }
    if (!h.jumping) { // o cavalo acompanha a inclinacao do terreno: 4 pontos de chao em volta (frente/tras 1,1 m, lados 0,45 m)
        float fx = -std::sin(h.heading), fy = std::cos(h.heading), rx = fy, ry = -fx;
        auto gz = [&](float ox, float oy) {
            bool ok = false;
            float z = CWorld::FindGroundZFor3DCoord(h.x + ox, h.y + oy, h.groundZ + 1.5f, &ok, nullptr);
            return ok ? z : h.groundZ;
        };
        // 4 cantos (patas): inclinacao para a frente e para o lado do plano que passa por eles; o chao medio vira a altura do cavalo
        const float L = 1.0f, W = 0.4f;
        float zfl = gz(fx * L - rx * W, fy * L - ry * W), zfr = gz(fx * L + rx * W, fy * L + ry * W);
        float zbl = gz(-fx * L - rx * W, -fy * L - ry * W), zbr = gz(-fx * L + rx * W, -fy * L + ry * W);
        float sf = std::clamp(((zfl + zfr) - (zbl + zbr)) / (4 * L), -0.9f, 0.9f);
        float sr = std::clamp(((zfr + zbr) - (zfl + zbl)) / (4 * W), -0.9f, 0.9f);
        float tx = -sf * fx - sr * rx, ty = -sf * fy - sr * ry, tl = std::sqrt(tx * tx + ty * ty + 1.0f);
        float k = std::min(1.0f, 14.0f * dt);
        h.tnx += (tx / tl - h.tnx) * k; h.tny += (ty / tl - h.tny) * k; h.tnz += (1.0f / tl - h.tnz) * k;
        float n = std::sqrt(h.tnx * h.tnx + h.tny * h.tny + h.tnz * h.tnz);
        h.tnx /= n; h.tny /= n; h.tnz /= n;
    }
    ped->SetPosition(h.x, h.y, h.z);
    ped->SetHeading(h.heading);
    ped->m_fHeadingCurrent = ped->m_fHeadingGoal = h.heading;
    HorseAxes ax = AxesOf(h, h.heading);
    TiltPed(ped, ax);
    ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    // o CJ acompanha o pulo: sobe um pouco da sela no meio do salto
    float lift = h.jumping ? 0.18f * std::sin(3.14159265f * std::min(1.0f, (now - h.jumpT0) / 900.0f)) : 0.0f;
    // (no empinar a raiz de sit_kick ja acompanha a sela)
    // o corpo do cavalo sobe e desce a cada passo: o CJ acompanha a coluna do cavalo (senao "quica" em relacao a sela)
    RwV3d spine{};
    ped->GetBonePosition(spine, 2, true); // true: com false o osso vinha desatualizado (sempre o repouso)
    // osso da sela no referencial do cavalo (inclinado): para cima e para a frente
    float dX = spine.x - h.x, dY = spine.y - h.y, dZ = spine.z - h.z;
    float sUp = dX * ax.u[0] + dY * ax.u[1] + dZ * ax.u[2], sFwd = dX * ax.f[0] + dY * ax.f[1] + dZ * ax.f[2];
    if (!h.boneRestSet) { h.boneRest = sUp; h.boneRestFwd = sFwd; h.boneRestSet = true; }
    // Empinando, a sela sobe e recua num arco (ate ~1 m): o CJ (raiz fixa em sit_kick) segue o osso da sela em tempo real, em vez de um caminho
    // gravado na animacao que nao acompanha o fade nem o tempo do cavalo
    float seatBack = 0.0f;
    if (!h.jumping && now < h.kickUntil && !aiming) {
        // Empinando, a sela recua ate ~1,4 m e sobe: o deslocamento vem da tabela da animacao do Gun (horse_rear.hpp), pelo tempo do coice
        // (o osso da sela lido no jogo nao se move aqui: log REARDIAG antigo, sobe=0.00)
        float u = std::clamp((float)(now - (h.kickUntil - 1130)) / 1000.0f / kRearDur, 0.0f, 1.0f) * (kRearN - 1);
        int i0 = std::min((int)u, kRearN - 2); float fr = u - i0;
        float up = kRear[i0][2] + (kRear[i0 + 1][2] - kRear[i0][2]) * fr, fw = kRear[i0][1] + (kRear[i0 + 1][1] - kRear[i0][1]) * fr;
        // Preferencia: o osso da sela medido agora (e o que o cavalo faz de verdade); a tabela fica de reserva se o osso nao se mexe
        float bUp = sUp - h.boneRest, bBack = h.boneRestFwd - sFwd;
        if (std::fabs(bUp) + std::fabs(bBack) > 0.03f) h.boneMoves = true;
        // O osso da sela (2) so sobe (~0,3 m) e quase nao recua, mas a sela de verdade recua: com so o osso o CJ entrava no cavalo, com a tabela
        // inteira (1,4 m) ficava ~1 m atras da sela (foto). A subida vem do osso e o recuo e 30% da tabela (kRearBack).
        if (h.boneMoves) up = bUp;
        fw *= kRearBack;
        static unsigned lastLog = 0; static int logs = 0;
        if (logs < 60 && now - lastLog > 100) { lastLog = now; ++logs; Log("REARDIAG2 osso: sobe=%.2f recua=%.2f tabela: sobe=%.2f recua=%.2f usando=%s", bUp, bBack, kRear[i0][2], -kRear[i0][1], h.boneMoves ? "osso" : "tabela"); }
        lift += up; seatBack = -fw;
    }
    else if (!h.jumping && now >= h.kickUntil) lift += std::clamp(sUp - h.boneRest, -0.2f, 0.2f);
    // Mirando, o CJ fica na pose em pe do jogo (sem a raiz que o "senta" na sela): sobe 0,45 m e recua 0,3 m, senao o tronco afunda no cavalo
    // O jogo faz "coronhada" (GUN_BUTT) se ha um ped vivo a menos de 1 m na frente do CJ: o cavalo, colado nele. Com a origem do CJ exatamente
    // na do cavalo o vetor CJ->cavalo e nulo e a checagem (produto escalar > 0) falha; por isso, mirando, sem deslocamento lateral nem para tras.
    // Mirando a raiz e a pelve sao as da base de mira (em pe); sobe 0,61 m (quadril a altura da sela do Gun) e as pernas sentadas vem de sitl_*.
    PlaceRider(player, h, aiming ? 0.0f : -0.05f, lift + (aiming ? 0.61f : 0.0f), seatBack);
    player->bIsStanding = true; player->bWasStanding = true; player->bIsInTheAir = false; player->bIsLanding = false; // colisao desligada: sem isso o jogo acha que ele esta no ar
    AlignGun(player, aiming);
    ClampAim(player, h.heading, now, aiming);
    if (aiming) { FireMounted(player, pad, now); AimPose(player, now); } else AimPoseOff(player);
    // AlignGun/ClampAim refazem a matriz do CJ so com o rumo (em pe): por ultimo ele volta a se inclinar com o cavalo, no rumo em que ficou (mirando,
    // o rumo e o da camera); sem isso, em ladeira o corpo ficava reto enquanto o quadril acompanhava a sela inclinada
    if (player->m_matrix) TiltPed(player, AxesOf(h, std::atan2(-player->m_matrix->up.x, player->m_matrix->up.y)));

    // animacao pela velocidade; o ciclo toca na velocidade do chao (legs/speed) para o casco nao deslizar
    CAnimBlendHierarchy *horse, *rider;
    float native = 0.0f;
    int g = mm::gait(h.speed);
    const char* rn = "sit";
    if (h.jumping) { horse = HA("horse_jump") ? HA("horse_jump") : HA("horse_kick_forward"); rn = "sit_run"; }
    else if (now < h.kickUntil) { horse = HA("horse_kick_forward"); if (!aiming && HA("sit_kick")) rn = "sit_kick"; } // o CJ empina junto (gun_h_trample)
    else if (g == 0) horse = HA("horse_idle");
    else if (g == 1) { horse = HA("horse_walk"); rn = "sit_walk"; native = kHorseWalk; }
    else if (g == 2) { horse = HA("horse_run"); rn = "sit_run"; native = kHorseRun; }
    else { horse = HA("horse_sprint"); rn = "sit_galop"; native = kHorseSprint; }
    rider = HA(rn);
    float rate = native > 0.0f ? std::clamp(h.speed / native, 0.6f, 1.6f) : 1.0f;
    // No empinar a mistura e instantanea: a animacao ja entra e sai da pose parada (FADE) e a tabela do CJ (horse_rear.hpp) supoe o cavalo na pose cheia
    bool rearing = !h.jumping && now < h.kickUntil;
    CAnimBlendAssociation* ah = PlayHorse(ped, horse, rearing ? 1000.0f : 4.0f);
    // Mirando, a pose de sela vira pose em pe com a mira do jogo (misturar as duas deitava o CJ); so se zeram os passos
    CAnimBlendAssociation* ar = nullptr;
    if (aiming) {
        // Mirando, as animacoes do CJ sao do jogo, intactas: a pose de mirar do SA e uma base (gun_stand) com o corpo girado ~54 graus mais a animacao
        // parcial de tiro por cima, que so aponta certo sobre essa base. Trocar a base por idle_stance (como eu fazia) deixava o cano 54 graus
        // fora da mira (medido: desvio_do_cano ~0,95 rad, igual com os mods Enhanced desligados). So o pulo/queda do CJ continuam bloqueados.
        ZeroBlocked(player);
        KeepAimBase(player, true);
        // Como no Gun, so o tronco mira: as pernas ficam sentadas. sitl_* tem so raiz, pelve e pernas e entra como PARCIAL por cima da base.
        std::string ln = std::string("sitl") + (rn + 3);
        if (CAnimBlendHierarchy* lh = HA(ln.c_str())) {
            ar = CAnimManager::BlendAnimation(player->m_pRwClump, lh, ANIMATION_LOOPED | ANIMATION_PARTIAL, 8.0f);
            for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(player->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a))
                if (a != ar && IsRiderHier(a->m_pHierarchy)) a->m_fBlendDelta = -8.0f;
        }
    } else {
        KeepAimBase(player, false);
        ar = PlayRider(player, rider, rearing ? 1000.0f : 4.5f);
        for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(player->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a))
            if (IsLegsHier(a->m_pHierarchy)) a->m_fBlendDelta = -8.0f;
    }
    if (ah) ah->m_fSpeed = rate;
    if (ar) ar->m_fSpeed = rate;
    // cavalo e cavaleiro tem ciclos de mesma duracao (Gun): mantem a mesma fase, senao o CJ balanca fora do passo do cavalo
    if (ah && ar && native > 0.0f && !h.jumping && now >= h.kickUntil) ar->m_fCurrentTime = ah->m_fCurrentTime;
    LogRiderDiag(now, player, h, aiming);
    if (ah && !h.jumping && h.speed >= 2.8f) { // casco no inicio de cada ciclo; asfalto e concreto soam diferente da terra
        float t = ah->m_fCurrentTime;
        if (t < 0.5f && !h.stepSounded) {
            h.stepSounded = true;
            unsigned char surf = ped->m_nContactSurface;
            bool hard = surf < 8 || surf == 89;
            bool gallop = h.speed >= 6.2f;
            HorseSound(hard ? (gallop ? "cavalo4.mp3" : "cavalo3.mp3") : (gallop ? "cavalo1.mp3" : "cavalo-andando.mp3"), hp);
        } else if (t >= 0.5f) {
            h.stepSounded = false;
        }
    }
}

// Zoom ao mirar do cavalo (como no Red Dead): o campo de visao fecha 40% em 0,25 s e volta ao soltar a mira. O jogo reescreve o FOV a
// cada frame, entao a conta parte do valor que ele pos neste frame (e nao do que escrevemos no anterior).
void UpdateAimZoom(float dt, CPed* player) {
    static float zoom = 0.0f, speed = 0.0f, lastWritten = 0.0f, lastBase = 0.0f;
    bool aim = gRiding && gAiming;
    zoom = std::clamp(zoom + (aim ? 1.0f : -1.0f) * dt / 0.25f, 0.0f, 1.0f);
    // sensacao de velocidade: o campo de visao abre ate 18% no galope (so sem mirar, a mira fecha)
    speed += (gRiding ? std::min(1.0f, gRideSpeed / kHorseSprintSpeed) - speed : -speed) * std::min(1.0f, 3.0f * dt);
    float k = (1.0f - 0.4f * zoom) * (1.0f + 0.18f * speed * (1.0f - zoom));
    float cur = CDraw::ms_fFOV;
    float base = (lastWritten != 0.0f && cur == lastWritten) ? lastBase : cur;
    if (std::fabs(k - 1.0f) > 0.001f) {
        CDraw::ms_fFOV = base * k;
        lastWritten = CDraw::ms_fFOV; lastBase = base;
    } else {
        lastWritten = 0.0f;
    }
    gAimFov = CDraw::ms_fFOV;
}

// Um passo por frame: nasce/some os cavalos dos locais, atualiza cada um. useHit/jumpHit: tecla pressionada neste frame.
// Diagnostico dos tiros (ate 150 linhas, a cada 250 ms atirando): onde esta a boca da arma em relacao ao eixo da camera. O jogo atira ao
// longo do eixo da camera (Find3rdPersonCamTargetVector), entao o deslocamento da boca para esse eixo e o deslocamento das balas.
// Compare as linhas "a pe" com as "cavalo": a diferenca em dz e quanto as balas saem mais baixas montado.
void ShotDiag(CPed* player, unsigned now) {
    static unsigned last = 0;
    static int lines = 0;
    if (lines >= 150 || !player->bFiringWeapon || now - last < 250) return;
    int type = (int)player->GetWeapon()->m_eWeaponType;
    CWeaponInfo* wi = type ? CWeaponInfo::GetWeaponInfo(player->GetWeapon()->m_eWeaponType, 0) : nullptr;
    if (!wi) return;
    last = now; ++lines;
    RwV3d m{ wi->m_vecFireOffset.x, wi->m_vecFireOffset.y, wi->m_vecFireOffset.z };
    player->GetTransformedBonePosition(m, 24, false);
    CVector src, tgt;
    TheCamera.Find3rdPersonCamTargetVector(wi->m_fWeaponRange, CVector(m.x, m.y, m.z), &src, &tgt);
    CVector pp = player->GetPosition();
    bool found = false;
    float gz = CWorld::FindGroundZFor3DCoord(pp.x, pp.y, pp.z + 1.0f, &found, nullptr);
    CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
    Log("SHOTDIAG %s arma=%d cam=%d ped.z=%.2f chao=%.2f boca=(%.2f,%.2f,%.2f) eixo=(%.2f,%.2f,%.2f) boca-eixo=(%.2f,%.2f,%.2f) cam.z=%.2f frente.z=%.2f",
        gRiding ? "cavalo" : "a pe", type, (int)cam.m_nMode, pp.z, found ? gz : -99.0f, m.x, m.y, m.z, src.x, src.y, src.z, m.x - src.x, m.y - src.y, m.z - src.z,
        cam.m_vecSource.z, cam.m_vecFront.z);
}

// Diagnostico do tiro montado (ate 200 linhas, a cada 300 ms com mira ou tiro apertado): por que o jogo nao atira. Uma linha mostra a arma,
// o estado dela, os botoes e a tarefa de usar arma (comando pedido/atual, se terminou, qual animacao tocou).
void FireDiag(CPed* player, unsigned now, CPad* pad) {
    static unsigned last = 0;
    static int lines = 0;
    if (lines >= 200 || !gRiding || now - last < 300) return;
    if (!(pad->NewState.RightShoulder1 > 0 || pad->NewState.ButtonCircle > 0)) return;
    last = now; ++lines;
    CWeapon* w = player->GetWeapon();
    CTaskSimpleUseGun* t = player->m_pIntelligence ? player->m_pIntelligence->GetTaskUseGun() : nullptr;
    Log("FIREDIAG slot=%d escolhida=%d tipo=%d estado=%d clip=%u total=%u mira(RS1)=%d fogo(Circulo)=%d bIsAiming=%d bFiring=%d move=%d agachado=%d bracoBloq=%d tarefa=%d cmd=%d/%d fim=%d anim=%08X",
        (int)player->m_nSelectedWepSlot, player->m_pPlayerData ? (int)player->m_pPlayerData->m_nChosenWeapon : -1, (int)w->m_eWeaponType, (int)w->m_nState,
        w->m_nAmmoInClip, w->m_nAmmoTotal, (int)pad->NewState.RightShoulder1, (int)pad->NewState.ButtonCircle, (int)player->bIsAimingGun, (int)player->bFiringWeapon,
        (int)player->m_nMoveState, (int)player->bIsDucking, (int)player->bDuckRightArmBlocked, t ? 1 : 0, t ? (int)t->m_nLastCommand : -1, t ? (int)t->m_nNextCommand : -1,
        t ? (int)t->m_bIsFinished : -1, (t && t->m_pAnim && t->m_pAnim->m_pHierarchy) ? t->m_pAnim->m_pHierarchy->m_hashKey : 0);
}

// Pose de mira da pistola (a animacao do pacote Enhanced, parcial so do tronco/bracos): a tarefa de arma do jogo nao roda montado (bIsAiming=0, sem
// IK de braco), entao os bracos ficavam caidos. Aqui a animacao de tiro do grupo da arma fica parada no inicio do laco (a pose de mira) e toca
// inteira a cada tiro, como a tarefa faria. Ao soltar a mira ela sai de cena.
unsigned gShotAt = 0;
void AimPose(CPed* player, unsigned now) {
    CWeapon* w = player->GetWeapon();
    CWeaponInfo* wi = w->m_eWeaponType ? CWeaponInfo::GetWeaponInfo(w->m_eWeaponType, 0) : nullptr;
    // A 9mm do jogo (22) e a unica pistola sem animacao do Enhanced: montada ela toca a pose da Desert Eagle (24, grupo python), igual as outras;
    // o tiro continua sendo o da 9mm (so a animacao e emprestada).
    if (w->m_eWeaponType == 22) { if (CWeaponInfo* de = CWeaponInfo::GetWeaponInfo((eWeaponType)24, 0)) wi = de; }
    if (!wi || wi->m_nSlot != 2 || !player->m_pRwClump) return;
    CAnimBlendAssociation* a = CAnimManager::BlendAnimation(player->m_pRwClump, (int)wi->m_nAnimToPlay, 224, 8.0f);
    if (!a) return;
    float start = wi->m_fAnimLoopStart, end = std::max(wi->m_fAnimLoopEnd, start + 0.1f);
    static int logs = 0;
    if (logs++ < 3) Log("AIMPOSE grupo=%d laco=%.2f..%.2f duracao=%.2f", (int)wi->m_nAnimToPlay, start, end, a->m_pHierarchy ? a->m_pHierarchy->m_fTotalTime : -1.0f);
    bool shooting = gShotAt && now - gShotAt < (unsigned)((end - start) * 1000.0f);
    a->m_fSpeed = shooting ? 1.0f : 0.0f;
    if (!shooting) a->m_fCurrentTime = start;
    // a tarefa de arma (parada, sem alvo) tambem mexe no peso desta animacao: o peso e o marcador de remocao ficam por nossa conta
    a->m_fBlendAmount = 1.0f; // (log: o peso ficava em ~0,55: algo o reduzia a cada frame)
    a->m_fBlendDelta = 0.0f;
    static unsigned lastAim = 0; static int aimLines = 0;
    if (aimLines < 15 && now - lastAim > 500) { lastAim = now; ++aimLines; Log("AIMPOSE2 peso=%.2f tempo=%.2f vel=%.1f flags=%04X atirando=%d", a->m_fBlendAmount, a->m_fCurrentTime, a->m_fSpeed, (unsigned)a->m_nFlags, (int)shooting); }
}
void AimPoseOff(CPed* player) {
    if (!player->m_pRwClump) return;
    if (CAnimBlendAssociation* a = RpAnimBlendClumpGetAssociation(player->m_pRwClump, 224u)) { a->m_fSpeed = 1.0f; a->m_fBlendDelta = -8.0f; }
}

// O jogo processa o comando de tiro do jogador montado (FIREDIAG: cmd=2) mas nunca chama o disparo (municao fixa, bFiringWeapon=0): aqui a arma e
// disparada direto, como CTaskSimpleUseGun::FireGun (origem na mao, boca pelo offset da arma). CWeapon::Fire cuida da cadencia, da municao e do
// recarregar; o tiro do jogador sai ao longo do eixo da camera. So pistolas (slot 2): as demais armas nao atiram montado.
void FireMounted(CPed* player, CPad* pad, unsigned now) {
    CWeapon* w = player->GetWeapon();
    int type = (int)w->m_eWeaponType;
    CWeaponInfo* wi = type ? CWeaponInfo::GetWeaponInfo(w->m_eWeaponType, 0) : nullptr;
    // Recarga: como nao rodamos o comando de recarga do jogo, o pente vazio podia ficar em recarga (estado 2) sem voltar. Passados 2,2 s o pente e
    // refeito aqui (se o jogo completar antes, o estado muda e o relogio zera).
    static unsigned reloadSince = 0;
    if (wi && wi->m_nSlot == 2 && (int)w->m_nState == 2 && w->m_nAmmoInClip == 0 && w->m_nAmmoTotal > 0) {
        if (!reloadSince) reloadSince = now;
        else if (now - reloadSince > 2200) { w->m_nAmmoInClip = (unsigned)std::min<unsigned>(w->m_nAmmoTotal, wi->m_nAmmoClip); w->m_nState = (decltype(w->m_nState))0; reloadSince = 0; }
    } else reloadSince = 0;
    static int presses = 0;
    if (pad->NewState.ButtonCircle > 0 && pad->OldState.ButtonCircle <= 0 && presses++ < 40)
        Log("FIREMOUNTED clique: tipo=%d info=%d mira(RS1)=%d tipoDeFogo=%d estado=%d clip=%u", type, wi ? 1 : 0, (int)pad->NewState.RightShoulder1, wi ? (int)wi->m_nWeaponFire : -1, (int)w->m_nState, w->m_nAmmoInClip);
    if (!wi || wi->m_nSlot != 2 || pad->NewState.RightShoulder1 <= 0 || pad->NewState.ButtonCircle <= 0) return;
    static int skipped = 0;
    if ((wi->m_nWeaponFire != 1 || (int)w->m_nState == 2) && skipped++ < 20) Log("FIREMOUNTED ignorado: tipo=%d tipoDeFogo=%u estado=%d", type, wi->m_nWeaponFire, (int)w->m_nState);
    if (wi->m_nWeaponFire != 1 || (int)w->m_nState == 2) return;
    if (pad->OldState.ButtonCircle > 0) return; // pistola: um tiro por clique
    RwV3d hand{}, m{ wi->m_vecFireOffset.x, wi->m_vecFireOffset.y, wi->m_vecFireOffset.z };
    player->GetBonePosition(hand, 24, false);
    player->GetTransformedBonePosition(m, 24, false);
    CVector origin(hand.x, hand.y, hand.z + wi->m_vecFireOffset.z + 0.15f), muzzle(m.x, m.y, m.z);
    // Alvo: onde o eixo da camera (a mira) encontra o mundo. Sem alvo o jogo calculava a trajetoria pelo eixo da camera a partir da boca da arma
    // e as balas saiam abaixo da mira (bug da 1.0.0): com o ponto de impacto dado, a bala vai da boca direto ate a mira.
    CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
    // A mira desenhada NAO fica no centro da tela: o jogo a poe em (CHairMultX, CHairMultY) = (0,53, 0,4) da tela (acima do centro). O raio da mira e o do
    // centro mais esse desvio, com o campo de visao que esta sendo desenhado (o zoom da mira mexe nele). Atirar pelo centro mandava a bala abaixo da mira.
    float W = (float)RsGlobal.maximumWidth, H = (float)RsGlobal.maximumHeight;
    float fov = gAimFov > 1.0f ? gAimFov : CDraw::ms_fFOV, tanH = std::tan(fov * 0.5f * 3.14159265f / 180.0f), asp = CDraw::ms_fAspectRatio;
    CVector fr = cam.m_vecFront, up = cam.m_vecUp;
    CVector rt(fr.y * up.z - fr.z * up.y, fr.z * up.x - fr.x * up.z, fr.x * up.y - fr.y * up.x);
    // Direcao pela mira DESENHADA, achada com a mesma projecao que desenha a tela (CSprite::CalcScreenCoors): parte da frente da camera e corrige o
    // erro em pixels ate a projecao de um ponto a 40 m cair em (CHairMultX*W, CHairMultY*H). Nao depende de saber o FOV certo (o chute com tan(FOV)
    // errava: o amarelo, alvo do jogo, ficava 40 px acima da mira no print).
    CVector dir = fr;
    {
        float tx = W * CCamera::m_f3rdPersonCHairMultX, ty = H * CCamera::m_f3rdPersonCHairMultY, kpx = 2.0f * tanH / W;
        for (int it = 0; it < 5; ++it) {
            CVector p = cam.m_vecSource + dir * 40.0f;
            RwV3d in{ p.x, p.y, p.z }, out{}; float w = 0, h = 0;
            if (!CSprite::CalcScreenCoors(in, &out, &w, &h, false, false)) break;
            dir = dir + rt * ((tx - out.x) * kpx) + up * ((out.y - ty) * kpx);
            float l = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            dir = dir * (1.0f / (l > 0.0f ? l : 1.0f));
        }
    }
    // O jogo mistura a direcao do cano (boca - origem) com a mira, e o cano montado aponta alguns graus para o lado (AIMDIAG: desvio_do_cano): o tiro saia
    // deslocado para a esquerda. A origem passa a ficar na linha da mira, atras da boca, e o cano coincide com ela.
    origin = muzzle - dir * 0.35f;
    // O raio sai da camera, que fica atras do CJ: ele batia no proprio cavalo (log: o alvo parava a 3-6 m, na altura do pescoco do cavalo) e a bala ia
    // para dentro dele. Comeca na frente da boca da arma e ignora o cavalo.
    CVector mc = muzzle - cam.m_vecSource;
    CVector pc = player->GetPosition() - cam.m_vecSource;
    // o raio nao pode comecar antes do CJ (o print mostrou o alvo verde na cabeca dele): depois da boca e 0,8 m alem do CJ
    float along = std::max({ 0.5f, mc.x * dir.x + mc.y * dir.y + mc.z * dir.z + 0.3f, pc.x * dir.x + pc.y * dir.y + pc.z * dir.z + 0.8f });
    CVector cs = cam.m_vecSource + dir * along, ce = cam.m_vecSource + dir * wi->m_fWeaponRange, tgt = ce;
    CColPoint cp; CEntity* hitEnt = nullptr;
    CEntity* savedIgnore = CWorld::pIgnoreEntity;
    CWorld::pIgnoreEntity = gRider >= 0 ? gHorses[gRider].ped : nullptr;
    bool los = CWorld::ProcessLineOfSight(cs, ce, cp, hitEnt, true, true, true, true, true, false, false, true);
    if (los) tgt = cp.m_vecPoint;
    CWorld::pIgnoreEntity = savedIgnore;
    static int tl = 0;
    if (tl++ < 20) Log("FIREMOUNTED alvo=(%.1f,%.1f,%.1f) boca=(%.1f,%.1f,%.1f) acertou=%d", tgt.x, tgt.y, tgt.z, muzzle.x, muzzle.y, muzzle.z, hitEnt ? 1 : 0);
    // Log do teste: o alvo que o PROPRIO jogo calcula (amarelo) ia para baixo da terra enquanto o nosso (verde) ficava certo, e a bala seguia o dele.
    // A conta dele usa o FOV da camera (cam.m_fFOV), que e menor que o desenhado: o desvio vertical da mira sai errado. Durante o disparo o FOV da
    // camera passa a ser o desenhado, e as duas contas coincidem.
    // O jogo calcula a direcao do tiro sozinho (Find3rdPersonCamTargetVector) e ignora o alvo passado. A conta dele usa o FOV da camera e os
    // multiplicadores da mira; so durante o disparo os multiplicadores sao trocados pelos que fazem a conta dele dar a nossa direcao.
    float camTan = std::tan(cam.m_fFOV * 0.5f * 3.14159265f / 180.0f), c = dir.x * fr.x + dir.y * fr.y + dir.z * fr.z;
    float da = (dir.x * up.x + dir.y * up.y + dir.z * up.z) / c, db = (dir.x * rt.x + dir.y * rt.y + dir.z * rt.z) / c;
    float oldMx = CCamera::m_f3rdPersonCHairMultX, oldMy = CCamera::m_f3rdPersonCHairMultY, oldCamFov = cam.m_fFOV;
    CCamera::m_f3rdPersonCHairMultY = 0.5f - da * asp / (2.0f * camTan);
    CCamera::m_f3rdPersonCHairMultX = 0.5f + db / (2.0f * camTan);
    bool ok = w->Fire(player, &origin, &muzzle, nullptr, &tgt, nullptr);
    // som: o evento do ped nao toca montado (e a arma 72 e de mod, sem som proprio); o motor global toca o da pistola (CAudioEngine::ReportWeaponEvent)
    if (ok) plugin::CallMethod<0x506F40, void*, int, int, CPhysical*>((void*)0xB6BC90, 145, 22, player);
    if (ok) player->DoGunFlash(250, false);
    if (ok) {
        // Depuracao: o que o jogo fez de verdade (os tracos de bala que ele criou agora) contra o que pedimos, e o alvo que o proprio jogo calcularia
        CVector gs, gt;
        TheCamera.Find3rdPersonCamTargetVector(wi->m_fWeaponRange, muzzle, &gs, &gt);
        gShotDbg.muzzle = muzzle; gShotDbg.tgt = tgt; gShotDbg.gameTgt = gt; gShotDbg.at = now;
        Log("FIREMOUNTED fov camera=%.1f desenhado=%.1f", oldCamFov, fov);
    }
    // O tiro do jogo montado vai para o ponto (0,0,0) (log TRACO: o traco dele parte da boca rumo a origem do mundo: ponto de impacto zerado), entao nao ha impacto nem dano: o impacto e o traco saem daqui,
    // da boca ate o ponto de mira. O log TRACOS diz quantos tracos o jogo criou.
    if (ok) {
        int made = 0;
        for (unsigned i = 0; i < MAX_NUM_BULLETTRACES; ++i) if (CBulletTraces::aTraces[i].m_bExists && CBulletTraces::aTraces[i].m_nCreationTime >= (int)CTimer::m_snTimeInMilliseconds - 50) { ++made; const CBulletTrace& t = CBulletTraces::aTraces[i]; static int tl2 = 0; if (tl2++ < 40) Log("TRACO ini=(%.1f,%.1f,%.1f) fim=(%.1f,%.1f,%.1f) nossoAlvo=(%.1f,%.1f,%.1f) boca=(%.1f,%.1f,%.1f)", t.m_vecStart.x, t.m_vecStart.y, t.m_vecStart.z, t.m_vecEnd.x, t.m_vecEnd.y, t.m_vecEnd.z, tgt.x, tgt.y, tgt.z, muzzle.x, muzzle.y, muzzle.z); }
        static int tr = 0;
        if (tr++ < 40) Log("TRACOS jogo=%d los=%d acertou=%d alcance=%.0f", made, (int)los, hitEnt ? 1 : 0, wi->m_fWeaponRange);
        if (los) { CVector st = muzzle; w->DoBulletImpact(player, hitEnt, &st, &tgt, &cp, 0); }
    }
    CCamera::m_f3rdPersonCHairMultX = oldMx; CCamera::m_f3rdPersonCHairMultY = oldMy;
    if (ok) gShotAt = now; // AimPose toca a animacao de tiro desde o inicio do laco
    static int lines = 0;
    if (lines++ < 80) Log("FIREMOUNTED tipo=%d estado=%d clip=%u total=%u disparou=%d", type, (int)w->m_nState, w->m_nAmmoInClip, w->m_nAmmoTotal, (int)ok);
}

void DrawShotDebug() {
    unsigned t = CTimer::m_snTimeInMilliseconds;
    if (!gShotDbg.at || t - gShotDbg.at > 4000) return;
    float W = (float)RsGlobal.maximumWidth, H = (float)RsGlobal.maximumHeight;
    auto box = [&](float x, float y, float r, CRGBA c) { CSprite2d::DrawRect(CRect(x - r, y + r, x + r, y - r), c); };
    auto proj = [&](const CVector& p, float r, CRGBA c) {
        RwV3d in{ p.x, p.y, p.z }, out{}; float w = 0, h = 0;
        if (CSprite::CalcScreenCoors(in, &out, &w, &h, true, false)) box(out.x, out.y, r, c);
    };
    box(W * CCamera::m_f3rdPersonCHairMultX, H * CCamera::m_f3rdPersonCHairMultY, 4, CRGBA(60, 120, 255, 255)); // onde a mira e desenhada
    for (int i = 1; i <= 8; ++i) proj(gShotDbg.muzzle + (gShotDbg.tgt - gShotDbg.muzzle) * (i / 8.0f), 2.5f, CRGBA(255, 40, 40, 255)); // caminho pedido
    proj(gShotDbg.tgt, 6, CRGBA(40, 255, 40, 255));       // alvo calculado aqui (verde)
    proj(gShotDbg.gameTgt, 6, CRGBA(255, 230, 40, 255));  // alvo que o jogo calcularia (amarelo)
    proj(gShotDbg.muzzle, 4, CRGBA(255, 255, 255, 255));  // boca da arma
}

void UpdateHorses(unsigned now, CPed* player) {
    if (!gHorseInit) InitHorse();
    if (!gHorseOn) return;
    ShotDiag(player, now);
    FireDiag(player, now, CPad::GetPad(0));
    static unsigned last = 0;
    float dt = last ? std::min(0.1f, (now - last) / 1000.0f) : 0.0f;
    last = now;
    CPad* pad = CPad::GetPad(0);
    bool useHit = pad->NewState.ButtonTriangle > 0 && pad->OldState.ButtonTriangle <= 0;
    bool jumpHit = pad->NewState.ButtonSquare > 0 && pad->OldState.ButtonSquare <= 0;
    bool kickHit = pad->NewState.ShockButtonL > 0 && pad->OldState.ShockButtonL <= 0; // agachar (C)
    // o coice usa a tecla de agachar: se ela chega ao jogo o CJ agacha na sela e fica agachado (log: agachado=1, sem comando de tiro)
    if (gRiding) pad->NewState.ShockButtonL = 0;
    CVector pp = player->GetPosition();
    for (int i = 0; i < kHorseSpotN; ++i) {
        Horse& h = gHorses[i];
        const HorseSpot& sp = kHorseSpots[i];
        if (!h.ped) {
            if (std::hypot(sp.x - pp.x, sp.y - pp.y) > kHorseSpawnDist) continue;
            bool found = false;
            float gz = CWorld::FindGroundZFor3DCoord(sp.x, sp.y, sp.z < -99.0f ? 100.0f : sp.z + 30.0f, &found, nullptr);
            if (!found || (sp.z > -99.0f && std::fabs(gz - sp.z) > 30.0f)) continue; // chao ainda nao carregou
            SpawnHorse(i, sp.x, sp.y, gz, sp.heading, now, i % (kHorseCoats - 1));
        }
    }
    for (int i = 0; i < kHorseN; ++i) {
        Horse& h = gHorses[i];
        if (!h.ped) continue;
        gStageSlot = 100 + i;
        float d = std::hypot(h.x - pp.x, h.y - pp.y);
        if (d > kHorseDespawnDist && h.st == HState::Idle) { // longe por 60 s: some e volta ao local de origem
            if (!h.farSince) h.farSince = now;
            if (now - h.farSince > kHorseFarMs) { Log("cavalo %d: removido (longe)", i); RemoveHorse(i); continue; }
        } else {
            h.farSince = 0;
        }
        UpdateHorse(i, now, dt, player, pad, useHit && (gRider < 0 || gRider == i), jumpHit, kickHit && gRider == i);
    }
    UpdateAimZoom(dt, player);
}

// F9 > Cavalo: um cavalo 4 m a frente do jogador (slot manual, so um por vez).
void SpawnHorseAhead(CPed* player, unsigned now) {
    if (!gHorseOn) { Toast("Cavalo desligado ou sem arquivos (veja FaunaSA.log)", now); return; }
    if (gHorses[kHorseManual].ped) RemoveHorse(kHorseManual);
    CVector p = player->TransformFromObjectSpace({ 0.0f, 4.0f, 0.0f });
    bool found = false;
    float gz = CWorld::FindGroundZFor3DCoord(p.x, p.y, p.z + 2.0f, &found, nullptr);
    if (!found) { Toast("Sem chao carregado aqui", now); return; }
    int coat = gManualCoat % kHorseCoats;
    if (SpawnHorse(kHorseManual, p.x, p.y, gz, player->m_fHeadingCurrent * 57.29578f + 180.0f, now, coat)) {
        gManualCoat = (gManualCoat + 1) % kHorseCoats; // cada Enter da uma pelagem
        Toast((std::string("Spawnado: Cavalo ") + kHorseCoatLabel[coat]).c_str(), now);
    }
    else Toast("Carregando o modelo do cavalo... aperte Enter de novo", now);
}

void ResetHorses() { // novo jogo/load: os ponteiros antigos nao valem mais (sem chamar o jogo)
    for (auto& h : gHorses) h = Horse{};
    for (int c = 0; c < kHorseCoats; ++c) gHorseModels[c] = ModelRef{ kHorseCoatName[c] };
    gRiding = false; gRider = -1;
}
