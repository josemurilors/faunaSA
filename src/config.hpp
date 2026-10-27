// Leitor dos .ini do Myths Maker 2.7 (Nadalao e Tarzan_3). Sem dependencias do jogo.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace mm {

struct Location { float x = 0, y = 0, z = 0, angle = 0; };

class Ini {
public:
    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string line, section;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';') continue;
            if (line[0] == '[') {
                auto e = line.find(']');
                if (e != std::string::npos) section = lower(line.substr(1, e - 1));
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            values_[section + "/" + lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
        }
        return true;
    }
    std::string str(const std::string& sec, const std::string& key, const std::string& def = "") const {
        auto it = values_.find(lower(sec) + "/" + lower(key));
        return it == values_.end() ? def : it->second;
    }
    int num(const std::string& sec, const std::string& key, int def = 0) const {
        auto s = str(sec, key);
        return s.empty() ? def : std::atoi(s.c_str());
    }
    float real(const std::string& sec, const std::string& key, float def = 0) const {
        auto s = str(sec, key);
        return s.empty() ? def : (float)std::atof(s.c_str());
    }
    bool has(const std::string& sec, const std::string& key) const {
        return values_.count(lower(sec) + "/" + lower(key)) > 0;
    }
private:
    static std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? "" : s.substr(a, b - a + 1);
    }
    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }
    std::map<std::string, std::string> values_;
};

// Um animal/mito: campos que o script CLEO original le do .ini.
struct MythConfig {
    float findRadius = 0, spawnRadius = 0, interactionRadius = 0, maxHeightDiff = 0;
    int locationMode = 0;
    int startHour = 0, endHour = 0, spawnTime = 0, respawnTime = 1;
    int quantity = 1, health = 100, weaponId = 0, damage = 0, accuracy = 0, attackRate = 0, fightingStyle = 0, money = 0;
    std::string customAnimGroup;
    int animGroup = -1;
    int walkMode = 1;
    bool beheadable = false, damageAnim = false, bulletproof = false, fireproof = false, explosionproof = false, meleeproof = false;
    bool attacksPeds = false, attacksAnimals = false, recruitable = false;
    std::vector<Location> locations;

    static bool load(const std::string& path, MythConfig& out) {
        Ini ini;
        if (!ini.load(path)) return false;
        out.findRadius = ini.real("RADIUS", "find radius", 100);
        out.spawnRadius = ini.real("RADIUS", "spawn radius", 67);
        out.interactionRadius = ini.real("RADIUS", "interaction radius");
        out.maxHeightDiff = ini.real("RADIUS", "max height difference", 10);
        out.locationMode = ini.num("LOCATIONS", "location mode");
        out.startHour = ini.num("TIME", "starting hour");
        out.endHour = ini.num("TIME", "ending hour");
        out.spawnTime = ini.num("TIME", "spawn time");
        out.respawnTime = ini.num("TIME", "respawn time", 1);
        out.quantity = ini.num("ATRIBUTES", "myth quantity", 1);
        out.health = ini.num("ATRIBUTES", "health", 100);
        out.weaponId = ini.num("ATRIBUTES", "weapon id");
        out.damage = ini.num("ATRIBUTES", "damage");
        out.accuracy = ini.num("ATRIBUTES", "attack accuracy");
        out.attackRate = ini.num("ATRIBUTES", "attack rate");
        out.fightingStyle = ini.num("ATRIBUTES", "fighting style");
        // animgroup=<n> e um grupo do jogo; alguns .ini trazem o nome (wolf, dog) no lugar de "custom animgroup".
        std::string ag = ini.str("ATRIBUTES", "animgroup");
        out.customAnimGroup = ini.str("ATRIBUTES", "custom animgroup");
        out.animGroup = -1;
        if (!ag.empty() && (std::isalpha((unsigned char)ag[0]) != 0)) {
            if (out.customAnimGroup.empty()) out.customAnimGroup = ag;
        } else if (!ag.empty()) {
            out.animGroup = std::atoi(ag.c_str());
        }
        out.money = ini.num("MISC", "money");
        out.beheadable = ini.num("RESISTENCES", "can be beheaded") != 0;
        out.damageAnim = ini.num("RESISTENCES", "does damage animation") != 0;
        out.bulletproof = ini.num("RESISTENCES", "bulletproof") != 0;
        out.fireproof = ini.num("RESISTENCES", "fireproof") != 0;
        out.explosionproof = ini.num("RESISTENCES", "explosionproof") != 0;
        out.meleeproof = ini.num("RESISTENCES", "meleeproof") != 0;
        out.walkMode = ini.num("MISC", "walk mode", 1);
        out.attacksPeds = ini.num("MISC", "attacks peds") != 0;
        out.attacksAnimals = ini.num("MISC", "attacks animals") != 0;
        out.recruitable = ini.num("MISC", "recruitable") != 0;
        // Locais sao numerados a partir de 1, sem buracos.
        for (int i = 1; ini.has("LOCATIONS", "location " + std::to_string(i) + " x"); ++i) {
            std::string p = "location " + std::to_string(i) + " ";
            Location l{ ini.real("LOCATIONS", p + "x"), ini.real("LOCATIONS", p + "y"), ini.real("LOCATIONS", p + "z"), ini.real("LOCATIONS", p + "angle") };
            if (l.x == 0 && l.y == 0 && l.z == 0) continue; // vaga nao preenchida do .ini (0,0,0 e a fazenda Blueberry)
            out.locations.push_back(l);
        }
        return true;
    }
};

} // namespace mm
