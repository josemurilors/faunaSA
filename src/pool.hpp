// Logica pura (sem jogo) do spawn: pool de slots fixo, janela de horario e escolha de local.
#pragma once
#include "config.hpp"
#include <array>
#include <cmath>

namespace mm {

// N slots fixos: nunca aloca em tempo de jogo. acquire() devolve -1 quando cheio.
template <size_t N>
class SlotPool {
public:
    int acquire() {
        for (size_t i = 0; i < N; ++i)
            if (!used_[i]) { used_[i] = true; ++count_; return (int)i; }
        return -1;
    }
    void release(int i) {
        if (i >= 0 && (size_t)i < N && used_[i]) { used_[i] = false; --count_; }
    }
    bool used(int i) const { return used_[i]; }
    size_t count() const { return count_; }
    static constexpr size_t capacity() { return N; }
private:
    std::array<bool, N> used_{};
    size_t count_ = 0;
};

// starting hour == ending hour significa "sempre" (como no .ini original); a janela pode virar a meia-noite.
inline bool hourInWindow(int hour, int start, int end) {
    if (start == end) return true;
    return start < end ? (hour >= start && hour < end) : (hour >= start || hour < end);
}

// Local mais proximo em 3D (como LOCATE_CHAR_DISTANCE_TO_COORDINATES do script original); -1 se nao houver locais.
inline int nearestLocation(const MythConfig& c, float px, float py, float pz, float* dist = nullptr) {
    int best = -1;
    float bestD = 0;
    for (size_t i = 0; i < c.locations.size(); ++i) {
        const Location& l = c.locations[i];
        float dx = l.x - px, dy = l.y - py, dz = l.z - pz;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (best < 0 || d < bestD) { bestD = d; best = (int)i; }
    }
    if (dist) *dist = bestD;
    return best;
}

// O script original ativa o mito quando o jogador esta a "raio do local + 120" do local (o raio padrao e o spawn radius).
inline float activationRadius(const MythConfig& c) { return c.spawnRadius + 120.0f; }

// Local ativado pelo jogador, ou -1.
inline int findLocation(const MythConfig& c, float px, float py, float pz) {
    float d;
    int i = nearestLocation(c, px, py, pz, &d);
    return (i >= 0 && d <= activationRadius(c)) ? i : -1;
}

} // namespace mm
