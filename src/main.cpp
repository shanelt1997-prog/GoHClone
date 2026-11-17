// GoHClone - text-mode squad battler engine. See README.md.
//
// Run with no arguments to get an interactive menu. Command-line arguments still work
// for scripting (see 'help').
#include "Ai.h"
#include "Arena.h"
#include "Battle.h"
#include "Calibrate.h"
#include "Defs.h"
#include "Golden.h"
#include "Rules.h"
#include "Ui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

int runSelfTest(const std::string& dataDir); // SelfTest.cpp

namespace fs = std::filesystem;

namespace {

struct InputClosed {}; // thrown when stdin ends (e.g. Ctrl+Z / Ctrl+D) while the menu is waiting

struct Speed {
    const char* name;
    int delayMs;
};
const Speed kSpeeds[] = {{"Slow   (about 1.5 minutes for a long battle)", 420}, {"Normal (about 40 seconds)", 180},
                         {"Fast   (about 15 seconds)", 70}, {"Instant (jump straight to the result)", 0}};

struct Options {
    std::string command = "menu";
    std::string dataDir;
    std::string teamA = "concord_strike";
    std::string teamB = "syndicate_raiders";
    std::string unitId;
    std::uint64_t seed = 0;
    bool seedGiven = false;
    int runs = 1000;
    bool color = true;      // used when the terminal supports it
    bool ascii = false;     // plain ASCII bars instead of block characters
    bool quiet = false;     // log mode: only print the final result
    bool viewArena = false; // animated arena view instead of the text log
    bool write = false;     // golden: (re)write the files instead of verifying them
    // calibrate
    double calStat = 0, calDefense = 0, calDefensePct = -1, calCrit = 1.0, calOther = 1.0;
    double calArmorPen = 0, calDefensePen = 0, calVariance = 0.05;
    int calLevel = 85;
    std::string calDamages;
    int delayMs = 180;
};

void usage() {
    std::puts(
        "GoHClone - text-mode squad battler engine\n"
        "\n"
        "Run with no arguments to open the interactive menu.\n"
        "\n"
        "Command-line usage: GoHClone <command> [options]\n"
        "\n"
        "Commands:\n"
        "  menu       Interactive main menu (default)\n"
        "  play       Fight a battle; you control Team A, the AI controls Team B\n"
        "  auto       Watch the AI play both teams\n"
        "  sim        Run many AI-vs-AI battles and print balance statistics\n"
        "  list       List teams and units in the data files\n"
        "  show <id>  Show a unit's stats and abilities\n"
        "  statuses [buff|debuff|text]  List every buff and debuff in the library\n"
        "  calibrate  Turn damage numbers seen in the real game into an ability multiplier (see below)\n"
        "  selftest   Run the built-in test suite\n"
        "  golden     Verify the golden battle fingerprints (add --write to regenerate them)\n"
        "\n"
        "Options:\n"
        "  --a <team|u1,u2,..>   Team A: a team id or comma-separated unit ids (first = leader)\n"
        "  --b <team|u1,u2,..>   Team B\n"
        "  --seed <n>            Random seed (default: time-based; always shown)\n"
        "  --runs <n>            Number of battles for 'sim' (default 1000)\n"
        "  --arena               Use the animated arena view for play/auto (the menu offers it too)\n"
        "  --speed <s>           Arena playback speed: slow | normal | fast | instant\n"
        "  --no-color            Disable colours\n"
        "  --ascii               Plain ASCII bars (if block characters look broken)\n"
        "  --data <dir>          Folder holding rules.json and content.json\n"
        "  --quiet               Log mode: print only the final result\n"
        "\n"
        "calibrate options (all numbers as shown in the game):\n"
        "  --stat <n>            attacker's Physical/Special Damage at the time of the hit\n"
        "  --defense <n>         target's flat Armor/Resistance rating, or\n"
        "  --defense-pct <p>     the damage-reduction % shown on the stat screen (e.g. 41.27)\n"
        "  --level <n>           target level (default 85)\n"
        "  --damage <a,b,c>      one or more damage numbers you observed\n"
        "  --crit <x>            critical damage multiplier if those were crits (e.g. 1.875)\n"
        "  --other <x>           other damage modifier in play (default 1.0)\n"
        "  --armor-pen <n>  --defense-pen <f>  --variance <f>   penetration / damage spread (default 0.05)\n");
}

bool parseSpeed(const std::string& s, int& delayMs) {
    if (s == "slow") delayMs = kSpeeds[0].delayMs;
    else if (s == "normal") delayMs = kSpeeds[1].delayMs;
    else if (s == "fast") delayMs = kSpeeds[2].delayMs;
    else if (s == "instant") delayMs = kSpeeds[3].delayMs;
    else return false;
    return true;
}

bool parseArgs(int argc, char** argv, Options& o) {
    int i = 1;
    if (i < argc && argv[i][0] != '-') o.command = argv[i++];
    if ((o.command == "show" || o.command == "statuses") && i < argc && argv[i][0] != '-') o.unitId = argv[i++];
    auto need = [&](const char* name) -> const char* {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "Missing value for %s\n", name);
            return nullptr;
        }
        return argv[++i];
    };
    for (; i < argc; ++i) {
        const std::string a = argv[i];
        const char* v = nullptr;
        if (a == "--a") { if (!(v = need("--a"))) return false; o.teamA = v; }
        else if (a == "--b") { if (!(v = need("--b"))) return false; o.teamB = v; }
        else if (a == "--seed") { if (!(v = need("--seed"))) return false; o.seed = std::strtoull(v, nullptr, 10); o.seedGiven = true; }
        else if (a == "--runs") { if (!(v = need("--runs"))) return false; o.runs = std::max(1, std::atoi(v)); }
        else if (a == "--data") { if (!(v = need("--data"))) return false; o.dataDir = v; }
        else if (a == "--speed") {
            if (!(v = need("--speed"))) return false;
            if (!parseSpeed(v, o.delayMs)) { std::fprintf(stderr, "Unknown speed '%s' (slow, normal, fast, instant)\n", v); return false; }
        }
        else if (a == "--stat") { if (!(v = need("--stat"))) return false; o.calStat = std::atof(v); }
        else if (a == "--defense") { if (!(v = need("--defense"))) return false; o.calDefense = std::atof(v); }
        else if (a == "--defense-pct") { if (!(v = need("--defense-pct"))) return false; o.calDefensePct = std::atof(v); }
        else if (a == "--level") { if (!(v = need("--level"))) return false; o.calLevel = std::max(1, std::atoi(v)); }
        else if (a == "--damage") { if (!(v = need("--damage"))) return false; o.calDamages = v; }
        else if (a == "--crit") { if (!(v = need("--crit"))) return false; o.calCrit = std::atof(v); }
        else if (a == "--other") { if (!(v = need("--other"))) return false; o.calOther = std::atof(v); }
        else if (a == "--armor-pen") { if (!(v = need("--armor-pen"))) return false; o.calArmorPen = std::atof(v); }
        else if (a == "--defense-pen") { if (!(v = need("--defense-pen"))) return false; o.calDefensePen = std::atof(v); }
        else if (a == "--variance") { if (!(v = need("--variance"))) return false; o.calVariance = std::atof(v); }
        else if (a == "--arena") o.viewArena = true;
        else if (a == "--write") o.write = true;
        else if (a == "--no-color") o.color = false;
        else if (a == "--color") o.color = true;
        else if (a == "--ascii") o.ascii = true;
        else if (a == "--quiet") o.quiet = true;
        else { std::fprintf(stderr, "Unknown option: %s\n", a.c_str()); return false; }
    }
    return true;
}

// Finds the data folder: --data, then a few places relative to the working directory / executable.
std::string findDataDir(const Options& o, const char* argv0) {
    std::vector<fs::path> tries;
    if (!o.dataDir.empty()) tries.push_back(o.dataDir);
    for (const char* rel : {"data", "../data", "../../data", "../../../data"}) tries.push_back(rel);
    std::error_code ec;
    fs::path exe = fs::absolute(argv0, ec).parent_path();
    for (const char* rel : {"data", "../data", "../../data", "../../../data"}) tries.push_back(exe / rel);
    for (const fs::path& p : tries)
        if (fs::exists(p / "content.json", ec)) return p.string();
    return std::string();
}

std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

bool resolveTeam(const Content& c, const std::string& spec, std::vector<std::string>& out, std::string& label) {
    if (const TeamDef* t = c.team(spec)) {
        out = t->units;
        label = t->name;
        return true;
    }
    out = splitCsv(spec);
    if (out.empty()) {
        std::fprintf(stderr, "Empty team '%s'\n", spec.c_str());
        return false;
    }
    for (const std::string& id : out)
        if (!c.units.count(id)) {
            std::fprintf(stderr, "'%s' is neither a team nor a unit id (try the 'list' command)\n", id.c_str());
            return false;
        }
    label = spec;
    return true;
}

// Random seeds are kept short (6 digits) so a battle you liked is easy to note down and replay.
std::uint64_t pickSeed(const Options& o) {
    if (o.seedGiven) return o.seed;
    return static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) % 1000000ULL;
}

// ------------------------------------------------------------ commands -------

void cmdList(const Content& c) {
    std::puts("Teams:");
    for (const std::string& id : c.teamOrder) {
        const TeamDef& t = c.teams.at(id);
        std::printf("  %-20s %s\n    ", t.id.c_str(), t.name.c_str());
        for (size_t i = 0; i < t.units.size(); ++i) std::printf("%s%s", i ? ", " : "", c.unit(t.units[i]).name.c_str());
        std::puts("");
    }
    std::puts("\nUnits:");
    for (const std::string& id : c.unitOrder) {
        const UnitDef& u = c.units.at(id);
        std::printf("  %-16s %-22s [", u.id.c_str(), u.name.c_str());
        for (size_t i = 0; i < u.tags.size(); ++i) std::printf("%s%s", i ? " " : "", u.tags[i].c_str());
        std::puts("]");
    }
}

int cmdShow(const Content& c, const Rules& rules, const std::string& id) {
    if (id.empty() || !c.units.count(id)) {
        std::fprintf(stderr, "Usage: show <unit id>   (see 'list')\n");
        return 1;
    }
    Battle b(c, rules, 1);
    b.addTeam(0, {id});
    ui::inspect(b, b.units[0]);
    return 0;
}

int cmdBattle(const Content& c, const Rules& rules, const Options& o, bool interactive) {
    std::vector<std::string> a, b;
    std::string la, lb;
    if (!resolveTeam(c, o.teamA, a, la) || !resolveTeam(c, o.teamB, b, lb)) return 1;
    const std::uint64_t seed = pickSeed(o);

    Battle battle(c, rules, seed);
    battle.addTeam(0, a);
    battle.addTeam(1, b);
    battle.logging = false;

    AiController ai;
    HumanController human;

    const bool arenaMode = o.viewArena && ui::ansiAvailable();
    if (o.viewArena && !arenaMode)
        std::puts("(The animated arena needs a terminal with colour/cursor support - showing the text log instead.)");

    if (arenaMode) {
        ArenaOptions ao;
        ao.delayMs = o.delayMs;
        ao.unicode = ui::unicodeEnabled();
        ao.color = ui::colorEnabled();
        ArenaView arena(battle, la, lb, seed, ao);
        battle.observer = &arena;
        battle.echo = false;
        human.arena = &arena;
        arena.begin();
        battle.start();
        try {
            if (interactive) battle.run(human, ai);
            else battle.run(ai, ai);
        } catch (const QuitRequested&) {
            arena.abort();
            std::puts("\nBattle abandoned.");
            return 0;
        }
        arena.finish();
        // Keep the final arena (with the winner banner) on screen; the report is taller than a
        // typical window and would push it out of view.
        std::printf("\n  Press Enter to see the battle report...");
        std::fflush(stdout);
        {
            std::string dummy;
            std::getline(std::cin, dummy);
        }
        ui::clearScreen();
        ui::printResult(battle);
        ui::printReport(battle, la, lb);
        return 0;
    }

    std::printf("Team A: %s\nTeam B: %s\nSeed: %llu\n", la.c_str(), lb.c_str(), static_cast<unsigned long long>(seed));
    battle.echo = !o.quiet || interactive;
    battle.start();
    try {
        if (interactive) battle.run(human, ai);
        else battle.run(ai, ai);
    } catch (const QuitRequested&) {
        std::puts("\nBattle abandoned.");
        return 0;
    }
    if (!interactive || !human.autoMode) ui::printState(battle);
    ui::printResult(battle);
    return 0;
}

// Prints `text` word-wrapped at `width`, continuation lines indented by `indent` spaces.
void printWrapped(const std::string& first, const std::string& text, size_t indent, size_t width) {
    std::string line = first;
    std::stringstream ss(text);
    std::string word;
    bool empty = true;
    while (ss >> word) {
        if (!empty && line.size() + 1 + word.size() > width) {
            std::puts(line.c_str());
            line = std::string(indent, ' ');
            empty = true;
        }
        line += (empty ? "" : " ") + word;
        empty = false;
    }
    std::puts(line.c_str());
}

void cmdStatuses(const Content& c, const std::string& filter) {
    int shown = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const StatusKind want = pass == 0 ? StatusKind::Buff : StatusKind::Debuff;
        if (filter == "buff" && want != StatusKind::Buff) continue;
        if (filter == "debuff" && want != StatusKind::Debuff) continue;
        bool header = false;
        for (const std::string& id : c.statusOrder) {
            const StatusDef& s = c.status(id);
            if (s.kind != want) continue;
            if (!filter.empty() && filter != "buff" && filter != "debuff" && s.id.find(filter) == std::string::npos &&
                s.name.find(filter) == std::string::npos)
                continue;
            if (!header) {
                std::printf("\n%s\n", want == StatusKind::Buff ? "BUFFS" : "DEBUFFS");
                header = true;
            }
            char name[64];
            std::snprintf(name, sizeof name, "  %-28s", s.name.c_str());
            std::string desc = s.description;
            if (!s.dispellable) desc += " [cannot be dispelled]";
            printWrapped(std::string(name), desc, 30, 118);
            ++shown;
        }
    }
    std::printf("\n%d status effect%s shown (ids are what you write in the data files).\n", shown, shown == 1 ? "" : "s");
}

std::vector<double> parseNumbers(const std::string& s) {
    std::vector<double> out;
    std::string norm = s;
    std::replace(norm.begin(), norm.end(), ',', ' ');
    std::stringstream ss(norm);
    std::string tok;
    while (ss >> tok) {
        char* end = nullptr;
        const double v = std::strtod(tok.c_str(), &end);
        if (end != tok.c_str() && *end == '\0') out.push_back(v);
    }
    return out;
}

int printCalibration(const Rules& rules, const CalibrationInput& in, const std::vector<double>& obs) {
    if (in.offense <= 0 || obs.empty()) {
        std::fprintf(stderr, "calibrate needs the attacker's damage stat (--stat) and at least one observed damage number (--damage).\n");
        return 1;
    }
    const CalibrationResult r = calibrate(rules, in, obs);
    std::printf("\nCalibration\n");
    std::printf("  attacker damage stat : %s\n", ui::withCommas(in.offense).c_str());
    std::printf("  target defense       : %.0f flat = %.1f%% damage reduction at level %d", in.defenseRating,
                100.0 * rules.mitigation(in.defenseRating, in.level), in.level);
    if (in.armorPen > 0 || in.defensePen > 0)
        std::printf("  (%.1f%% after penetration)", 100.0 * r.mitigation);
    std::printf("\n  critical multiplier  : x%.3f   other modifiers: x%.3f   random spread: +/-%.1f%%\n", in.critMultiplier,
                in.otherMultiplier, 100.0 * in.variance);
    for (size_t i = 0; i < obs.size(); ++i) {
        std::printf("  hit %zu: %s damage  ->  multiplier %.3f  (anywhere from %.3f to %.3f given the spread)\n", i + 1,
                    ui::withCommas(obs[i]).c_str(), r.perHit[i], r.perHit[i] / (1.0 + in.variance),
                    in.variance < 1.0 ? r.perHit[i] / (1.0 - in.variance) : 0.0);
    }
    if (r.consistent) {
        std::printf("\n  %s consistent: the ability multiplier is between %.3f and %.3f\n",
                    obs.size() > 1 ? "All observations are" : "The observation is", r.low, r.high);
        std::printf("  best estimate: %.3f  (a %.1f%% ability)\n", r.estimate, r.estimate * 100.0);
        std::printf("  in a data file:  \"multiplier\": %.3f\n", r.estimate);
        if (obs.size() == 1) std::printf("  tip: add more observations of the same ability to narrow the range.\n");
    } else {
        std::printf("\n  No single multiplier explains every observation. Usually one of them was a critical hit, or a buff/\n"
                    "  debuff changed the stats between hits. Using the average: %.3f. Try again with only non-crit hits at\n"
                    "  unchanged stats.\n", r.estimate);
    }
    return 0;
}

int cmdCalibrate(const Rules& rules, const Options& o) {
    CalibrationInput in;
    in.offense = o.calStat;
    in.level = o.calLevel;
    in.defenseRating = o.calDefensePct >= 0 ? flatFromPercent(o.calDefensePct, o.calLevel, rules.levelConstant) : o.calDefense;
    in.armorPen = o.calArmorPen;
    in.defensePen = o.calDefensePen;
    in.critMultiplier = o.calCrit;
    in.otherMultiplier = o.calOther;
    in.variance = o.calVariance;
    return printCalibration(rules, in, parseNumbers(o.calDamages));
}

struct Agg {
    double dealt = 0, taken = 0, healed = 0, kills = 0, crits = 0, turns = 0, alive = 0;
};

int cmdSim(const Content& c, const Rules& rules, const Options& o) {
    std::vector<std::string> a, b;
    std::string la, lb;
    if (!resolveTeam(c, o.teamA, a, la) || !resolveTeam(c, o.teamB, b, lb)) return 1;
    const std::uint64_t seed0 = pickSeed(o);
    std::printf("Simulating %d battles: [%s] vs [%s]  (base seed %llu)\n", o.runs, la.c_str(), lb.c_str(),
                static_cast<unsigned long long>(seed0));

    std::vector<std::vector<Agg>> agg(2);
    agg[0].resize(a.size());
    agg[1].resize(b.size());
    int wins[3] = {0, 0, 0};
    double totalTurns = 0, survA = 0, survB = 0;
    AiController ai;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < o.runs; ++i) {
        Battle battle(c, rules, seed0 + static_cast<std::uint64_t>(i));
        battle.addTeam(0, a);
        battle.addTeam(1, b);
        battle.start();
        battle.run(ai, ai);
        const int w = battle.winner();
        ++wins[w == 0 ? 0 : w == 1 ? 1 : 2];
        totalTurns += battle.turnsTaken;
        survA += battle.aliveCount(0);
        survB += battle.aliveCount(1);
        for (const BattleUnit& u : battle.units) {
            Agg& g = agg[static_cast<size_t>(u.side)][static_cast<size_t>(u.slot)];
            g.dealt += static_cast<double>(u.st.damageDealt);
            g.taken += static_cast<double>(u.st.damageTaken);
            g.healed += static_cast<double>(u.st.healingDone);
            g.kills += u.st.kills;
            g.crits += u.st.crits;
            g.turns += u.st.turns;
            g.alive += u.alive ? 1 : 0;
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double n = static_cast<double>(o.runs);
    std::printf("\nResults over %d battles (%.2fs, %.0f battles/s)\n", o.runs, secs, n / std::max(secs, 1e-9));
    std::printf("  Team A wins : %5d  (%.1f%%)\n", wins[0], 100.0 * wins[0] / n);
    std::printf("  Team B wins : %5d  (%.1f%%)\n", wins[1], 100.0 * wins[1] / n);
    std::printf("  Draws       : %5d  (%.1f%%)\n", wins[2], 100.0 * wins[2] / n);
    std::printf("  Avg turns   : %.1f   Avg survivors A/B: %.2f / %.2f\n", totalTurns / n, survA / n, survB / n);

    for (int side = 0; side < 2; ++side) {
        const std::vector<std::string>& ids = side == 0 ? a : b;
        std::printf("\n%s per-unit averages\n", side == 0 ? "Team A" : "Team B");
        std::printf("  %-22s %9s %9s %9s %6s %6s %6s %7s\n", "unit", "dmg dealt", "dmg taken", "healing", "kills", "crits",
                    "turns", "survive");
        for (size_t s = 0; s < ids.size(); ++s) {
            const Agg& g = agg[static_cast<size_t>(side)][s];
            std::printf("  %-22s %9s %9s %9s %6.2f %6.1f %6.1f %6.0f%%\n", c.unit(ids[s]).name.c_str(),
                        ui::withCommas(g.dealt / n).c_str(), ui::withCommas(g.taken / n).c_str(),
                        ui::withCommas(g.healed / n).c_str(), g.kills / n, g.crits / n, g.turns / n, 100.0 * g.alive / n);
        }
    }
    return 0;
}

// ------------------------------------------------------------ menu -----------

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string ask(const std::string& prompt) {
    std::printf("%s", prompt.c_str());
    std::fflush(stdout);
    std::string s;
    if (!std::getline(std::cin, s)) throw InputClosed{};
    return trim(s);
}

// Returns a number in [lo, hi]; an empty answer returns `def` when def >= 0.
int askNumber(const std::string& prompt, int lo, int hi, int def = -1) {
    for (;;) {
        const std::string s = ask(prompt);
        if (s.empty() && def >= 0) return def;
        char* end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);
        if (end != s.c_str() && *end == '\0' && v >= lo && v <= hi) return static_cast<int>(v);
        std::printf("  Please enter a number from %d to %d.\n", lo, hi);
    }
}

void waitForEnter() { ask("\nPress Enter to return to the menu..."); }

void printUnitTable(const Content& c) {
    for (size_t i = 0; i < c.unitOrder.size(); ++i) {
        const UnitDef& u = c.units.at(c.unitOrder[i]);
        std::printf("  %2zu) %-22s ", i + 1, u.name.c_str());
        for (size_t t = 0; t < u.tags.size(); ++t) std::printf("%s%s", t ? " " : "", u.tags[t].c_str());
        std::printf("\n");
    }
}

// Lets the player pick up to 5 units by number. Returns "id1,id2,..." (first = leader) or "" to go back.
std::string buildCustomTeam(const Content& c) {
    for (;;) {
        std::printf("\nBuild a custom team (the first unit you pick is the leader)\n");
        printUnitTable(c);
        const std::string line = ask("Enter 1-5 unit numbers separated by spaces or commas (blank = go back): ");
        if (line.empty()) return std::string();

        std::vector<int> picks;
        std::string token;
        bool ok = true;
        std::string norm = line;
        std::replace(norm.begin(), norm.end(), ',', ' ');
        std::stringstream ss(norm);
        while (ss >> token) {
            char* end = nullptr;
            const long v = std::strtol(token.c_str(), &end, 10);
            if (*end != '\0' || v < 1 || v > static_cast<long>(c.unitOrder.size())) {
                std::printf("  '%s' is not a unit number between 1 and %zu.\n", token.c_str(), c.unitOrder.size());
                ok = false;
                break;
            }
            if (std::find(picks.begin(), picks.end(), static_cast<int>(v)) != picks.end()) {
                std::printf("  Unit %ld was listed twice - each unit can only be on a team once.\n", v);
                ok = false;
                break;
            }
            picks.push_back(static_cast<int>(v));
        }
        if (ok && (picks.empty() || picks.size() > 5)) {
            std::printf("  Pick between 1 and 5 units.\n");
            ok = false;
        }
        if (!ok) continue;

        std::string spec;
        for (size_t i = 0; i < picks.size(); ++i) spec += (i ? "," : "") + c.unitOrder[static_cast<size_t>(picks[i] - 1)];
        return spec;
    }
}

// Shows the team list; returns a team id or a custom "id,id,..." spec.
std::string pickTeam(const Content& c, const char* title, const std::string& defaultTeamId) {
    for (;;) {
        std::printf("\n%s\n", title);
        int defIdx = -1;
        for (size_t i = 0; i < c.teamOrder.size(); ++i) {
            const TeamDef& t = c.teams.at(c.teamOrder[i]);
            if (t.id == defaultTeamId) defIdx = static_cast<int>(i) + 1;
            std::printf("  %zu) %-22s ", i + 1, t.name.c_str());
            for (size_t k = 0; k < t.units.size(); ++k) std::printf("%s%s", k ? ", " : "", c.unit(t.units[k]).name.c_str());
            std::printf("\n");
        }
        const int custom = static_cast<int>(c.teamOrder.size()) + 1;
        std::printf("  %d) Build a custom team\n", custom);
        std::string prompt = "Choose a team";
        if (defIdx > 0) prompt += " [Enter = " + std::to_string(defIdx) + "]";
        prompt += ": ";
        const int n = askNumber(prompt, 1, custom, defIdx);
        if (n < custom) return c.teamOrder[static_cast<size_t>(n - 1)];
        const std::string spec = buildCustomTeam(c);
        if (!spec.empty()) return spec;
    }
}

void askSeed(Options& o) {
    for (;;) {
        const std::string s = ask("\nSeed - same seed = same battle (blank = random): ");
        if (s.empty()) {
            o.seedGiven = false;
            return;
        }
        char* end = nullptr;
        const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
        if (end != s.c_str() && *end == '\0') {
            o.seed = v;
            o.seedGiven = true;
            return;
        }
        std::printf("  Please enter a whole number, or leave it blank.\n");
    }
}

// The arena needs room: warn if the window is too small and let the player enlarge it or pick the log.
bool arenaFits() {
    for (;;) {
        int cols = 0, rows = 0;
        ui::consoleSize(cols, rows);
        if ((cols == 0 && rows == 0) || (cols >= ArenaView::kWidth && rows >= ArenaView::kRows)) return true;
        std::printf("\n  Your console window is %d x %d characters; the arena view needs at least %d x %d.\n"
                    "  Maximize the window (or make the text smaller with Ctrl + mouse wheel), then press Enter to re-check.\n",
                    cols, rows, ArenaView::kWidth, ArenaView::kRows);
        const std::string s = ask("  Or type L to use the text log instead: ");
        if (!s.empty() && (s[0] == 'l' || s[0] == 'L')) return false;
    }
}

// Asks how the battle should be shown (and at what speed). Remembers the answers in `base`.
void askDisplay(Options& o, Options& base) {
    const bool ansi = ui::ansiAvailable();
    std::printf("\nHow do you want to see the battle?\n");
    std::printf("  1) Arena view    - clean, animated: health bars, damage numbers, short action feed%s\n",
                ansi ? "" : "   [not available in this terminal]");
    std::printf("  2) Full details  - the complete turn-by-turn text log of every roll and effect\n");
    const int def = (base.viewArena && ansi) ? 1 : 2;
    const int n = askNumber("Choose [Enter = " + std::to_string(def) + "]: ", 1, 2, def);
    o.viewArena = n == 1 && ansi;
    if (n == 1 && !ansi) std::puts("  (Arena view needs colour/cursor support, so the text log will be used.)");

    if (o.viewArena) {
        int defSpeed = 2;
        for (int i = 0; i < 4; ++i)
            if (kSpeeds[i].delayMs == base.delayMs) defSpeed = i + 1;
        std::printf("\nPlayback speed\n");
        for (int i = 0; i < 4; ++i) std::printf("  %d) %s\n", i + 1, kSpeeds[i].name);
        const int s = askNumber("Choose [Enter = " + std::to_string(defSpeed) + "]: ", 1, 4, defSpeed);
        o.delayMs = kSpeeds[s - 1].delayMs;
        if (!arenaFits()) o.viewArena = false;
    }
    base.viewArena = o.viewArena;
    base.delayMs = o.delayMs;
}

void displaySettings(Options& base) {
    for (;;) {
        std::printf(
            "\n  ------------------- DISPLAY SETTINGS --------------------\n"
            "    1) Colours   : %s\n"
            "    2) Graphics  : %s\n"
            "    0) Back\n"
            "  ---------------------------------------------------------\n",
            ui::colorEnabled() ? "ON" : (ui::ansiAvailable() ? "OFF" : "OFF (not supported by this terminal)"),
            ui::unicodeEnabled() ? "block characters (default)" : "plain ASCII (use this if bars look broken)");
        const int n = askNumber("  Select: ", 0, 2);
        if (n == 0) return;
        if (n == 1) {
            ui::setColor(!ui::colorEnabled());
            base.color = ui::colorEnabled();
        } else {
            ui::setUnicode(!ui::unicodeEnabled());
            base.ascii = !ui::unicodeEnabled();
        }
    }
}

void printBanner(const std::string& dataDir) {
    std::puts("");
    std::puts("  ==========================================================");
    std::puts("    GoHClone  -  text-mode squad battler engine");
    std::puts("  ==========================================================");
    std::printf("    data folder: %s\n", dataDir.c_str());
}

double askDouble(const std::string& prompt, double def, bool required) {
    for (;;) {
        const std::string s = ask(prompt);
        if (s.empty() && !required) return def;
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (end != s.c_str() && *end == '\0') return v;
        std::printf("  Please enter a number%s.\n", required ? "" : " (or press Enter for the default)");
    }
}

void menuCalibrate(const Rules& rules) {
    std::puts("\nCALIBRATION HELPER\n"
              "Hit a target in the real game, note the damage number, and enter the stats here. The result is the\n"
              "ability's hidden damage multiplier, ready to paste into a data file. Use plain hits (no crits) with\n"
              "unchanged stats where you can; several hits of the same ability narrow the answer.\n");
    CalibrationInput in;
    in.offense = askDouble("Attacker's damage stat at the time (Physical Damage for a physical ability, Special Damage for a\n"
                           "special one): ", 0, true);
    const double pct = askDouble("Target's Armor % (physical) or Resistance % (special) as shown in the game, e.g. 41.27\n"
                                 "[Enter = 0]: ", 0, false);
    in.level = static_cast<int>(askDouble("Target level [Enter = 85]: ", 85, false));
    in.defenseRating = flatFromPercent(pct, in.level, rules.levelConstant);
    in.critMultiplier = 1.0;
    const double crit = askDouble("If these were critical hits, the attacker's Critical Damage as a % (e.g. 187.5)\n"
                                  "[Enter = normal hits]: ", 0, false);
    if (crit > 0) in.critMultiplier = crit / 100.0;
    in.armorPen = askDouble("Attacker's armor/resistance penetration, flat number [Enter = 0]: ", 0, false);
    const std::vector<double> obs = parseNumbers(ask("Observed damage numbers, separated by commas or spaces: "));
    printCalibration(rules, in, obs);
}

int runMenu(const Content& c, const Rules& rules, const std::string& dir, Options base) {
    printBanner(dir);
    base.viewArena = ui::ansiAvailable(); // arena is the default whenever the terminal can show it
    for (;;) {
        std::printf(
            "\n  ----------------------- MAIN MENU -----------------------\n"
            "    1) Play a battle          (you control Team A)\n"
            "    2) Watch a battle         (AI vs AI)\n"
            "    3) Balance simulation     (run many AI battles, see win rates)\n"
            "    4) Browse teams and units\n"
            "    5) Browse status effects  (every buff and debuff)\n"
            "    6) Inspect a unit         (stats and abilities)\n"
            "    7) Calibration helper     (damage number from the game -> ability multiplier)\n"
            "    8) Run self-test\n"
            "    9) Display settings       (colours, graphics)\n"
            "   10) Help / command-line options\n"
            "    0) Quit\n"
            "  ---------------------------------------------------------\n");
        const int choice = askNumber("  Select an option: ", 0, 10);

        try {
            Options o = base;
            switch (choice) {
            case 0:
                std::puts("Goodbye.");
                return 0;

            case 1:
            case 2: {
                o.teamA = pickTeam(c, choice == 1 ? "TEAM A - your team" : "TEAM A", "concord_strike");
                o.teamB = pickTeam(c, choice == 1 ? "TEAM B - the AI's team" : "TEAM B", "syndicate_raiders");
                askSeed(o);
                askDisplay(o, base);
                std::puts("");
                cmdBattle(c, rules, o, choice == 1);
                waitForEnter();
                break;
            }

            case 3: {
                o.teamA = pickTeam(c, "TEAM A", "concord_strike");
                o.teamB = pickTeam(c, "TEAM B", "syndicate_raiders");
                o.runs = askNumber("\nHow many battles? (1-100000) [Enter = 1000]: ", 1, 100000, 1000);
                askSeed(o);
                std::puts("");
                cmdSim(c, rules, o);
                waitForEnter();
                break;
            }

            case 4:
                std::puts("");
                cmdList(c);
                waitForEnter();
                break;

            case 5: {
                std::puts("\n  1) Everything   2) Buffs only   3) Debuffs only");
                const int n = askNumber("  Show [Enter = 1]: ", 1, 3, 1);
                cmdStatuses(c, n == 2 ? "buff" : n == 3 ? "debuff" : "");
                waitForEnter();
                break;
            }

            case 6: {
                std::puts("\nWhich unit?");
                printUnitTable(c);
                const int n = askNumber("Unit number: ", 1, static_cast<int>(c.unitOrder.size()));
                cmdShow(c, rules, c.unitOrder[static_cast<size_t>(n - 1)]);
                waitForEnter();
                break;
            }

            case 7:
                menuCalibrate(rules);
                waitForEnter();
                break;

            case 8:
                std::puts("");
                runSelfTest(dir);
                waitForEnter();
                break;

            case 9:
                displaySettings(base);
                break;

            case 10:
                std::puts("");
                usage();
                waitForEnter();
                break;
            }
        } catch (const std::exception& e) {
            std::printf("\nSomething went wrong: %s\n", e.what());
            waitForEnter();
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) {
        usage();
        return 2;
    }
    if (o.command == "help" || o.command == "--help" || o.command == "-h") {
        usage();
        return 0;
    }
    ui::setup(o.color);
    ui::setUnicode(!o.ascii);
    const bool menuMode = o.command == "menu";

    // In menu mode (e.g. started by double-click) keep the window open long enough to read errors.
    auto fatal = [&](const std::string& msg) {
        std::fprintf(stderr, "%s\n", msg.c_str());
        if (menuMode) {
            std::fprintf(stderr, "\nPress Enter to exit...");
            std::string dummy;
            std::getline(std::cin, dummy);
        }
        return 2;
    };

    const std::string dir = findDataDir(o, argv[0]);
    if (dir.empty()) return fatal("Could not find the data folder (content.json). Use --data <dir>.");
    if (o.command == "selftest") return runSelfTest(dir);
    if (o.command == "golden") {
        try {
            return runGolden(dir, o.write);
        } catch (const std::exception& e) {
            return fatal(std::string("Error: ") + e.what());
        }
    }

    try {
        Rules rules;
        if (fs::exists(fs::path(dir) / "rules.json")) rules = Rules::fromJson(json::parseFile((fs::path(dir) / "rules.json").string()));
        const Content content = loadContent((fs::path(dir) / "content.json").string());

        if (menuMode) return runMenu(content, rules, dir, o);
        if (o.command == "list") { cmdList(content); return 0; }
        if (o.command == "show") return cmdShow(content, rules, o.unitId);
        if (o.command == "statuses") { cmdStatuses(content, o.unitId); return 0; }
        if (o.command == "calibrate") return cmdCalibrate(rules, o);
        if (o.command == "play") return cmdBattle(content, rules, o, true);
        if (o.command == "auto") return cmdBattle(content, rules, o, false);
        if (o.command == "sim") return cmdSim(content, rules, o);
        std::fprintf(stderr, "Unknown command '%s'\n\n", o.command.c_str());
        usage();
        return 2;
    } catch (const InputClosed&) {
        std::puts("");
        return 0;
    } catch (const std::exception& e) {
        return fatal(std::string("Error: ") + e.what());
    }
}
