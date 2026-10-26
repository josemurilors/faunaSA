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
constexpr float kHorseWalk = 0.97f, kHorseRun = 4.7f, kHorseSprint = 7.5f; // m/s, da propria animacao
const char* const kHorseDir = "FaunaSA\\horse\\";

enum class HState { Idle, Mounting, Riding, Dismounting };

struct Horse {
    CPed* ped = nullptr;
    HState st = HState::Idle;
    float x = 0, y = 0, z = 0, heading = 0, speed = 0, vz = 0, groundZ = 0;
    bool jumping = false, stepSounded = false;
    unsigned t0 = 0, jumpT0 = 0, idleT0 = 0, nextKick = 0, kickUntil = 0, deadSince = 0, farSince = 0;
};

std::array<Horse, kHorseN> gHorses;
ModelRef gHorseModel{ "cavalo" };
std::map<std::string, CAnimBlendHierarchy*> gHorseAnim; // nome do bloco em minusculas
bool gHorseOn = true, gHorseInit = false, gRiding = false;
unsigned gHintAt = 0; // proxima vez que a dica de montar pode aparecer
int gRider = -1;

CAnimBlendHierarchy* HA(const char* n) { auto it = gHorseAnim.find(n); return it == gHorseAnim.end() ? nullptr : it->second; }

void InitHorse() {
    gHorseInit = true;
    mm::Ini ini;
    ini.load("FaunaSA.ini");
    gHorseOn = ini.num("Horse", "Enabled", 1) != 0;
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
        bool rider = boarding || n.rfind("sit", 0) == 0;
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
void SetRiderFree(CPed* player) {
    player->bUsesCollision = true;
    gRiding = false; gRider = -1;
}

void RemoveHorse(int i) {
    Horse& h = gHorses[i];
    if (h.ped && CPools::ms_pPedPool->IsObjectValid(h.ped)) CPopulation::RemovePed(h.ped);
    if (gRider == i) { if (CPed* pl = FindPlayerPed()) SetRiderFree(pl); else { gRiding = false; gRider = -1; } } // nunca deixa o jogador preso
    h = Horse{};
}

bool SpawnHorse(int i, float x, float y, float groundZ, float headingDeg, unsigned now) {
    if (!EnsureLoaded(gHorseModel, now)) return false;
    auto* ped = new CCivilianPed(PED_TYPE_MISSION1, gHorseModel.id);
    if (!ped) return false;
    ped->m_nCreatedBy = 2; // PED_MISSION: o gerenciador de populacao nao recicla
    ped->m_fHealth = 1500.0f;
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
void PlaceRider(CPed* player, const Horse& h, float dx, float lift = 0.0f) {
    float cs = std::cos(h.heading), sn = std::sin(h.heading);
    player->SetPosition(h.x + cs * dx, h.y + sn * dx, h.z + 0.05f + lift);
    if (!player->bIsAimingGun && !player->bFiringWeapon) { // mirando, o CJ gira pela camera; senao olha para onde o cavalo vai
        player->SetHeading(h.heading);
        player->m_fHeadingCurrent = player->m_fHeadingGoal = h.heading;
    }
    player->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
}

CAnimBlendAssociation* Play(CPed* p, CAnimBlendHierarchy* a, float blend = 6.0f) {
    return a ? CAnimManager::BlendAnimation(p->m_pRwClump, a, ANIMATION_LOOPED, blend) : nullptr;
}

// Com o controle ligado a tarefa do jogador volta a tocar andar/correr a cada frame e as pernas do CJ "caminham" na sela. Depois de
// tocar a nossa animacao, zera o peso das demais (mantem as parciais: mira e tiro).
void KeepOnly(CPed* p, CAnimBlendAssociation* keep) {
    if (!keep || !p->m_pRwClump) return;
    for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(p->m_pRwClump); a; a = RpAnimBlendGetNextAssociation(a)) {
        // parciais (mira/tiro) ficam, exceto as de movimento (andar mirando mexe as pernas do CJ)
        if (a == keep || ((a->m_nFlags & ANIMATION_PARTIAL) && !(a->m_nFlags & ANIMATION_MOVEMENT))) continue;
        a->m_fBlendAmount = 0.0f;
        a->m_fBlendDelta = 0.0f;
    }
}

CAnimBlendAssociation* PlayRider(CPed* p, CAnimBlendHierarchy* a, float blend) {
    CAnimBlendAssociation* as = Play(p, a, blend);
    KeepOnly(p, as);
    return as;
}

void UpdateHorse(int i, unsigned now, float dt, CPed* player, CPad* pad, bool useHit, bool jumpHit) {
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
            h.kickUntil = now + 1030;
            h.nextKick = now + 15000 + (unsigned)(25000 * mm::rnd01());
            HorseSound("cavalo2.mp3", hp);
            a = HA("horse_kick_forward");
        }
        Play(ped, a, 4.0f);
        ped->SetPosition(h.x, h.y, h.z);
        ped->SetHeading(h.heading);
        ped->m_fHeadingCurrent = ped->m_fHeadingGoal = h.heading;
        ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        // montar: a pe, a 3 m, tecla de entrar no veiculo
        if (!gRiding && !player->bInVehicle && player->m_fHealth > 0.0f && pd < kHorseMountDist) {
            if (now >= gHintAt) { // caixa de ajuda preta do proprio jogo; repete a cada 9 s enquanto estiver perto
                CHud::SetHelpMessage("Aperte ~k~~VEHICLE_ENTER_EXIT~ para montar no cavalo.", true, false, false);
                gHintAt = now + 9000;
            }
            if (useHit) {
                player->bUsesCollision = false;
                gRiding = true; gRider = i;
                h.st = HState::Mounting; h.t0 = now;
                gHintAt = 0;
                Log("cavalo %d: montando", i);
            }
        } else if (pd > 6.0f) {
            gHintAt = 0; // longe: a dica volta a aparecer quando chegar perto de novo
        }
        return;
    }

    // ---- com cavaleiro
    if (player->m_fHealth <= 0.0f || player->bInVehicle) { SetRiderFree(player); h.st = HState::Idle; h.speed = 0; return; }
    player->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    if (player->m_pPlayerData) player->m_pPlayerData->m_fTimeCanRun = 1000.0f; // correr/galopar nao cansa o CJ (ele caia da sela)
    if (h.st == HState::Mounting) {
        Play(ped, HA("horse_idle"));
        ped->SetPosition(h.x, h.y, h.z);
        PlaceRider(player, h, 1.0f);
        PlayRider(player, HA("jump_on_right"), 8.0f);
        if (now - h.t0 >= kMountMs) {
            h.st = HState::Riding; h.t0 = now; Log("cavalo %d: montado", i);
            CHud::SetHelpMessage("~k~~VEHICLE_ENTER_EXIT~ desmontar~n~~k~~PED_SPRINT~ galopar~n~~k~~PED_JUMPING~ pular~n~Mire e atire normalmente.", true, false, false);
        }
        return;
    }
    if (h.st == HState::Dismounting) {
        Play(ped, HA("horse_idle"));
        ped->SetPosition(h.x, h.y, h.z);
        PlaceRider(player, h, 1.0f);
        PlayRider(player, HA("exit_right"), 8.0f);
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

    // Riding: analogico = rumo e marcha. Esticar anda e depois corre (acelera); a tecla de correr da galope.
    if (useHit) { h.st = HState::Dismounting; h.t0 = now; h.speed = 0.0f; return; }
    float sx = pad->NewState.LeftStickX, sy = pad->NewState.LeftStickY;
    bool forward = sy < -10.0f, back = sy > 10.0f, sprint = pad->NewState.ButtonCross > 0;
    float turn = (h.speed > 6.0f ? 0.9f : h.speed > 2.8f ? 1.2f : 1.6f);
    if (std::fabs(sx) > 10.0f && !h.jumping) h.heading = WrapAngle(h.heading - (sx / 128.0f) * turn * dt);
    float target = forward ? (sprint ? kHorseSprint : kHorseRun) : 0.0f;
    float accel = target > h.speed ? (sprint ? 5.0f : 3.0f) : (back ? 14.0f : 8.0f);
    if (h.jumping) { target = h.speed; accel = 0; }
    h.speed = h.speed < target ? std::min(target, h.speed + accel * dt) : std::max(target, h.speed - accel * dt);

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
    if (h.jumping) {
        h.z += h.vz * dt; h.vz -= 16.0f * dt;
        if (h.vz < 0.0f && h.z <= h.groundZ + kHorseCenter) { h.jumping = false; h.z = h.groundZ + kHorseCenter; }
    } else {
        h.z = h.groundZ + kHorseCenter;
    }
    ped->SetPosition(h.x, h.y, h.z);
    ped->SetHeading(h.heading);
    ped->m_fHeadingCurrent = ped->m_fHeadingGoal = h.heading;
    ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    // o CJ acompanha o pulo: sobe um pouco da sela no meio do salto
    float lift = h.jumping ? 0.18f * std::sin(3.14159265f * std::min(1.0f, (now - h.jumpT0) / 900.0f)) : 0.0f;
    PlaceRider(player, h, -0.05f, lift);

    // animacao pela velocidade; o ciclo toca na velocidade do chao (legs/speed) para o casco nao deslizar
    CAnimBlendHierarchy *horse, *rider;
    float native = 0.0f;
    if (h.jumping) { horse = HA("horse_kick_forward"); rider = HA("sit_run"); }
    else if (h.speed < 0.25f) { horse = HA("horse_idle"); rider = HA("sit"); }
    else if (h.speed < 2.8f) { horse = HA("horse_walk"); rider = HA("sit_walk"); native = kHorseWalk; }
    else if (h.speed < 6.2f) { horse = HA("horse_run"); rider = HA("sit_run"); native = kHorseRun; }
    else { horse = HA("horse_sprint"); rider = HA("sit_galop"); native = kHorseSprint; }
    float rate = native > 0.0f ? std::clamp(h.speed / native, 0.6f, 1.3f) : 1.0f;
    CAnimBlendAssociation* ah = Play(ped, horse, 4.0f);
    CAnimBlendAssociation* ar = PlayRider(player, rider, 8.0f);
    if (ah) ah->m_fSpeed = rate;
    if (ar) ar->m_fSpeed = rate;
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
    static float zoom = 0.0f, lastWritten = 0.0f, lastBase = 0.0f;
    bool aim = gRiding && (player->bIsAimingGun || player->bFiringWeapon);
    zoom = std::clamp(zoom + (aim ? 1.0f : -1.0f) * dt / 0.25f, 0.0f, 1.0f);
    float cur = CDraw::ms_fFOV;
    float base = (lastWritten != 0.0f && cur == lastWritten) ? lastBase : cur;
    if (zoom > 0.001f) {
        CDraw::ms_fFOV = base * (1.0f - 0.4f * zoom);
        lastWritten = CDraw::ms_fFOV; lastBase = base;
    } else {
        lastWritten = 0.0f;
    }
}

// Um passo por frame: nasce/some os cavalos dos locais, atualiza cada um. useHit/jumpHit: tecla pressionada neste frame.
void UpdateHorses(unsigned now, CPed* player) {
    if (!gHorseInit) InitHorse();
    if (!gHorseOn) return;
    static unsigned last = 0;
    float dt = last ? std::min(0.1f, (now - last) / 1000.0f) : 0.0f;
    last = now;
    CPad* pad = CPad::GetPad(0);
    bool useHit = pad->NewState.ButtonTriangle > 0 && pad->OldState.ButtonTriangle <= 0;
    bool jumpHit = pad->NewState.ButtonSquare > 0 && pad->OldState.ButtonSquare <= 0;
    CVector pp = player->GetPosition();
    for (int i = 0; i < kHorseSpotN; ++i) {
        Horse& h = gHorses[i];
        const HorseSpot& sp = kHorseSpots[i];
        if (!h.ped) {
            if (std::hypot(sp.x - pp.x, sp.y - pp.y) > kHorseSpawnDist) continue;
            bool found = false;
            float gz = CWorld::FindGroundZFor3DCoord(sp.x, sp.y, sp.z < -99.0f ? 100.0f : sp.z + 30.0f, &found, nullptr);
            if (!found || (sp.z > -99.0f && std::fabs(gz - sp.z) > 30.0f)) continue; // chao ainda nao carregou
            SpawnHorse(i, sp.x, sp.y, gz, sp.heading, now);
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
        UpdateHorse(i, now, dt, player, pad, useHit && (gRider < 0 || gRider == i), jumpHit);
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
    if (SpawnHorse(kHorseManual, p.x, p.y, gz, player->m_fHeadingCurrent * 57.29578f + 180.0f, now)) Toast("Spawnado: Cavalo", now);
    else Toast("Carregando o modelo do cavalo... aperte Enter de novo", now);
}

void ResetHorses() { // novo jogo/load: os ponteiros antigos nao valem mais (sem chamar o jogo)
    for (auto& h : gHorses) h = Horse{};
    gHorseModel = ModelRef{ "cavalo" };
    gRiding = false; gRider = -1;
}
