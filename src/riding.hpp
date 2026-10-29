// Contas puras da cavalgada (sem dependencia do jogo, para teste): rampa de velocidade, marcha pela velocidade e limite da mira.
#pragma once
#include <cmath>

namespace mm {

inline float wrapAngle(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

// Aproxima v de alvo em no maximo accel*dt (sobe e desce com a mesma regra).
inline float approach(float v, float target, float accel, float dt) {
    float step = accel * dt;
    return v < target ? (v + step < target ? v + step : target) : (v - step > target ? v - step : target);
}

// 0 parado, 1 andar, 2 correr, 3 galope; cortes (andar ate 1,5 m/s, trotar ate 6,4 m/s; o ciclo toca ate 1,6x).
inline int gait(float speed) { return speed < 0.25f ? 0 : speed < 1.5f ? 1 : speed < 6.4f ? 2 : 3; }

// Quanto o rumo rel (rad, relativo ao cavalo) passa do limite; 0 se esta dentro. Limite <= 0 desliga.
inline float aimExcess(float rel, float limit) {
    if (limit <= 0.0f || std::fabs(rel) <= limit) return 0.0f;
    return rel - std::copysign(limit, rel);
}

} // namespace mm
