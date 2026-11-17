#include "Ui.h"
#include "Arena.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

bool g_ansi = false;
bool g_color = false;
bool g_unicode = true;

// True when the environment variable `name` is set to "1". MSVC's /sdl treats plain getenv() as an
// error (C4996), so use the safe _dupenv_s there.
bool envFlagSet(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) return false;
    const bool on = value[0] == '1';
    std::free(value);
    return on;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] == '1';
#endif
}

const char* col(const char* code) { return g_color ? code : ""; }
const char* RESET = "\x1b[0m";
const char* DIM = "\x1b[2m";
const char* GREEN = "\x1b[32m";
const char* RED = "\x1b[31m";
const char* YELLOW = "\x1b[33m";
const char* CYAN = "\x1b[36m";

std::string bar(double frac, int width) {
    frac = std::clamp(frac, 0.0, 1.0);
    const int filled = static_cast<int>(std::lround(frac * width));
    return "[" + std::string(static_cast<size_t>(filled), '#') + std::string(static_cast<size_t>(width - filled), '.') + "]";
}

const char* kindName(AbilityKind k) {
    switch (k) {
    case AbilityKind::Basic: return "Basic";
    case AbilityKind::Special: return "Special";
    case AbilityKind::Leader: return "Leader";
    case AbilityKind::Unique: return "Unique";
    case AbilityKind::Ultimate: return "Ultimate";
    }
    return "?";
}

std::string shorten(const std::string& s, size_t n) {
    return s.size() <= n ? s : s.substr(0, n > 3 ? n - 3 : n) + "...";
}

} // namespace

namespace ui {

std::string withCommas(double v) {
    long long n = std::llround(v);
    const bool neg = n < 0;
    std::string d = std::to_string(neg ? -n : n);
    for (int i = static_cast<int>(d.size()) - 3; i > 0; i -= 3) d.insert(static_cast<size_t>(i), ",");
    return neg ? "-" + d : d;
}

bool setup(bool wantColor) {
    g_ansi = false;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    // GetConsoleMode only succeeds when stdout is a real console (not redirected).
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        g_ansi = SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/) != 0;
#else
    g_ansi = isatty(fileno(stdout)) != 0;
#endif
    if (envFlagSet("GOHCLONE_FORCE_ANSI")) g_ansi = true;
    g_color = wantColor && g_ansi;
    return g_ansi;
}

bool ansiAvailable() { return g_ansi; }
bool colorEnabled() { return g_color; }
void setColor(bool on) { g_color = on && g_ansi; }
bool unicodeEnabled() { return g_unicode; }
void setUnicode(bool on) { g_unicode = on; }

void consoleSize(int& cols, int& rows) {
    cols = rows = 0;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        cols = info.srWindow.Right - info.srWindow.Left + 1;
        rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
#endif
}

void clearScreen() {
    if (g_ansi) {
        std::fputs("\x1b[2J\x1b[H", stdout);
        std::fflush(stdout);
    }
}

std::size_t visibleWidth(const std::string& s) {
    std::size_t w = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0x1b) { // skip an ANSI escape sequence: ESC [ ... final byte
            if (i + 1 < s.size() && s[i + 1] == '[') {
                i += 2;
                while (i < s.size() && !std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
            }
            continue;
        }
        if ((c & 0xC0) != 0x80) ++w; // count UTF-8 lead bytes only
    }
    return w;
}

std::string statusLine(const Battle&, const BattleUnit& u) {
    std::string out;
    for (const StatusInst& si : u.statuses) {
        if (si.fromAura) continue; // leader / unique auras are shown by 'inspect'
        const char sign = si.def->kind == StatusKind::Buff ? '+' : si.def->kind == StatusKind::Debuff ? '-' : '~';
        if (!out.empty()) out += "  ";
        out += sign;
        out += si.def->name;
        if (si.stacks > 1) out += " x" + std::to_string(si.stacks);
        if (si.remaining > 0) out += "(" + std::to_string(si.remaining) + ")";
    }
    return out;
}

void printState(const Battle& b) {
    std::printf("\n%s=================== Turn %d ====================%s\n", col(CYAN), b.turnsTaken, col(RESET));
    for (int side = 0; side < 2; ++side) {
        std::printf("%s%s%s\n", col(side == 0 ? GREEN : RED), side == 0 ? " TEAM A" : " TEAM B", col(RESET));
        for (const BattleUnit& u : b.units) {
            if (u.side != side) continue;
            const char* lead = u.isLeader ? "*" : " ";
            if (!u.alive) {
                std::printf("  %s#%d %-22s%s%s DEFEATED%s\n", col(DIM), u.id + 1, u.name.c_str(), lead, "", col(RESET));
                continue;
            }
            const double mh = b.maxHealth(u), mp = b.maxProtection(u);
            std::printf("  #%d %-22s%s HP %7s/%-7s %s  PR %7s %s  TM %3d%%  SPD %3.0f\n", u.id + 1, u.name.c_str(), lead,
                        withCommas(u.health).c_str(), withCommas(mh).c_str(), bar(u.health / mh, 10).c_str(),
                        withCommas(u.protection).c_str(), bar(mp > 0 ? u.protection / mp : 0, 6).c_str(),
                        static_cast<int>(std::lround(std::min(u.tm, 9.99) * 100)), b.stat(u, Stat::Speed));
            const std::string s = statusLine(b, u);
            if (!s.empty()) std::printf("       %s%s%s\n", col(YELLOW), s.c_str(), col(RESET));
        }
    }
}

void inspect(const Battle& b, const BattleUnit& u) {
    std::printf("\n%s%s%s  (%s, level %d)  tags: ", col(CYAN), u.def->name.c_str(), col(RESET), u.isLeader ? "leader" : "member",
                u.def->level);
    for (size_t i = 0; i < u.def->tags.size(); ++i) std::printf("%s%s", i ? ", " : "", u.def->tags[i].c_str());
    std::printf("\n");
    const double armor = b.stat(u, Stat::Armor), res = b.stat(u, Stat::Resistance);
    std::printf("  Health %s  Protection %s  Speed %.0f\n", withCommas(b.maxHealth(u)).c_str(),
                withCommas(b.maxProtection(u)).c_str(), b.stat(u, Stat::Speed));
    std::printf("  Phys Dmg %s  Spec Dmg %s  Crit Dmg %.0f%%\n", withCommas(b.stat(u, Stat::PhysDamage)).c_str(),
                withCommas(b.stat(u, Stat::SpecDamage)).c_str(), b.stat(u, Stat::CritDamage) * 100);
    std::printf("  Crit Chance %.1f%% phys / %.1f%% spec   Crit Avoid %.1f%% / %.1f%%\n", b.stat(u, Stat::PhysCrit) * 100,
                b.stat(u, Stat::SpecCrit) * 100, b.stat(u, Stat::PhysCritAvoid) * 100, b.stat(u, Stat::SpecCritAvoid) * 100);
    std::printf("  Armor %.0f (%.1f%% mitigation)  Resistance %.0f (%.1f%% mitigation)\n", armor,
                b.rules.mitigation(armor, u.def->level) * 100, res, b.rules.mitigation(res, u.def->level) * 100);
    std::printf("  Potency %.1f%%  Tenacity %.1f%%  Accuracy %.1f%%  Dodge %.1f%%  Deflection %.1f%%  Health Steal %.1f%%\n",
                b.stat(u, Stat::Potency) * 100, b.stat(u, Stat::Tenacity) * 100, b.stat(u, Stat::Accuracy) * 100,
                b.stat(u, Stat::Dodge) * 100, b.stat(u, Stat::Deflection) * 100, b.stat(u, Stat::HealthSteal) * 100);
    for (const AbilityState& as : u.abilities) {
        const AbilityDef& a = *as.def;
        std::printf("  [%s] %s", kindName(a.kind), a.name.c_str());
        if (a.cooldown > 0) std::printf("  (cooldown %d)", a.cooldown);
        std::printf("\n");
        if (!a.description.empty()) std::printf("      %s\n", a.description.c_str());
    }
    for (const StatusInst& si : u.statuses) {
        std::printf("  status: %s%s%s", si.def->name.c_str(), si.stacks > 1 ? (" x" + std::to_string(si.stacks)).c_str() : "",
                    si.fromAura ? " (aura)" : "");
        if (si.remaining > 0) std::printf(" (%d turns)", si.remaining);
        std::printf("\n");
    }
}

void printResult(const Battle& b) {
    const int w = b.winner();
    std::printf("\n%s==================================================%s\n", col(CYAN), col(RESET));
    if (w == 0) std::printf("%s TEAM A WINS%s after %d turns\n", col(GREEN), col(RESET), b.turnsTaken);
    else if (w == 1) std::printf("%s TEAM B WINS%s after %d turns\n", col(RED), col(RESET), b.turnsTaken);
    else std::printf(" DRAW after %d turns\n", b.turnsTaken);
}

void printReport(const Battle& b, const std::string& labelA, const std::string& labelB) {
    std::printf("\n BATTLE REPORT\n");
    for (int side = 0; side < 2; ++side) {
        std::printf("\n %s - %s\n", side == 0 ? "TEAM A" : "TEAM B", (side == 0 ? labelA : labelB).c_str());
        std::printf("   %-22s %10s %10s %9s %6s %6s %6s  %s\n", "unit", "dmg dealt", "dmg taken", "healing", "kills", "crits",
                    "turns", "result");
        for (const BattleUnit& u : b.units) {
            if (u.side != side) continue;
            std::string result = u.alive ? "alive, " + withCommas(u.health) + " HP" : "defeated";
            std::printf("   %-22s %10s %10s %9s %6d %6d %6d  %s\n", u.name.c_str(), withCommas(static_cast<double>(u.st.damageDealt)).c_str(),
                        withCommas(static_cast<double>(u.st.damageTaken)).c_str(),
                        withCommas(static_cast<double>(u.st.healingDone)).c_str(), u.st.kills, u.st.crits, u.st.turns,
                        result.c_str());
        }
    }
}

} // namespace ui

// ---------------------------------------------------------------- human ------

namespace {

bool readLine(std::string& out) {
    std::cout.flush();
    if (!std::getline(std::cin, out)) return false;
    while (!out.empty() && (out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return true;
}

} // namespace

Decision HumanController::decide(Battle& b, BattleUnit& u) {
    if (autoMode) return ai_.decide(b, u);
    return arena ? decideArena(b, u) : decideLog(b, u);
}

// ---- classic text-log prompt ----
Decision HumanController::decideLog(Battle& b, BattleUnit& u) {
    ui::printState(b);

    for (;;) {
        std::printf("\n>>> %s's turn\n", u.name.c_str());
        std::vector<std::size_t> usable;
        int n = 0;
        for (std::size_t i = 0; i < u.abilities.size(); ++i) {
            const AbilityDef& a = *u.abilities[i].def;
            if (a.kind == AbilityKind::Leader || a.kind == AbilityKind::Unique) continue;
            ++n;
            std::string why;
            const bool ok = b.abilityUsable(u, i, &why);
            if (ok) usable.push_back(i);
            std::printf("  %d) %-22s %s", n, a.name.c_str(), kindName(a.kind));
            if (!ok) std::printf("  -- unavailable: %s", why.c_str());
            std::printf("\n");
            if (!a.description.empty()) std::printf("       %s\n", a.description.c_str());
        }
        std::printf("Choose ability number  |  a = auto-play the rest  |  i N = inspect unit #N  |  q = quit\n> ");
        std::string line;
        if (!readLine(line)) { autoMode = true; return ai_.decide(b, u); }
        if (line == "q" || line == "quit") throw QuitRequested{};
        if (line == "a" || line == "auto") { autoMode = true; return ai_.decide(b, u); }
        if (line.size() > 1 && line[0] == 'i') {
            const int k = std::atoi(line.c_str() + 1);
            if (k >= 1 && k <= static_cast<int>(b.units.size())) ui::inspect(b, b.units[static_cast<size_t>(k - 1)]);
            else std::printf("No such unit.\n");
            continue;
        }
        const int choice = std::atoi(line.c_str());
        if (choice < 1 || choice > n) { std::printf("Please enter a number from 1 to %d.\n", n); continue; }

        std::size_t ai = 0;
        int seen = 0;
        for (std::size_t i = 0; i < u.abilities.size(); ++i) {
            const AbilityKind k = u.abilities[i].def->kind;
            if (k == AbilityKind::Leader || k == AbilityKind::Unique) continue;
            if (++seen == choice) { ai = i; break; }
        }
        if (std::find(usable.begin(), usable.end(), ai) == usable.end()) { std::printf("That ability is not available.\n"); continue; }

        Decision d;
        d.ability = static_cast<int>(ai);
        const AbilityDef& ab = *u.abilities[ai].def;
        const std::vector<int> targets = b.validTargets(u, ab);
        if (ab.pick == Pick::Self) { d.target = u.id; return d; }
        if (targets.empty()) return d;
        if (targets.size() == 1) { d.target = targets[0]; return d; }
        for (std::size_t t = 0; t < targets.size(); ++t) {
            const BattleUnit& tu = b.units[static_cast<size_t>(targets[t])];
            std::printf("  %zu) %-22s HP %s/%s  PR %s\n", t + 1, tu.name.c_str(), ui::withCommas(tu.health).c_str(),
                        ui::withCommas(b.maxHealth(tu)).c_str(), ui::withCommas(tu.protection).c_str());
        }
        std::printf("Target number (blank = back)\n> ");
        if (!readLine(line)) { autoMode = true; return ai_.decide(b, u); }
        if (line.empty()) continue;
        const int tc = std::atoi(line.c_str());
        if (tc < 1 || tc > static_cast<int>(targets.size())) { std::printf("Invalid target.\n"); continue; }
        d.target = targets[static_cast<size_t>(tc - 1)];
        return d;
    }
}

// ---- arena prompt: drawn directly beneath the animated frame ----
Decision HumanController::decideArena(Battle& b, BattleUnit& u) {
    for (;;) {
        arena->showForInput(u.id);
        std::printf(" Your turn: %s  - choose an action\n", u.name.c_str());
        std::vector<std::size_t> usable;
        std::vector<std::size_t> menu; // displayed number -> ability index
        for (std::size_t i = 0; i < u.abilities.size(); ++i) {
            const AbilityDef& a = *u.abilities[i].def;
            if (a.kind == AbilityKind::Leader || a.kind == AbilityKind::Unique) continue;
            menu.push_back(i);
            std::string why;
            const bool ok = b.abilityUsable(u, i, &why);
            if (ok) usable.push_back(i);
            std::string info = ok ? shorten(a.description, 74) : "-- unavailable: " + why;
            std::printf("  %zu) %-18s %-8s %s\n", menu.size(), a.name.c_str(), kindName(a.kind), info.c_str());
        }
        std::printf("  [number] choose   a = auto-play the rest   i N = inspect unit #N   q = quit\n> ");
        std::string line;
        if (!readLine(line)) { autoMode = true; return ai_.decide(b, u); }
        if (line == "q" || line == "quit") throw QuitRequested{};
        if (line == "a" || line == "auto") { autoMode = true; return ai_.decide(b, u); }
        if (line.size() > 1 && line[0] == 'i') {
            const int k = std::atoi(line.c_str() + 1);
            if (k >= 1 && k <= static_cast<int>(b.units.size())) {
                ui::inspect(b, b.units[static_cast<size_t>(k - 1)]);
                std::printf("\nPress Enter to go back...");
                readLine(line);
            }
            continue;
        }
        const int choice = std::atoi(line.c_str());
        if (choice < 1 || choice > static_cast<int>(menu.size())) continue;
        const std::size_t ai = menu[static_cast<size_t>(choice - 1)];
        if (std::find(usable.begin(), usable.end(), ai) == usable.end()) continue;

        Decision d;
        d.ability = static_cast<int>(ai);
        const AbilityDef& ab = *u.abilities[ai].def;
        const std::vector<int> targets = b.validTargets(u, ab);
        if (ab.pick == Pick::Self) { d.target = u.id; return d; }
        if (targets.empty()) return d;
        if (targets.size() == 1) { d.target = targets[0]; return d; }

        for (;;) {
            arena->showForInput(u.id);
            std::printf(" %s - choose a target by its #number:\n", ab.name.c_str());
            for (int id : targets) {
                const BattleUnit& tu = b.units[static_cast<size_t>(id)];
                std::printf("  #%-2d %-22s HP %s / %s   Protection %s\n", id + 1, tu.name.c_str(), ui::withCommas(tu.health).c_str(),
                            ui::withCommas(b.maxHealth(tu)).c_str(), ui::withCommas(tu.protection).c_str());
            }
            std::printf("  (blank = go back)\n> ");
            if (!readLine(line)) { autoMode = true; return ai_.decide(b, u); }
            if (line.empty()) break;
            const int num = std::atoi(line.c_str()) - 1;
            if (std::find(targets.begin(), targets.end(), num) != targets.end()) {
                d.target = num;
                return d;
            }
        }
    }
}
