// Sons .mp3 pelo bass.dll que o proprio jogo/CLEO ja carregam (carregado em tempo de execucao, sem header nem .lib do BASS).
// Cada Play cria um stream curto, ajusta o volume e libera quando termina; sem som 3D: o volume cai com a distancia.
#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace snd {

inline HMODULE gBass = nullptr;
inline bool gInit = false, gFailed = false;
using QWORD = unsigned long long;
inline BOOL(WINAPI* BASS_Init)(int, DWORD, DWORD, HWND, const void*) = nullptr;
inline DWORD(WINAPI* BASS_StreamCreateFile)(BOOL, const void*, QWORD, QWORD, DWORD) = nullptr;
inline BOOL(WINAPI* BASS_ChannelPlay)(DWORD, BOOL) = nullptr;
inline BOOL(WINAPI* BASS_ChannelSetAttribute)(DWORD, DWORD, float) = nullptr;
inline DWORD(WINAPI* BASS_ChannelIsActive)(DWORD) = nullptr;
inline BOOL(WINAPI* BASS_StreamFree)(DWORD) = nullptr;
inline std::vector<DWORD> gActive;

constexpr DWORD kAttribVol = 2, kAttribPan = 3, kActivePlaying = 1;
constexpr size_t kMaxVoices = 12;

inline bool init() {
    if (gInit) return true;
    if (gFailed) return false;
    gBass = GetModuleHandleA("bass.dll");
    if (!gBass) gBass = LoadLibraryA("bass.dll");
    auto get = [&](const char* n) { return gBass ? GetProcAddress(gBass, n) : nullptr; };
    BASS_Init = (decltype(BASS_Init))get("BASS_Init");
    BASS_StreamCreateFile = (decltype(BASS_StreamCreateFile))get("BASS_StreamCreateFile");
    BASS_ChannelPlay = (decltype(BASS_ChannelPlay))get("BASS_ChannelPlay");
    BASS_ChannelSetAttribute = (decltype(BASS_ChannelSetAttribute))get("BASS_ChannelSetAttribute");
    BASS_ChannelIsActive = (decltype(BASS_ChannelIsActive))get("BASS_ChannelIsActive");
    BASS_StreamFree = (decltype(BASS_StreamFree))get("BASS_StreamFree");
    if (!BASS_Init || !BASS_StreamCreateFile || !BASS_ChannelPlay || !BASS_ChannelSetAttribute || !BASS_ChannelIsActive || !BASS_StreamFree) { gFailed = true; return false; }
    BASS_Init(-1, 44100, 0, nullptr, nullptr); // ja iniciado pelo CLEO/jogo: devolve erro e segue
    gInit = true;
    return true;
}

// Volume de efeitos do menu do jogo (0..1), o mesmo que o CLEO le com GET_AUDIO_SFX_VOLUME.
inline float sfxVolume() { return std::clamp(*(float*)0xB5FCCC, 0.0f, 1.0f); }

// Toca o arquivo uma vez. vol 0..1 (ja com a distancia); pan -1 (esquerda) .. 1 (direita). false se nao deu para tocar.
inline bool play(const std::string& file, float vol, float pan = 0.0f) {
    if (vol <= 0.01f || !init() || gActive.size() >= kMaxVoices) return false;
    DWORD h = BASS_StreamCreateFile(FALSE, file.c_str(), 0, 0, 0);
    if (!h) return false;
    BASS_ChannelSetAttribute(h, kAttribVol, std::clamp(vol, 0.0f, 1.0f));
    BASS_ChannelSetAttribute(h, kAttribPan, std::clamp(pan, -1.0f, 1.0f));
    if (!BASS_ChannelPlay(h, FALSE)) { BASS_StreamFree(h); return false; }
    gActive.push_back(h);
    return true;
}

// Libera os que terminaram; chamar uma vez por tick.
inline void update() {
    if (!gInit) return;
    gActive.erase(std::remove_if(gActive.begin(), gActive.end(), [](DWORD h) {
        if (BASS_ChannelIsActive(h) == kActivePlaying) return false;
        BASS_StreamFree(h);
        return true;
    }), gActive.end());
}

// Volume por distancia: cheio ate near, zero em far.
inline float falloff(float dist, float nearD = 5.0f, float farD = 60.0f) { return std::clamp((farD - dist) / (farD - nearD), 0.0f, 1.0f); }

} // namespace snd
