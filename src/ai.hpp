// Comportamento dos animais (sem dependencias do jogo): vagar, fugir, perseguir e atacar.
// Regras derivadas dos campos do .ini do Myths Maker (walk mode, attacks peds, damage, attack rate, spawn radius).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mm {

enum class AiState { Idle, Wander, Flee, Chase };
enum class Move { Idle, Walk, Run, Sprint };

struct AiCfg {
    bool wander = true;         // walk mode = 1
    bool attacksPeds = false;   // attacks peds = 1: ataca o ped mais proximo (inclusive o jogador)
    bool skittish = false;      // presa (cervo, coelho): foge do jogador quando ele chega perto
    float wanderRadius = 30;    // em volta do local de nascimento
    float detectRange = 25;     // distancia em que um atacante percebe um alvo
    float reach = 2.0f;         // distancia do ataque
    unsigned attackIntervalMs = 1500;
};

struct AiBrain {
    AiState state = AiState::Idle;
    float homeX = 0, homeY = 0;
    float tx = 0, ty = 0;       // destino do wander
    unsigned stateUntil = 0;    // fim da pausa / do wander / da fuga
    unsigned nextAttack = 0;
    unsigned hurtUntil = 0;
    unsigned dodgeFrom = 0, dodgeUntil = 0; // sai da frente de um carro (depois da buzina): corre para dodgeHeading
    float dodgeHeading = 0;
};

struct AiWorld {
    unsigned now = 0;
    float x = 0, y = 0;                                   // animal
    bool hasTarget = false;                               // ped mais proximo ao alcance (alvo de ataque)
    float targetX = 0, targetY = 0, targetDist = 1e9f;
    float playerX = 0, playerY = 0, playerDist = 1e9f;
    bool hurt = false;                                    // levou dano desde o passo anterior
};

struct AiOut {
    Move move = Move::Idle;
    float heading = 0;       // alvo de heading, convencao do jogo: frente = (-sin h, cos h)
    bool hasHeading = false;
    bool attack = false;
};

constexpr float kFleeDistance = 15.0f;    // presa foge do jogador abaixo disso
constexpr float kFleeSafeDistance = 40.0f;
constexpr unsigned kHurtMs = 8000;

inline float headingTo(float dx, float dy) { return std::atan2(-dx, dy); }
inline float rnd01() { return (float)std::rand() / (float)RAND_MAX; }

inline unsigned attackInterval(int attackRate) { // attack rate 0..100: mais alto ataca mais rapido
    return (unsigned)std::clamp(3000 - 15 * attackRate, 1500, 3000); // nunca mais rapido que 1,5 s: da para fugir
}

inline AiOut aiStep(AiBrain& b, const AiCfg& c, const AiWorld& w) {
    AiOut o;
    if (w.hurt) b.hurtUntil = w.now + kHurtMs;

    // 0. Carro vindo na direcao do animal (buzinou): corre para o lado, antes de qualquer outra coisa.
    if (w.now >= b.dodgeFrom && w.now < b.dodgeUntil) {
        b.state = AiState::Flee;
        b.stateUntil = b.dodgeUntil;
        o.heading = b.dodgeHeading;
        o.hasHeading = true;
        o.move = Move::Sprint;
        return o;
    }

    // 1. Atacante com alvo ao alcance: persegue e morde.
    if (c.attacksPeds && w.hasTarget && w.targetDist <= c.detectRange) {
        b.state = AiState::Chase;
        o.heading = headingTo(w.targetX - w.x, w.targetY - w.y);
        o.hasHeading = true;
        if (w.targetDist <= c.reach) {
            o.move = Move::Idle;
            if (w.now >= b.nextAttack) { o.attack = true; b.nextAttack = w.now + c.attackIntervalMs; }
        } else {
            o.move = Move::Sprint;
        }
        return o;
    }

    // 2. Presa (ou animal machucado pelo jogador): foge.
    bool scared = (c.skittish && w.playerDist < kFleeDistance) || (w.now < b.hurtUntil && w.playerDist < kFleeSafeDistance);
    if (!c.attacksPeds && (scared || (b.state == AiState::Flee && w.now < b.stateUntil && w.playerDist < kFleeSafeDistance))) {
        if (b.state != AiState::Flee) b.stateUntil = w.now + 4000;
        b.state = AiState::Flee;
        o.heading = headingTo(w.x - w.playerX, w.y - w.playerY); // para longe do jogador
        o.hasHeading = true;
        o.move = Move::Sprint;
        return o;
    }
    if (b.state == AiState::Flee || b.state == AiState::Chase) { b.state = AiState::Idle; b.stateUntil = w.now + 1500; }

    // 3. Vagar / ficar parado.
    if (b.state == AiState::Idle) {
        if (w.now >= b.stateUntil && c.wander) {
            float a = 6.2831853f * rnd01(), d = c.wanderRadius * std::sqrt(rnd01());
            b.tx = b.homeX + std::cos(a) * d;
            b.ty = b.homeY + std::sin(a) * d;
            b.state = AiState::Wander;
            b.stateUntil = w.now + 15000; // desiste se nao chegar
        }
        return o; // Move::Idle
    }
    // Wander
    float dx = b.tx - w.x, dy = b.ty - w.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.5f || w.now >= b.stateUntil) {
        b.state = AiState::Idle;
        b.stateUntil = w.now + 1000 + (unsigned)(3000 * rnd01());
        return o;
    }
    o.move = Move::Walk;
    o.heading = headingTo(dx, dy);
    o.hasHeading = true;
    return o;
}

} // namespace mm
