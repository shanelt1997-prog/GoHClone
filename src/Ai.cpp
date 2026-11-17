#include "Ai.h"

#include <algorithm>
#include <cmath>

namespace {

bool compare(double value, const std::string& modifier, double param) {
    if (modifier == "greater_than") return value > param;
    if (modifier == "less_than") return value < param;
    return std::fabs(value - param) < 1e-9; // equal_to
}

std::vector<const BattleUnit*> group(const Battle& b, const BattleUnit& self, const std::string& g) {
    std::vector<const BattleUnit*> out;
    if (g == "self") {
        out.push_back(&self);
        return out;
    }
    for (const BattleUnit& u : b.units) {
        if (g == "ally" && u.side == self.side && u.alive) out.push_back(&u);
        else if (g == "enemy" && u.side != self.side && u.alive) out.push_back(&u);
        else if (g == "dead_ally" && u.side == self.side && !u.alive) out.push_back(&u);
    }
    return out;
}

// Percent values (0-100) are used for health / survivability / protection / speed fill.
bool anyUnit(const std::vector<const BattleUnit*>& g, const DecisionNode& n, double (*metric)(const Battle&, const BattleUnit&),
             const Battle& b) {
    for (const BattleUnit* u : g)
        if (compare(metric(b, *u), n.modifier, n.param)) return true;
    return false;
}

double mHealth(const Battle& b, const BattleUnit& u) { return 100.0 * b.healthFrac(u); }
double mSurv(const Battle& b, const BattleUnit& u) { return 100.0 * b.survivabilityFrac(u); }
double mProt(const Battle& b, const BattleUnit& u) {
    const double mp = b.maxProtection(u);
    return mp > 0 ? 100.0 * u.protection / mp : 0.0;
}
double mSpeedFill(const Battle&, const BattleUnit& u) { return 100.0 * u.tm; }

bool evalNode(Battle& b, const BattleUnit& self, const AbilityDef& ab, const DecisionNode& n) {
    const std::vector<const BattleUnit*> g = group(b, self, n.group);
    bool ok = false;
    if (n.type == "single_health") ok = anyUnit(g, n, mHealth, b);
    else if (n.type == "single_survivability") ok = anyUnit(g, n, mSurv, b);
    else if (n.type == "single_protection") ok = anyUnit(g, n, mProt, b);
    else if (n.type == "single_speed_fill") ok = anyUnit(g, n, mSpeedFill, b);
    else if (n.type == "median_health") {
        if (!g.empty()) {
            std::vector<double> v;
            for (const BattleUnit* u : g) v.push_back(mHealth(b, *u));
            std::sort(v.begin(), v.end());
            const double med = v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
            ok = compare(med, n.modifier, n.param);
        }
    } else if (n.type == "number_of_targets") {
        ok = compare(static_cast<double>(b.validTargets(self, ab).size()), n.modifier, n.param);
    } else if (n.type == "number_of_enemies_alive") {
        ok = compare(static_cast<double>(b.aliveCount(1 - self.side)), n.modifier, n.param);
    } else if (n.type == "outnumbered") {
        ok = b.aliveCount(self.side) < b.aliveCount(1 - self.side);
    } else if (n.type == "persistent_effect") {
        for (const BattleUnit* u : g) {
            int count = 0;
            for (const StatusInst& si : u->statuses) {
                if (n.paramStr == "buff" ? si.def->kind == StatusKind::Buff
                    : n.paramStr == "debuff" ? si.def->kind == StatusKind::Debuff
                    : si.def->id == n.paramStr)
                    ++count;
            }
            if (compare(static_cast<double>(count), n.modifier, n.param)) { ok = true; break; }
        }
    } else if (n.type == "target_by_unit_ids") {
        for (const BattleUnit* u : g)
            if (std::find(n.unitIds.begin(), n.unitIds.end(), u->def->id) != n.unitIds.end()) { ok = true; break; }
    } // reinforce_available / summon_available: not implemented -> false

    if (n.negate) ok = !ok;
    if (ok && n.chance < 1.0 && b.rand01() >= n.chance) ok = false;
    return ok;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

template <typename F>
std::vector<int> pickExtreme(const std::vector<int>& c, bool wantMax, F metric) {
    std::vector<int> out;
    double best = 0;
    for (int id : c) {
        const double v = metric(id);
        if (out.empty() || (wantMax ? v > best + 1e-9 : v < best - 1e-9)) { best = v; out.assign(1, id); }
        else if (std::fabs(v - best) <= 1e-9) out.push_back(id);
    }
    return out;
}

std::vector<int> applyRule(const Battle& b, const std::string& rule, const std::vector<int>& c) {
    auto U = [&](int id) -> const BattleUnit& { return b.units[static_cast<size_t>(id)]; };
    if (rule == "random") return c;
    if (rule == "lowest_health") return pickExtreme(c, false, [&](int id) { return U(id).health; });
    if (rule == "lowest_health_pct") return pickExtreme(c, false, [&](int id) { return b.healthFrac(U(id)); });
    if (rule == "lowest_survivability") return pickExtreme(c, false, [&](int id) { return b.survivabilityFrac(U(id)); });
    if (rule == "highest_damage")
        return pickExtreme(c, true, [&](int id) {
            return std::max(b.stat(U(id), Stat::PhysDamage), b.stat(U(id), Stat::SpecDamage));
        });
    if (rule == "highest_speed") return pickExtreme(c, true, [&](int id) { return b.stat(U(id), Stat::Speed); });
    if (rule == "highest_tm") return pickExtreme(c, true, [&](int id) { return U(id).tm; });
    std::vector<int> out;
    if (rule == "has_buffs") {
        for (int id : c) if (b.statusCount(U(id), StatusKind::Buff) > 0) out.push_back(id);
    } else if (rule == "has_debuffs") {
        for (int id : c) if (b.statusCount(U(id), StatusKind::Debuff) > 0) out.push_back(id);
    } else if (startsWith(rule, "tag:")) {
        for (int id : c) if (U(id).def->hasTag(rule.substr(4))) out.push_back(id);
    } else if (startsWith(rule, "status:")) {
        for (int id : c) if (b.hasStatus(U(id), rule.substr(7))) out.push_back(id);
    } else if (startsWith(rule, "no_status:")) {
        for (int id : c) if (!b.hasStatus(U(id), rule.substr(10))) out.push_back(id);
    }
    return out; // unknown rule or nothing matched: caller keeps the previous candidate set
}

} // namespace

bool isKnownTargetRule(const std::string& r) {
    static const char* fixed[] = {"random", "lowest_health", "lowest_health_pct", "lowest_survivability",
                                  "highest_damage", "highest_speed", "highest_tm", "has_buffs", "has_debuffs"};
    for (const char* f : fixed)
        if (r == f) return true;
    return (startsWith(r, "tag:") && r.size() > 4) || (startsWith(r, "status:") && r.size() > 7) ||
           (startsWith(r, "no_status:") && r.size() > 10);
}

Decision AiController::decide(Battle& b, BattleUnit& u) {
    // Priority: ultimates, then specials in data order, then the basic attack.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < u.abilities.size(); ++i)
        if (u.abilities[i].def->kind == AbilityKind::Ultimate) order.push_back(i);
    for (std::size_t i = 0; i < u.abilities.size(); ++i)
        if (u.abilities[i].def->kind == AbilityKind::Special) order.push_back(i);
    order.push_back(static_cast<std::size_t>(u.basicIdx));

    std::size_t chosen = static_cast<std::size_t>(u.basicIdx);
    for (std::size_t i : order) {
        if (!b.abilityUsable(u, i)) continue;
        const AbilityDef& ab = *u.abilities[i].def;
        bool pass = true;
        if (ab.kind != AbilityKind::Basic)
            for (const DecisionNode& n : ab.ai.nodes)
                if (!evalNode(b, u, ab, n)) { pass = false; break; }
        if (pass) { chosen = i; break; }
    }

    const AbilityDef& ab = *u.abilities[chosen].def;
    Decision d;
    d.ability = static_cast<int>(chosen);
    std::vector<int> cands = b.validTargets(u, ab);
    if (ab.pick == Pick::Self) { d.target = u.id; return d; }
    if (cands.empty()) { d.target = -1; return d; }

    std::vector<std::string> rules = ab.pick == Pick::Enemy ? ab.ai.enemyRules : ab.ai.allyRules;
    if (rules.empty() && ab.pick != Pick::Enemy) rules.push_back("lowest_health_pct");
    for (const std::string& r : rules) {
        std::vector<int> f = applyRule(b, r, cands);
        if (!f.empty()) cands = f;
    }
    d.target = cands[static_cast<size_t>(b.randInt(static_cast<int>(cands.size())))];
    return d;
}
