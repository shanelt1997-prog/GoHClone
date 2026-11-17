#include "Golden.h"
#include "Ai.h"
#include "Json.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

std::uint64_t fnv1a64(const std::string& bytes) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

namespace {

std::string hex64(std::uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

// One line per battle event: kind|unit|other|a|b|flag|text   (numbers with fixed precision so a
// port in another language formats them identically).
struct TraceRecorder : BattleObserver {
    std::string text;
    long events = 0;
    void onEvent(const VisEvent& e) override {
        char buf[256];
        std::snprintf(buf, sizeof buf, "%d|%d|%d|%.4f|%.4f|%d|", static_cast<int>(e.kind), e.unit, e.other, e.a, e.b,
                      e.flag ? 1 : 0);
        text += buf;
        text += e.text;
        text += '\n';
        ++events;
    }
};

std::string g17(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

// Random-number test vectors: the raw Mersenne Twister outputs and the two conversions built on them.
std::string rngVectors(const Content& c, const Rules& rules) {
    std::string out;
    for (std::uint64_t seed : {1ULL, 42ULL, 12345ULL}) {
        std::mt19937_64 raw(seed);
        out += "rng-raw seed=" + std::to_string(seed);
        for (int i = 0; i < 3; ++i) out += " " + std::to_string(raw());
        out += "\n";
        Battle a(c, rules, seed);
        out += "rng-rand01 seed=" + std::to_string(seed);
        for (int i = 0; i < 5; ++i) out += " " + g17(a.rand01());
        out += "\n";
        Battle b(c, rules, seed);
        out += "rng-randInt6 seed=" + std::to_string(seed);
        for (int i = 0; i < 8; ++i) out += " " + std::to_string(b.randInt(6));
        out += "\n";
        Battle d(c, rules, seed);
        out += "rng-randInt10 seed=" + std::to_string(seed);
        for (int i = 0; i < 8; ++i) out += " " + std::to_string(d.randInt(10));
        out += "\n";
    }
    return out;
}

fs::path goldenDir(const std::string& dataDir) { return fs::path(dataDir) / ".." / "golden"; }

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string l;
    while (std::getline(ss, l)) out.push_back(l);
    return out;
}

} // namespace

std::string dataFingerprint(const std::string& dataDir) {
    // rules, then library/*.json, content.json, user/*.json (the same order the loader merges them)
    auto jsonFiles = [](const fs::path& dir) {
        std::vector<std::string> out;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& e : fs::directory_iterator(dir, ec))
                if (e.is_regular_file() && e.path().extension() == ".json") out.push_back(e.path().string());
        std::sort(out.begin(), out.end());
        return out;
    };
    const fs::path d(dataDir);
    std::string bytes = readFile((d / "rules.json").string());
    for (const std::string& f : jsonFiles(d / "library")) bytes += "\n---\n" + readFile(f);
    bytes += "\n---\n" + readFile((d / "content.json").string());
    for (const std::string& f : jsonFiles(d / "user")) bytes += "\n---\n" + readFile(f);
    return hex64(fnv1a64(bytes));
}

GoldenSet buildGolden(const Content& c, const Rules& rules, const std::string& dataHash) {
    GoldenSet set;
    std::string& s = set.summary;
    s += "# GoHClone golden file v1\n";
    s += "# data-hash " + dataHash + "\n";
    s += "# matrix: every ordered pair of teams x seeds 1..3, both sides controlled by the AI\n";
    s += rngVectors(c, rules);

    AiController ai;
    int index = 0;
    for (const std::string& ta : c.teamOrder)
        for (const std::string& tb : c.teamOrder) {
            if (ta == tb) continue;
            for (std::uint64_t seed = 1; seed <= 3; ++seed) {
                Battle b(c, rules, seed);
                b.addTeam(0, c.teams.at(ta).units);
                b.addTeam(1, c.teams.at(tb).units);
                TraceRecorder rec;
                b.observer = &rec;
                b.start();
                b.run(ai, ai);

                char head[256];
                std::snprintf(head, sizeof head, "battle %s %s seed=%llu winner=%d turns=%d events=%ld trace=%s\n", ta.c_str(),
                              tb.c_str(), static_cast<unsigned long long>(seed), b.winner(), b.turnsTaken, rec.events,
                              hex64(fnv1a64(rec.text)).c_str());
                s += head;
                for (const BattleUnit& u : b.units) {
                    char ul[256];
                    std::snprintf(ul, sizeof ul, "  unit %s alive=%d hp=%.0f prot=%.0f tm=%.4f dealt=%lld taken=%lld healed=%lld\n",
                                  u.name.c_str(), u.alive ? 1 : 0, u.health, u.protection, u.tm, u.st.damageDealt,
                                  u.st.damageTaken, u.st.healingDone);
                    s += ul;
                }
                // keep two complete traces around for debugging a mismatch
                if (index == 0 || index == 17)
                    set.traces["trace_" + ta + "_vs_" + tb + "_seed" + std::to_string(seed) + ".txt"] = rec.text;
                ++index;
            }
        }
    return set;
}

GoldenStatus verifyGolden(const std::string& dataDir, std::string& message) {
    const fs::path dir = goldenDir(dataDir);
    const std::string stored = readFile((dir / "summary.txt").string());
    if (stored.empty()) {
        message = "no golden files found in " + dir.string() + " (create them with: GoHClone golden --write)";
        return GoldenStatus::NoFiles;
    }
    const std::string hash = dataFingerprint(dataDir);
    const std::string marker = "# data-hash ";
    const size_t p = stored.find(marker);
    const std::string storedHash = p == std::string::npos ? "" : stored.substr(p + marker.size(), 16);
    if (storedHash != hash) {
        message = "golden files were made from different data files (stored " + storedHash + ", current " + hash +
                  "); regenerate with: GoHClone golden --write";
        return GoldenStatus::DifferentData;
    }

    Rules rules = Rules::fromJson(json::parseFile((fs::path(dataDir) / "rules.json").string()));
    Content content = loadContent((fs::path(dataDir) / "content.json").string());
    const GoldenSet now = buildGolden(content, rules, hash);

    const std::vector<std::string> a = lines(stored), b = lines(now.summary);
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < a.size() ? a[i] : "<missing>", y = i < b.size() ? b[i] : "<missing>";
        if (x != y) {
            message = "first difference at summary line " + std::to_string(i + 1) + ":\n  golden file : " + x + "\n  engine now  : " + y;
            return GoldenStatus::Mismatch;
        }
    }
    for (const auto& t : now.traces) {
        if (readFile((dir / t.first).string()) != t.second) {
            message = "trace file " + t.first + " differs from what the engine produces now";
            return GoldenStatus::Mismatch;
        }
    }
    message = "all " + std::to_string((a.size() - 3) / 1) + " golden lines match";
    return GoldenStatus::Match;
}

int runGolden(const std::string& dataDir, bool write) {
    if (write) {
        Rules rules = Rules::fromJson(json::parseFile((fs::path(dataDir) / "rules.json").string()));
        Content content = loadContent((fs::path(dataDir) / "content.json").string());
        const GoldenSet set = buildGolden(content, rules, dataFingerprint(dataDir));
        const fs::path dir = goldenDir(dataDir);
        std::error_code ec;
        fs::create_directories(dir, ec);
        bool ok = writeFile(dir / "summary.txt", set.summary);
        for (const auto& t : set.traces) ok = writeFile(dir / t.first, t.second) && ok;
        if (!ok) {
            std::fprintf(stderr, "Could not write the golden files into %s\n", dir.string().c_str());
            return 1;
        }
        std::printf("Wrote golden files to %s\n  summary.txt (%zu bytes) and %zu trace file(s)\n", dir.string().c_str(),
                    set.summary.size(), set.traces.size());
        return 0;
    }
    std::string msg;
    const GoldenStatus st = verifyGolden(dataDir, msg);
    std::printf("%s\n", msg.c_str());
    return st == GoldenStatus::Match ? 0 : st == GoldenStatus::Mismatch ? 1 : 3;
}
