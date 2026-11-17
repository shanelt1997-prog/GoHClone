// Built-in tests: `GoHClone selftest`. No external framework needed.
#include "Ai.h"
#include "Arena.h"
#include "Battle.h"
#include "Calibrate.h"
#include "Defs.h"
#include "Golden.h"
#include "Json.h"
#include "Rules.h"
#include "Ui.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>

namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) ++g_pass;                                                      \
        else {                                                                   \
            ++g_fail;                                                            \
            std::printf("    FAIL line %d: %s\n", __LINE__, #cond);               \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        const double va_ = (a), vb_ = (b);                                                         \
        if (std::fabs(va_ - vb_) <= (eps)) ++g_pass;                                               \
        else {                                                                                     \
            ++g_fail;                                                                              \
            std::printf("    FAIL line %d: %s = %.6f, expected %.6f (+/- %g)\n", __LINE__, #a, va_, vb_, (double)(eps)); \
        }                                                                                          \
    } while (0)

void section(const char* name) { std::printf("  %s\n", name); }

// ------------------------------------------------------------ test lab -------

struct Script : Controller {
    std::function<Decision(Battle&, BattleUnit&)> fn;
    Decision decide(Battle& b, BattleUnit& u) override { return fn(b, u); }
};

Decision basicOn(BattleUnit& u, int target = -1) {
    Decision d;
    d.ability = u.basicIdx;
    d.target = target;
    return d;
}

TargetSpec spec(Select s, Side side) {
    TargetSpec t;
    t.select = s;
    t.side = side;
    return t;
}

EffectDef dmgEffect(double mult, DamageType t = DamageType::AttackDamage) {
    EffectDef e;
    e.type = EffectType::Damage;
    e.damageType = t;
    e.apply = t == DamageType::AttackDamage ? ApplyType::Physical
              : t == DamageType::AbilityPower ? ApplyType::Mental
                                              : ApplyType::Unavoidable;
    e.multiplier = mult;
    e.canCrit = t != DamageType::Unmitigable;
    e.target = spec(Select::Selected, Side::Enemy);
    return e;
}

EffectDef statusEffect(const std::string& id, TargetSpec t, bool canResist = false, int duration = -999) {
    EffectDef e;
    e.type = EffectType::ApplyStatus;
    e.statusId = id;
    e.target = t;
    e.canResist = canResist;
    e.duration = duration;
    return e;
}

struct Lab {
    Content c;
    Rules r;
    Lab() { r.variance = 0.0; }

    StatusDef& status(const std::string& id, StatusKind k, std::uint32_t flags = 0) {
        StatusDef s;
        s.id = id;
        s.name = id;
        s.kind = k;
        s.flags = flags;
        s.duration = 2;
        c.statuses[id] = s;
        return c.statuses[id];
    }

    UnitDef& unit(const std::string& id, double speed = 100.0) {
        UnitDef u;
        u.id = id;
        u.name = id;
        u.stats = defaultStats();
        u.stats[idx(Stat::MaxHealth)] = 100000;
        u.stats[idx(Stat::Speed)] = speed;
        u.stats[idx(Stat::Dodge)] = 0.0;
        u.stats[idx(Stat::Deflection)] = 0.0;
        AbilityDef b;
        b.id = id + "_basic";
        b.name = "Basic";
        b.kind = AbilityKind::Basic;
        b.pick = Pick::Enemy;
        b.effects.push_back(dmgEffect(1.0));
        u.abilities.push_back(b);
        c.units[id] = u;
        c.unitOrder.push_back(id);
        return c.units[id];
    }

    static AbilityDef& add(UnitDef& u, const std::string& id, AbilityKind k, Pick p, int cd, std::vector<EffectDef> fx) {
        AbilityDef a;
        a.id = id;
        a.name = id;
        a.kind = k;
        a.pick = p;
        a.cooldown = cd;
        a.effects = std::move(fx);
        u.abilities.push_back(a);
        return u.abilities.back();
    }

    // permanent self aura (as a leader/unique ability would apply)
    static void aura(UnitDef& u, const std::string& statusId) {
        AbilityDef a;
        a.id = "aura_" + statusId;
        a.name = a.id;
        a.kind = AbilityKind::Unique;
        a.pick = Pick::None;
        AuraDef d;
        d.target = spec(Select::Self, Side::Ally);
        d.statusId = statusId;
        a.auras.push_back(d);
        u.abilities.push_back(a);
    }
};

// Runs one controlled action: `actor` takes a turn using ability `ab` on `target`.
void act(Battle& b, int actor, int ab, int target) {
    Script s;
    s.fn = [=](Battle&, BattleUnit&) {
        Decision d;
        d.ability = ab;
        d.target = target;
        return d;
    };
    b.takeTurn(actor, s);
}

void actBasic(Battle& b, int actor, int target = -1) {
    Script s;
    s.fn = [=](Battle&, BattleUnit& u) { return basicOn(u, target); };
    b.takeTurn(actor, s);
}

// ------------------------------------------------------------ tests ----------

void testJson() {
    section("json parser");
    json::Value v = json::parse(R"({"a":1.5,"b":[1,2,{"c":"x\ny"}], // comment
                                   "d":true,"e":null,"s":"\u00e9"})");
    CHECK(v.num("a", 0) == 1.5);
    CHECK(v.arr("b").size() == 3);
    CHECK(v.arr("b")[2].str("c") == "x\ny");
    CHECK(v.boolean("d", false));
    CHECK(v.str("s") == "\xC3\xA9");
    CHECK(v.num("missing", 7) == 7);
    bool threw = false;
    try { json::parse("{\"a\":}"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { json::parse("[1,2"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

void testFormulas() {
    section("formulas");
    Rules r;
    // Worked example from community sources: 264 armor at level 85 -> ~29.29% reduction.
    CHECK_NEAR(r.mitigation(264, 85), 0.29293, 1e-4);
    CHECK_NEAR(r.mitigation(637.5, 85), 0.5, 1e-9);
    CHECK_NEAR(r.mitigation(0, 85), 0.0, 1e-12);
    CHECK_NEAR(r.mitigation(-50, 85), 0.0, 1e-12);

    // Potency / tenacity: resist = max(tenacity - potency, 15%), capped at 100%.
    CHECK_NEAR(r.resistChance(0.40, 0.80), 0.15, 1e-12);
    CHECK_NEAR(r.resistChance(0.05, 1.10), 0.15, 1e-12);
    CHECK_NEAR(r.resistChance(0.55, 0.35), 0.20, 1e-12);
    CHECK_NEAR(r.resistChance(1.10, 0.05), 1.00, 1e-12);
    CHECK_NEAR(r.resistChance(9999, 0.9), 1.00, 1e-12);   // Tenacity Up
    CHECK_NEAR(r.resistChance(-9999, 0.0), 0.15, 1e-12);  // Tenacity Down still has the floor

    CHECK_NEAR(Rules::critChance(0.20, 0.24), 0.0, 1e-12);
    CHECK_NEAR(Rules::critChance(0.50, 0.10), 0.40, 1e-12);
    CHECK_NEAR(Rules::critChance(1.50, 0.0), 1.0, 1e-12);
    CHECK_NEAR(Rules::evadeChance(0.10, 0.15), 0.0, 1e-12);
    CHECK_NEAR(Rules::evadeChance(0.30, 0.10), 0.20, 1e-12);

    CHECK_NEAR(Rules::effectiveDefense(300, 0.5, 50), 100.0, 1e-9);
    CHECK_NEAR(Rules::effectiveDefense(300, 0.0, 400), 0.0, 1e-9);

    // Published example: 2481 phys damage x 1.886 modifier x 216% crit damage, ~20% armor reduction -> ~8,145.
    const double est = 2481 * 1.886 * 2.16 * (1 - 0.20);
    CHECK(std::fabs(est - 8145) / 8145 < 0.05);
}

const char* kMinimal = R"({"statuses":[{"id":"s1","name":"S1","kind":"buff"}],
 "units":[{"id":"u1","stats":{"health":1000,"speed":100},
   "abilities":[{"id":"b","kind":"basic","effects":[{"type":"damage"}]}]}],
 "teams":[{"id":"t","units":["u1"]}]})";

bool loadFromText(const std::string& text) {
    const fs::path p = fs::temp_directory_path() / "gohclone_selftest.json";
    {
        std::ofstream f(p);
        f << text;
    }
    try {
        loadContent(p.string());
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

std::string replaced(std::string s, const std::string& from, const std::string& to) {
    const size_t p = s.find(from);
    if (p != std::string::npos) s.replace(p, from.size(), to);
    return s;
}

void testLoaderValidation() {
    section("loader validation");
    CHECK(loadFromText(kMinimal));
    CHECK(!loadFromText(replaced(kMinimal, "{\"type\":\"damage\"}", "{\"type\":\"apply_status\",\"status\":\"nope\"}")));
    CHECK(!loadFromText(replaced(kMinimal, "\"kind\":\"basic\"", "\"kind\":\"special\"")));
    CHECK(!loadFromText(replaced(kMinimal, "\"speed\":100", "\"speed\":0")));
    CHECK(!loadFromText(replaced(kMinimal, "\"type\":\"damage\"", "\"type\":\"explode\"")));
    CHECK(!loadFromText(replaced(kMinimal, "\"units\":[\"u1\"]", "\"units\":[\"ghost\"]")));
    CHECK(!loadFromText(replaced(kMinimal, "\"health\":1000", "\"hp\":1000")));
    CHECK(!loadFromText(replaced(kMinimal, "{\"type\":\"damage\"}", "{\"type\":\"damage\",\"chance\":2}")));
    CHECK(!loadFromText(replaced(kMinimal, "{\"type\":\"damage\"}", "{\"type\":\"damage\",\"basis\":\"bogus\"}")));
    CHECK(!loadFromText("{ not json"));
}

void testDamageMath() {
    section("damage pipeline");
    auto fight = [](std::function<void(Lab&)> setup, int ability, double expectDamage, const char* what) {
        Lab lab;
        UnitDef& a = lab.unit("atk");
        UnitDef& d = lab.unit("def");
        a.stats[idx(Stat::PhysDamage)] = 10000;
        a.stats[idx(Stat::SpecDamage)] = 8000;
        d.stats[idx(Stat::Armor)] = 637.5;      // 50% mitigation at level 85
        d.stats[idx(Stat::Resistance)] = 637.5;
        Lab::add(a, "spec", AbilityKind::Special, Pick::Enemy, 0, {dmgEffect(2.0, DamageType::AbilityPower)});
        EffectDef um = dmgEffect(0.1, DamageType::Unmitigable);
        um.basis = "target_max_health";
        Lab::add(a, "pct", AbilityKind::Special, Pick::Enemy, 0, {um});
        setup(lab);
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"atk"});
        b.addTeam(1, {"def"});
        b.start();
        act(b, 0, ability, 1);
        const double lost = 100000 - b.units[1].health;
        if (std::fabs(lost - expectDamage) > 1e-6) std::printf("    (%s: lost %.1f, expected %.1f)\n", what, lost, expectDamage);
        CHECK_NEAR(lost, expectDamage, 1e-6);
    };
    fight([](Lab&) {}, 0, 5000, "10000 x1.0 at 50% armor");
    fight([](Lab&) {}, 1, 8000, "8000 spec x2.0 at 50% resistance");
    fight([](Lab&) {}, 2, 10000, "unmitigable 10% max health ignores armor");
    fight([](Lab& l) { l.c.units["atk"].stats[idx(Stat::PhysCrit)] = 1.0; l.c.units["atk"].stats[idx(Stat::CritDamage)] = 2.0; }, 0, 10000, "guaranteed 2x crit");
    fight([](Lab& l) { auto& a = l.c.units["atk"]; a.stats[idx(Stat::PhysCrit)] = 1.0; a.stats[idx(Stat::CritDamage)] = 2.0;
                       l.c.units["def"].stats[idx(Stat::PhysCritAvoid)] = 1.0; }, 0, 5000, "crit avoidance cancels crit");
    fight([](Lab& l) { l.c.units["atk"].stats[idx(Stat::DefensePen)] = 0.5; }, 0, std::round(10000 * (1 - 318.75 / (318.75 + 637.5))), "50% defense penetration (rounded to whole damage)");
    fight([](Lab& l) { l.c.units["atk"].stats[idx(Stat::ArmorPen)] = 637.5; }, 0, 10000, "flat armor pen removes all armor");
    fight([](Lab& l) { l.c.units["atk"].stats[idx(Stat::DamageDealtMult)] = 1.2; }, 0, 6000, "damage dealt multiplier");
    fight([](Lab& l) { l.c.units["def"].stats[idx(Stat::DamageTakenMult)] = 1.5; }, 0, 7500, "damage taken multiplier");
    fight([](Lab& l) { l.c.units["def"].stats[idx(Stat::Dodge)] = 1.0; }, 0, 0, "100% dodge");
    fight([](Lab& l) { l.c.units["def"].stats[idx(Stat::Dodge)] = 1.0; l.c.units["atk"].stats[idx(Stat::Accuracy)] = 1.0; }, 0, 5000, "accuracy cancels dodge");
    fight([](Lab& l) { l.c.units["def"].stats[idx(Stat::Deflection)] = 1.0; }, 0, 5000, "deflection does not stop physical attacks");
    fight([](Lab& l) { l.c.units["def"].stats[idx(Stat::Deflection)] = 1.0; }, 1, 0, "100% deflection stops special attacks");

    // protection absorbs first and overflow carries into health
    {
        Lab lab;
        lab.unit("atk").stats[idx(Stat::PhysDamage)] = 10000;
        UnitDef& d = lab.unit("def");
        d.stats[idx(Stat::MaxProtection)] = 3000;
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"atk"});
        b.addTeam(1, {"def"});
        b.start();
        actBasic(b, 0, 1);
        CHECK_NEAR(b.units[1].protection, 0, 1e-9);
        CHECK_NEAR(b.units[1].health, 93000, 1e-9); // 10,000 dmg: 3,000 absorbed, 7,000 overflows into health
        CHECK(b.units[0].st.damageDealt == 10000);
    }
    // health steal heals a share of damage dealt
    {
        Lab lab;
        UnitDef& a = lab.unit("atk");
        a.stats[idx(Stat::PhysDamage)] = 10000;
        a.stats[idx(Stat::HealthSteal)] = 0.5;
        lab.unit("def");
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"atk"});
        b.addTeam(1, {"def"});
        b.start();
        b.units[0].health = 50000;
        actBasic(b, 0, 1);
        CHECK_NEAR(b.units[0].health, 55000, 1e-9); // 50% of 10,000 damage
    }
}

void testChances() {
    section("chance formulas (statistical)");
    auto rate = [](std::function<void(Lab&)> setup, std::function<bool(Battle&)> success, int trials) {
        Lab lab;
        lab.unit("atk");
        lab.unit("def");
        lab.status("weak", StatusKind::Debuff);
        UnitDef& a = lab.c.units["atk"];
        Lab::add(a, "hex", AbilityKind::Special, Pick::Enemy, 0, {statusEffect("weak", spec(Select::Selected, Side::Enemy), true)});
        setup(lab);
        int ok = 0;
        for (int i = 0; i < trials; ++i) {
            Battle b(lab.c, lab.r, 1000 + static_cast<std::uint64_t>(i));
            b.addTeam(0, {"atk"});
            b.addTeam(1, {"def"});
            b.start();
            act(b, 0, 1, 1);
            if (success(b)) ++ok;
        }
        return static_cast<double>(ok) / trials;
    };
    auto landed = [](Battle& b) { return b.hasStatus(b.units[1], "weak"); };
    auto set = [](double potency, double tenacity) {
        return [=](Lab& l) {
            l.c.units["atk"].stats[idx(Stat::Potency)] = potency;
            l.c.units["def"].stats[idx(Stat::Tenacity)] = tenacity;
        };
    };
    CHECK_NEAR(rate(set(0.0, 0.0), landed, 4000), 0.85, 0.03);   // floor: 15% always resisted
    CHECK_NEAR(rate(set(1.5, 0.2), landed, 4000), 0.85, 0.03);   // overwhelming potency still floors at 15%
    CHECK_NEAR(rate(set(0.35, 0.55), landed, 4000), 0.80, 0.03); // resist = 55% - 35% = 20%
    CHECK_NEAR(rate(set(0.0, 9999), landed, 500), 0.0, 1e-9);    // Tenacity Up: immune
    CHECK_NEAR(rate(set(0.0, 0.0), landed, 500) , 0.85, 0.06);

    // evasion reduced by accuracy: 50% dodge - 20% accuracy = 30% evade
    auto hitRate = [&](double dodge, double acc) {
        Lab lab;
        lab.unit("atk").stats[idx(Stat::Accuracy)] = acc;
        lab.c.units["atk"].stats[idx(Stat::PhysDamage)] = 1000;
        lab.unit("def").stats[idx(Stat::Dodge)] = dodge;
        int hits = 0;
        const int trials = 4000;
        for (int i = 0; i < trials; ++i) {
            Battle b(lab.c, lab.r, 77 + static_cast<std::uint64_t>(i));
            b.addTeam(0, {"atk"});
            b.addTeam(1, {"def"});
            b.start();
            actBasic(b, 0, 1);
            if (b.units[1].health < 100000) ++hits;
        }
        return static_cast<double>(hits) / trials;
    };
    CHECK_NEAR(hitRate(0.5, 0.2), 0.70, 0.03);
    CHECK_NEAR(hitRate(0.5, 0.0), 0.50, 0.03);

    // crit chance = crit - avoidance
    {
        Lab lab;
        lab.unit("atk").stats[idx(Stat::PhysCrit)] = 0.5;
        lab.c.units["atk"].stats[idx(Stat::CritDamage)] = 2.0;
        lab.unit("def").stats[idx(Stat::PhysCritAvoid)] = 0.2;
        int crits = 0;
        const int trials = 4000;
        for (int i = 0; i < trials; ++i) {
            Battle b(lab.c, lab.r, 5 + static_cast<std::uint64_t>(i));
            b.addTeam(0, {"atk"});
            b.addTeam(1, {"def"});
            b.start();
            actBasic(b, 0, 1);
            if (b.units[0].st.crits) ++crits;
        }
        CHECK_NEAR(static_cast<double>(crits) / trials, 0.30, 0.03);
    }
}

void testTurnMeter() {
    section("turn meter");
    // Speed 250 vs 100: A reaches 100% at t=.004,.008 ; B at .01 ; A again at .012,.016 ; tie at .02
    {
        Lab lab;
        lab.unit("a", 250).stats[idx(Stat::PhysDamage)] = 0;
        lab.unit("b", 100).stats[idx(Stat::PhysDamage)] = 0;
        Battle b(lab.c, lab.r, 3);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"b"});
        b.start();
        std::string order;
        for (int i = 0; i < 5; ++i) {
            const int id = b.nextActor();
            order += id == 0 ? 'A' : 'B';
            actBasic(b, id, id == 0 ? 1 : 0);
        }
        CHECK(order == "AABAA");
    }
    // Exact ties are broken randomly (both outcomes occur).
    {
        Lab lab;
        lab.unit("a", 100);
        lab.unit("b", 100);
        int aFirst = 0;
        for (int i = 0; i < 200; ++i) {
            Battle b(lab.c, lab.r, static_cast<std::uint64_t>(i));
            b.addTeam(0, {"a"});
            b.addTeam(1, {"b"});
            b.start();
            if (b.nextActor() == 0) ++aFirst;
        }
        CHECK(aFirst > 60 && aFirst < 140);
    }
    // Turn meter gain, overflow ordering, daze blocking gains, reduction floor.
    {
        Lab lab;
        UnitDef& a = lab.unit("a", 100);
        lab.unit("b", 100);
        lab.unit("c", 100);
        lab.status("dazed", StatusKind::Debuff, SF_Daze);
        Lab::aura(lab.c.units["c"], "dazed");
        EffectDef gain;
        gain.type = EffectType::ModifyTurnMeter;
        gain.amount = 0.5;
        gain.target = spec(Select::All, Side::Ally);
        Lab::add(a, "boost", AbilityKind::Special, Pick::None, 0, {gain});
        EffectDef cut;
        cut.type = EffectType::ModifyTurnMeter;
        cut.amount = -0.6;
        cut.canResist = false;
        cut.target = spec(Select::All, Side::Enemy);
        Lab::add(a, "slow", AbilityKind::Special, Pick::None, 0, {cut});
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a", "c"});
        b.addTeam(1, {"b"});
        b.start();
        act(b, 0, 1, -1);                    // 'boost': allies +50%
        CHECK_NEAR(b.units[0].tm, 0.5, 1e-9); // 'a' reset to 0 at turn start, then gained 50%
        CHECK_NEAR(b.units[1].tm, 0.0, 1e-9); // 'c' is dazed: cannot gain turn meter
        b.units[2].tm = 0.4;
        act(b, 0, 2, -1);                    // 'slow': enemies -60%, floor at 0
        CHECK_NEAR(b.units[2].tm, 0.0, 1e-9);
        // overflow: highest meter acts first
        b.units[0].tm = 1.3;
        b.units[2].tm = 1.0;
        CHECK(b.nextActor() == 0);
    }
}

void testStatusMechanics() {
    section("statuses, cooldowns, DoT, dispel");
    // stat modifiers: percent-of-base values are additive with each other
    {
        Lab lab;
        lab.unit("a").stats[idx(Stat::PhysDamage)] = 1000;
        lab.unit("d");
        StatusDef& s1 = lab.status("o1", StatusKind::Buff);
        s1.mods.push_back({Stat::PhysDamage, ModOp::PctBase, 0.5});
        s1.mods.push_back({Stat::Tenacity, ModOp::Add, 0.1});
        StatusDef& s2 = lab.status("o2", StatusKind::Buff);
        s2.mods.push_back({Stat::PhysDamage, ModOp::PctBase, 0.25});
        Lab::aura(lab.c.units["a"], "o1");
        Lab::aura(lab.c.units["a"], "o2");
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"d"});
        b.start();
        CHECK_NEAR(b.stat(b.units[0], Stat::PhysDamage), 1750, 1e-9);
        CHECK_NEAR(b.stat(b.units[0], Stat::Tenacity), 0.1, 1e-9);
    }
    // cooldown conventions
    auto cooldownTurns = [](bool countsUseTurn) {
        Lab lab;
        lab.r.cooldownCountsUseTurn = countsUseTurn;
        UnitDef& a = lab.unit("a");
        a.stats[idx(Stat::PhysDamage)] = 0;
        Lab::add(a, "sp", AbilityKind::Special, Pick::Enemy, 2, {dmgEffect(1.0)});
        lab.unit("d");
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"d"});
        b.start();
        std::vector<int> used;
        Script s;
        s.fn = [&](Battle& bb, BattleUnit& u) {
            Decision d;
            d.target = 1;
            if (bb.abilityUsable(u, 1)) { d.ability = 1; used.push_back(bb.turnsTaken); }
            else d.ability = u.basicIdx;
            return d;
        };
        for (int i = 0; i < 7; ++i) b.takeTurn(0, s);
        return used;
    };
    CHECK((cooldownTurns(false) == std::vector<int>{1, 4, 7})); // N more owner turns must pass
    CHECK((cooldownTurns(true) == std::vector<int>{1, 3, 5, 7}));  // use-turn counts toward the cooldown

    // DoT: ticks at the start of the owner's turn, hits protection first unless ignore_protection
    {
        Lab lab;
        UnitDef& a = lab.unit("a");
        a.stats[idx(Stat::PhysDamage)] = 0;
        StatusDef& bleed = lab.status("bleedT", StatusKind::Debuff);
        bleed.mode = AmountMode::Dot;
        bleed.amount = 0.05;
        bleed.duration = 2;
        StatusDef& plague = lab.status("plagueT", StatusKind::Debuff, SF_IgnoreProtection);
        plague.mode = AmountMode::Dot;
        plague.amount = 0.05;
        plague.duration = 2;
        UnitDef& d = lab.unit("d");
        d.stats[idx(Stat::MaxProtection)] = 50000;
        d.stats[idx(Stat::PhysDamage)] = 0;
        Lab::add(a, "bleedIt", AbilityKind::Special, Pick::Enemy, 0, {statusEffect("bleedT", spec(Select::Selected, Side::Enemy))});
        Lab::add(a, "plagueIt", AbilityKind::Special, Pick::Enemy, 0, {statusEffect("plagueT", spec(Select::Selected, Side::Enemy))});
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"d"});
        b.start();
        act(b, 0, 1, 1); // bleed
        actBasic(b, 1, 0);
        CHECK_NEAR(b.units[1].protection, 45000, 1e-9);  // 5% of 100,000 hit protection
        CHECK_NEAR(b.units[1].health, 100000, 1e-9);
        CHECK(b.hasStatus(b.units[1], "bleedT"));
        actBasic(b, 1, 0);
        CHECK_NEAR(b.units[1].protection, 40000, 1e-9);
        CHECK(!b.hasStatus(b.units[1], "bleedT"));        // 2-turn duration expired after two turns
        act(b, 0, 2, 1); // plague
        actBasic(b, 1, 0);
        CHECK_NEAR(b.units[1].protection, 40000, 1e-9);  // ignores protection
        CHECK_NEAR(b.units[1].health, 95000, 1e-9);
    }
    // stun skips exactly one turn
    {
        Lab lab;
        UnitDef& a = lab.unit("a");
        a.stats[idx(Stat::PhysDamage)] = 0;
        lab.status("stunT", StatusKind::Debuff, SF_Stun).duration = 1;
        lab.unit("d");
        Lab::add(a, "stunIt", AbilityKind::Special, Pick::Enemy, 0, {statusEffect("stunT", spec(Select::Selected, Side::Enemy))});
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"d"});
        b.start();
        act(b, 0, 1, 1);
        int acted = 0;
        Script s;
        s.fn = [&](Battle&, BattleUnit& u) { ++acted; return basicOn(u, 0); };
        b.takeTurn(1, s);
        CHECK(acted == 0);
        b.takeTurn(1, s);
        CHECK(acted == 1);
    }
    // dispel: removes dispellable statuses only, never auras
    {
        Lab lab;
        UnitDef& a = lab.unit("a");
        lab.status("b1", StatusKind::Buff);
        lab.status("b2", StatusKind::Buff);
        lab.status("fixed", StatusKind::Buff);
        lab.c.statuses["fixed"].dispellable = false;
        lab.status("auraBuff", StatusKind::Buff);
        UnitDef& d = lab.unit("d");
        Lab::aura(d, "auraBuff");
        EffectDef give1 = statusEffect("b1", spec(Select::Self, Side::Ally));
        EffectDef give2 = statusEffect("b2", spec(Select::Self, Side::Ally));
        EffectDef give3 = statusEffect("fixed", spec(Select::Self, Side::Ally));
        Lab::add(d, "buffUp", AbilityKind::Special, Pick::Self, 0, {give1, give2, give3});
        EffectDef dis;
        dis.type = EffectType::Dispel;
        dis.dispelKind = "buff";
        dis.canResist = false;
        dis.canEvade = false;
        dis.target = spec(Select::Selected, Side::Enemy);
        Lab::add(a, "dispelIt", AbilityKind::Special, Pick::Enemy, 0, {dis});
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"d"});
        b.start();
        act(b, 1, 2, 1); // ability 1 is the aura passive, 2 is buffUp
        CHECK(b.statusCount(b.units[1], StatusKind::Buff) == 4);
        act(b, 0, 1, 1);
        CHECK(b.hasStatus(b.units[1], "fixed"));
        CHECK(b.hasStatus(b.units[1], "auraBuff"));
        CHECK(!b.hasStatus(b.units[1], "b1"));
        CHECK(!b.hasStatus(b.units[1], "b2"));
    }
    // protection-granting buff adds capacity, and removing it trims protection
    {
        Lab lab;
        UnitDef& a = lab.unit("a");
        a.stats[idx(Stat::MaxProtection)] = 10000;
        StatusDef& pu = lab.status("protUp", StatusKind::Buff);
        pu.mode = AmountMode::BonusProtection;
        pu.amount = 0.2;
        pu.duration = 2;
        lab.unit("d");
        Lab::add(a, "shield", AbilityKind::Special, Pick::Self, 0, {statusEffect("protUp", spec(Select::Self, Side::Ally))});
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"d"});
        b.start();
        act(b, 0, 1, 0);
        CHECK_NEAR(b.units[0].protection, 30000, 1e-9); // +20% of 100,000 health
        CHECK_NEAR(b.maxProtection(b.units[0]), 30000, 1e-9);
        actBasic(b, 0, 1);                              // 2nd owner turn: the buff expires
        CHECK(!b.hasStatus(b.units[0], "protUp"));
        CHECK_NEAR(b.units[0].protection, 10000, 1e-9);  // capacity and protection trimmed back
    }
    // convention switch: does a self-applied status tick on the turn it was applied?
    for (int mode = 0; mode < 2; ++mode) {
        const bool tickOnApply = mode == 0;
        Lab lab;
        lab.r.statusesTickOnApplyTurn = tickOnApply;
        UnitDef& a = lab.unit("a");
        a.stats[idx(Stat::PhysDamage)] = 0;
        lab.status("oneTurn", StatusKind::Buff).duration = 1;
        lab.unit("d");
        Lab::add(a, "buffSelf", AbilityKind::Special, Pick::Self, 0, {statusEffect("oneTurn", spec(Select::Self, Side::Ally))});
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"d"});
        b.start();
        act(b, 0, 1, 0);
        CHECK(b.hasStatus(b.units[0], "oneTurn") == !tickOnApply);
        actBasic(b, 0, 1);
        CHECK(!b.hasStatus(b.units[0], "oneTurn"));
    }
}

void testTargeting() {
    section("taunt / stealth targeting");
    auto valid = [](bool e1Taunt, bool e1Stealth, bool e2Taunt, bool e2Stealth, bool e3Stealth) {
        Lab lab;
        lab.unit("atk");
        lab.unit("e1");
        lab.unit("e2");
        lab.unit("e3");
        lab.status("tauntT", StatusKind::Buff, SF_Taunt);
        lab.status("stealthT", StatusKind::Buff, SF_Stealth);
        if (e1Taunt) Lab::aura(lab.c.units["e1"], "tauntT");
        if (e1Stealth) Lab::aura(lab.c.units["e1"], "stealthT");
        if (e2Taunt) Lab::aura(lab.c.units["e2"], "tauntT");
        if (e2Stealth) Lab::aura(lab.c.units["e2"], "stealthT");
        if (e3Stealth) Lab::aura(lab.c.units["e3"], "stealthT");
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"atk"});
        b.addTeam(1, {"e1", "e2", "e3"});
        b.start();
        return b.validTargets(b.units[0], *b.units[0].abilities[0].def);
    };
    CHECK((valid(false, false, true, false, true) == std::vector<int>{2}));        // taunter wins over open units
    CHECK((valid(false, false, false, true, true) == std::vector<int>{1}));        // stealthed units are skipped
    CHECK((valid(false, true, false, true, true) == std::vector<int>{1, 2, 3}));   // all stealthed: anyone targetable
    CHECK((valid(false, false, false, false, false) == std::vector<int>{1, 2, 3}));
    CHECK((valid(false, true, true, false, false) == std::vector<int>{2}));        // stealthed taunter is not targetable; but another taunter is
}

void testReactions() {
    section("counters, assists, revive, bonus turns, triggers");
    // counterattack
    {
        Lab lab;
        lab.unit("atk").stats[idx(Stat::PhysDamage)] = 10000;
        UnitDef& d = lab.unit("def");
        d.stats[idx(Stat::PhysDamage)] = 4000;
        d.stats[idx(Stat::CounterChance)] = 1.0;
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"atk"});
        b.addTeam(1, {"def"});
        b.start();
        actBasic(b, 0, 1);
        CHECK_NEAR(b.units[0].health, 96000, 1e-9);  // took the counter
        CHECK_NEAR(b.units[1].health, 90000, 1e-9);
    }
    // a counter never triggers another counter (no infinite exchange)
    {
        Lab lab;
        UnitDef& a = lab.unit("atk");
        a.stats[idx(Stat::PhysDamage)] = 1000;
        a.stats[idx(Stat::CounterChance)] = 1.0;
        UnitDef& d = lab.unit("def");
        d.stats[idx(Stat::PhysDamage)] = 1000;
        d.stats[idx(Stat::CounterChance)] = 1.0;
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"atk"});
        b.addTeam(1, {"def"});
        b.start();
        actBasic(b, 0, 1);
        CHECK_NEAR(b.units[1].health, 99000, 1e-9);
        CHECK_NEAR(b.units[0].health, 99000, 1e-9);
    }
    // assist trigger: Leader's basic attack brings an ally in for 50% damage
    {
        Lab lab;
        UnitDef& l = lab.unit("lead");
        l.stats[idx(Stat::PhysDamage)] = 10000;
        lab.unit("helper").stats[idx(Stat::PhysDamage)] = 10000;
        lab.unit("def");
        AbilityDef a;
        a.id = "cover";
        a.name = "cover";
        a.kind = AbilityKind::Unique;
        a.pick = Pick::None;
        TriggerDef t;
        t.on = EventType::Attack;
        t.scope = Scope::Self;
        t.abilityKinds = {AbilityKind::Basic};
        t.requireInTurn = true;
        EffectDef as;
        as.type = EffectType::Assist;
        as.multiplier = 0.5;
        as.target = spec(Select::All, Side::Ally);
        as.target.excludeSelf = true;
        t.effects.push_back(as);
        a.triggers.push_back(t);
        l.abilities.push_back(a);
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"lead", "helper"});
        b.addTeam(1, {"def"});
        b.start();
        actBasic(b, 0, 2);
        CHECK_NEAR(b.units[2].health, 100000 - 10000 - 5000, 1e-9);
    }
    // auto-revive
    {
        Lab lab;
        lab.unit("atk").stats[idx(Stat::PhysDamage)] = 50000;
        UnitDef& d = lab.unit("def");
        d.stats[idx(Stat::MaxHealth)] = 10000;
        StatusDef& sw = lab.status("secondWind", StatusKind::Buff);
        sw.reviveHealth = 0.5;
        sw.duration = -1;
        Lab::aura(d, "secondWind");
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"atk"});
        b.addTeam(1, {"def"});
        b.start();
        actBasic(b, 0, 1);
        CHECK(b.units[1].alive);
        CHECK_NEAR(b.units[1].health, 5000, 1e-9);
        CHECK(!b.hasStatus(b.units[1], "secondWind"));
        actBasic(b, 0, 1);
        CHECK(!b.units[1].alive);
        CHECK(b.over() && b.winner() == 0);
    }
    // bonus turn goes before anyone else
    {
        Lab lab;
        UnitDef& a = lab.unit("a", 250);
        a.stats[idx(Stat::PhysDamage)] = 0;
        lab.unit("b", 100);
        EffectDef bm;
        bm.type = EffectType::BonusMove;
        bm.target = spec(Select::Self, Side::Ally);
        Lab::add(a, "again", AbilityKind::Special, Pick::None, 0, {bm});
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"b"});
        b.start();
        const int first = b.nextActor();
        CHECK(first == 0);
        act(b, 0, 1, -1);
        CHECK(b.nextActor() == 0); // bonus turn
    }
    // trigger scopes: ally crit gives self turn meter; death event reaches dead owner scope=self
    {
        Lab lab;
        UnitDef& a = lab.unit("a");
        a.stats[idx(Stat::PhysCrit)] = 1.0;
        a.stats[idx(Stat::PhysDamage)] = 1000;
        UnitDef& n = lab.unit("n");
        AbilityDef t;
        t.id = "sig";
        t.name = "sig";
        t.kind = AbilityKind::Unique;
        t.pick = Pick::None;
        TriggerDef tr;
        tr.on = EventType::Crit;
        tr.scope = Scope::Ally;
        EffectDef g;
        g.type = EffectType::ModifyTurnMeter;
        g.amount = 0.05;
        g.target = spec(Select::Self, Side::Ally);
        tr.effects.push_back(g);
        t.triggers.push_back(tr);
        n.abilities.push_back(t);
        lab.unit("d");
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a", "n"});
        b.addTeam(1, {"d"});
        b.start();
        actBasic(b, 0, 2);
        CHECK_NEAR(b.units[1].tm, 0.05, 1e-9);
    }
}

bool invariantsHold(const Battle& b) {
    for (const BattleUnit& u : b.units) {
        if (std::isnan(u.health) || std::isnan(u.protection) || std::isnan(u.tm)) return false;
        if (u.health < 0 || u.protection < 0 || u.tm < 0) return false;
        if (u.health > b.maxHealth(u) + 1e-6) return false;
        if (u.protection > b.maxProtection(u) + 1e-6) return false;
        if (u.alive && u.health <= 0) return false;
        if (!u.alive && (u.health != 0 || !u.statuses.empty())) return false;
    }
    return true;
}

void testFullContent(const std::string& dir) {
    section("shipped content: load + full-battle invariants");
    Content c;
    Rules rules;
    try {
        c = loadContent((fs::path(dir) / "content.json").string());
        rules = Rules::fromJson(json::parseFile((fs::path(dir) / "rules.json").string()));
    } catch (const std::exception& e) {
        std::printf("    content failed to load: %s\n", e.what());
        ++g_fail;
        return;
    }
    CHECK(c.units.size() >= 12);
    CHECK(c.teams.size() >= 4);

    // every AI targeting rule referenced by data is understood
    bool rulesOk = true;
    for (const auto& up : c.units)
        for (const AbilityDef& a : up.second.abilities) {
            for (const std::string& r : a.ai.enemyRules) rulesOk = rulesOk && isKnownTargetRule(r);
            for (const std::string& r : a.ai.allyRules) rulesOk = rulesOk && isKnownTargetRule(r);
        }
    CHECK(rulesOk);

    // all team pairs, many seeds
    AiController ai;
    int battles = 0, finished = 0, badInvariant = 0, draws = 0, timeouts = 0;
    std::map<std::string, int> feature;
    const char* keys[] = {"counterattacks", "assists!", "CRIT", "evades", "resists", "survives with", "Cataclysm",
                          "bonus turn", "is defeated", "stunned", "gains Taunt", "Bleed", "recovers", "turn meter",
                          "Ability Block", "Stealth"};
    const auto t0 = std::chrono::steady_clock::now();
    for (const std::string& ta : c.teamOrder)
        for (const std::string& tb : c.teamOrder) {
            if (ta == tb) continue;
            for (int seed = 0; seed < 40; ++seed) {
                Battle b(c, rules, 9000 + static_cast<std::uint64_t>(seed));
                b.logging = seed < 8; // keep logs for a subset to verify feature coverage
                b.addTeam(0, c.teams.at(ta).units);
                b.addTeam(1, c.teams.at(tb).units);
                b.start();
                bool ok = true;
                while (b.step(ai, ai)) ok = ok && invariantsHold(b);
                ok = ok && invariantsHold(b);
                ++battles;
                if (!ok) ++badInvariant;
                if (b.winner() >= 0 && b.winner() <= 1) ++finished;
                if (b.winner() == 2) ++draws;
                if (b.timedOut) ++timeouts;
                for (const std::string& line : b.log)
                    for (const char* k : keys)
                        if (line.find(k) != std::string::npos) feature[k]++;
            }
        }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    %d battles in %.2fs: %d decisive, %d draws (%d timeouts), %d invariant violations\n", battles, secs, finished,
                draws, timeouts, badInvariant);
    CHECK(badInvariant == 0);
    CHECK(finished + draws == battles);        // every battle terminates cleanly
    CHECK(draws == timeouts);                  // draws only ever come from the turn limit (healer stalemates)
    CHECK(draws * 100 < battles * 3);          // and they are rare (<3%) with the shipped content
    for (const char* k : keys) {
        if (feature[k] == 0) std::printf("    (mechanic never observed in sample logs: %s)\n", k);
        CHECK(feature[k] > 0);
    }

    // determinism: same seed => identical log; different seed => (almost surely) different
    auto logFor = [&](std::uint64_t seed) {
        Battle b(c, rules, seed);
        b.logging = true;
        b.addTeam(0, c.teams.at("concord_strike").units);
        b.addTeam(1, c.teams.at("syndicate_raiders").units);
        b.start();
        b.run(ai, ai);
        return b.log;
    };
    CHECK(logFor(123) == logFor(123));
    CHECK(logFor(123) != logFor(124));
}


// Forwards events to a silent ArenaView while counting them and checking every frame it would draw.
struct ArenaProbe : BattleObserver {
    ArenaView* arena = nullptr;
    const Battle* battle = nullptr;
    bool checkAscii = false;
    long turnStarts = 0, turnEnds = 0, defeats = 0, frames = 0, badFrames = 0;
    double damageSeen = 0, healSeen = 0;

    void checkFrames() {
        ++frames;
        std::vector<std::string> all = arena->buildFrame();
        const size_t frameLines = all.size();
        std::vector<std::string> bottom = arena->buildBottom();
        size_t perSide[2] = {0, 0};
        for (const BattleUnit& u : battle->units) perSide[u.side]++;
        const size_t rows = std::max(perSide[0], perSide[1]);
        if (frameLines != 3 * rows + 3) ++badFrames;        // title + headers + 3 lines/unit + separator
        if (bottom.size() != 8) ++badFrames;                // 2 banner lines + 6 feed lines
        for (const std::string& l : all)
            if (ui::visibleWidth(l) != static_cast<size_t>(ArenaView::kWidth)) ++badFrames;
        for (const std::string& l : bottom)
            if (ui::visibleWidth(l) > static_cast<size_t>(ArenaView::kWidth)) ++badFrames;
        if (checkAscii) {
            for (const std::string& l : all)
                for (unsigned char ch : l)
                    if (ch >= 128) { ++badFrames; break; }
        }
    }

    void onEvent(const VisEvent& ev) override {
        arena->onEvent(ev);
        switch (ev.kind) {
        case VisKind::TurnStart: ++turnStarts; break;
        case VisKind::TurnEnd: ++turnEnds; checkFrames(); break;
        case VisKind::Defeat: ++defeats; break;
        case VisKind::Damage: damageSeen += ev.a + ev.b; break;
        case VisKind::Heal: healSeen += ev.a + ev.b; break;
        default: break;
        }
    }
};

void testObserverAndArena(const std::string& dir) {
    section("event stream + arena view");

    CHECK(ui::visibleWidth("abc") == 3);
    CHECK(ui::visibleWidth("\x1b[31mabc\x1b[0m") == 3);
    CHECK(ui::visibleWidth("\xE2\x96\x88\xE2\x96\x91") == 2); // two block characters

    Content c = loadContent((fs::path(dir) / "content.json").string());
    Rules rules = Rules::fromJson(json::parseFile((fs::path(dir) / "rules.json").string()));
    AiController ai;

    struct Mode { bool unicode, color; };
    const Mode modes[] = {{true, true}, {true, false}, {false, false}};
    long battles = 0, badEvents = 0, badFrames = 0, framesChecked = 0;
    for (const Mode& m : modes)
        for (const std::string& ta : c.teamOrder)
            for (const std::string& tb : c.teamOrder) {
                if (ta == tb) continue;
                for (int seed = 0; seed < 4; ++seed) {
                    Battle b(c, rules, 500 + static_cast<std::uint64_t>(seed));
                    b.addTeam(0, c.teams.at(ta).units);
                    b.addTeam(1, c.teams.at(tb).units);
                    ArenaOptions ao;
                    ao.silent = true;
                    ao.unicode = m.unicode;
                    ao.color = m.color;
                    ArenaView arena(b, c.teams.at(ta).name, c.teams.at(tb).name, 500, ao);
                    ArenaProbe probe;
                    probe.arena = &arena;
                    probe.battle = &b;
                    probe.checkAscii = !m.unicode && !m.color;
                    b.observer = &probe;
                    arena.begin();
                    b.start();
                    b.run(ai, ai);
                    arena.finish();

                    double dmgTaken = 0, healed = 0;
                    int dead = 0;
                    for (const BattleUnit& u : b.units) {
                        dmgTaken += static_cast<double>(u.st.damageTaken);
                        healed += static_cast<double>(u.st.healingDone);
                        if (!u.alive) ++dead;
                    }
                    ++battles;
                    // The event stream must agree exactly with the engine's own bookkeeping.
                    if (probe.turnStarts != b.turnsTaken || probe.turnEnds != b.turnsTaken) ++badEvents;
                    if (std::fabs(probe.damageSeen - dmgTaken) > 0.5) ++badEvents;
                    if (std::fabs(probe.healSeen - healed) > 0.5) ++badEvents;
                    if (probe.defeats != dead) ++badEvents;
                    badFrames += probe.badFrames;
                    framesChecked += probe.frames;
                }
            }
    std::printf("    %ld battles, %ld frames checked in unicode/colour, unicode/plain and ASCII modes\n", battles, framesChecked);
    CHECK(badEvents == 0);
    CHECK(badFrames == 0);
    CHECK(framesChecked > 10000);

    // team sizes other than 5v5 still lay out correctly
    for (size_t n : {1u, 3u, 6u}) {
        Battle b(c, rules, 1);
        std::vector<std::string> ids(c.unitOrder.begin(), c.unitOrder.begin() + static_cast<std::ptrdiff_t>(n));
        std::vector<std::string> ids2(c.unitOrder.end() - static_cast<std::ptrdiff_t>(n), c.unitOrder.end());
        b.addTeam(0, ids);
        b.addTeam(1, ids2);
        ArenaOptions ao;
        ao.silent = true;
        ArenaView arena(b, "A", "B", 1, ao);
        b.start();
        const std::vector<std::string> f = arena.buildFrame();
        CHECK(f.size() == 3 * n + 3);
        bool widthOk = true;
        for (const std::string& l : f) widthOk = widthOk && ui::visibleWidth(l) == static_cast<size_t>(ArenaView::kWidth);
        CHECK(widthOk);
    }

    // every unit's name appears in the opening frame
    {
        Battle b(c, rules, 1);
        b.addTeam(0, c.teams.at("concord_strike").units);
        b.addTeam(1, c.teams.at("syndicate_raiders").units);
        ArenaOptions ao;
        ao.silent = true;
        ao.color = false;
        ArenaView arena(b, "A", "B", 1, ao);
        b.start();
        std::string all;
        for (const std::string& l : arena.buildFrame()) all += l + "\n";
        bool allNames = true;
        for (const BattleUnit& u : b.units) allNames = allNames && all.find(u.name) != std::string::npos;
        CHECK(allNames);
    }
}


void testRandomNumbers() {
    section("random numbers (hand-specified, language-independent)");
    // The C++ standard fixes the 10000th output of a default-constructed mt19937_64.
    std::mt19937_64 mt;
    std::uint64_t v = 0;
    for (int i = 0; i < 10000; ++i) v = mt();
    CHECK(v == 9981545732273789042ULL);

    Lab lab;
    lab.unit("a");
    lab.unit("b");
    Battle b(lab.c, lab.r, 99);
    b.addTeam(0, {"a"});
    b.addTeam(1, {"b"});
    double sum = 0, lo = 1, hi = 0;
    const int n = 200000;
    for (int i = 0; i < n; ++i) {
        const double x = b.rand01();
        sum += x;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    CHECK(lo >= 0.0 && hi < 1.0);
    CHECK_NEAR(sum / n, 0.5, 0.005);

    // randInt(6): every face about 1/6 of the time, never out of range
    int counts[6] = {0, 0, 0, 0, 0, 0};
    bool inRange = true;
    for (int i = 0; i < 120000; ++i) {
        const int r = b.randInt(6);
        if (r < 0 || r > 5) inRange = false;
        else counts[r]++;
    }
    CHECK(inRange);
    for (int k = 0; k < 6; ++k) CHECK_NEAR(counts[k] / 120000.0, 1.0 / 6.0, 0.006);
    CHECK(b.randInt(1) == 0 && b.randInt(0) == 0);

    // same seed => same sequence; different seed => different
    Battle x1(lab.c, lab.r, 5), x2(lab.c, lab.r, 5), x3(lab.c, lab.r, 6);
    bool same = true, diff = false;
    for (int i = 0; i < 50; ++i) {
        const double p = x1.rand01(), q = x2.rand01(), r = x3.rand01();
        same = same && p == q;
        diff = diff || p != r;
    }
    CHECK(same && diff);
}

void testPresentationData(const std::string& dir) {
    section("presentation metadata");
    Content c = loadContent((fs::path(dir) / "content.json").string());
    const std::set<std::string> ok = {"melee", "ranged", "area", "self", "support", "none"};
    bool allValid = true, passivesNone = true, activesStaged = true, defaultsOk = true;
    for (const auto& up : c.units) {
        const UnitDef& u = up.second;
        defaultsOk = defaultsOk && !u.presentation.prefab.empty() && !u.presentation.portrait.empty() &&
                     u.presentation.clips.count("idle") && u.presentation.clips.count("hit") &&
                     u.presentation.clips.count("death") && u.presentation.clips.count("victory");
        for (const AbilityDef& a : u.abilities) {
            allValid = allValid && ok.count(a.presentation.tmpl) && !a.presentation.anim.empty();
            const bool passive = a.kind == AbilityKind::Leader || a.kind == AbilityKind::Unique;
            if (passive && a.presentation.tmpl != "none") passivesNone = false;
            if (!passive && a.presentation.tmpl == "none") activesStaged = false;
        }
    }
    CHECK(allValid && passivesNone && activesStaged && defaultsOk);

    auto tmpl = [&](const char* unit, const char* ability) {
        for (const AbilityDef& a : c.unit(unit).abilities)
            if (a.id == ability) return a.presentation.tmpl;
        return std::string("?");
    };
    CHECK(tmpl("kade_orlan", "throwing_knife") == "melee");   // single-target physical hit
    CHECK(tmpl("vexa_thorne", "hex_bolt") == "ranged");        // single-target special hit
    CHECK(tmpl("vexa_thorne", "void_nova") == "area");         // hits every enemy
    CHECK(tmpl("brask_orrin", "hold_the_line") == "self");     // only buffs the caster
    CHECK(tmpl("nyx_halloran", "overclock") == "support");     // buffs the whole team
    CHECK(tmpl("morrow", "silence") == "ranged");              // single-target control
    CHECK(tmpl("warden_krell", "retaliation") == "none");      // passive

    // explicit values in the data override the derived ones; bad template names are rejected
    const std::string base = std::string(kMinimal);
    CHECK(loadFromText(replaced(base, "\"effects\":[{\"type\":\"damage\"}]",
                                "\"effects\":[{\"type\":\"damage\"}],\"presentation\":{\"template\":\"ranged\",\"anim\":\"zap\"}")));
    CHECK(!loadFromText(replaced(base, "\"effects\":[{\"type\":\"damage\"}]",
                                 "\"effects\":[{\"type\":\"damage\"}],\"presentation\":{\"template\":\"teleport\"}")));
    CHECK(!c.stages.empty());
}

void testGoldenFiles(const std::string& dir) {
    section("golden fingerprints");
    std::string msg;
    const GoldenStatus st = verifyGolden(dir, msg);
    if (st == GoldenStatus::Match) {
        CHECK(true);
        std::printf("    %s\n", msg.c_str());
    } else if (st == GoldenStatus::Mismatch) {
        std::printf("    %s\n", msg.c_str());
        CHECK(false);
    } else {
        std::printf("    (skipped) %s\n", msg.c_str()); // no files / data edited since: not a failure
    }
    // the fingerprints themselves must be reproducible within one run
    Rules rules = Rules::fromJson(json::parseFile((fs::path(dir) / "rules.json").string()));
    Content content = loadContent((fs::path(dir) / "content.json").string());
    const std::string h = dataFingerprint(dir);
    CHECK(buildGolden(content, rules, h).summary == buildGolden(content, rules, h).summary);
}


// ---- tests that run the real status library through the engine ----

Lab libLab(const std::string& dir) {
    Lab lab;
    lab.c = loadContent((fs::path(dir) / "library" / "statuses.json").string());
    return lab;
}

void testStatusLibrary(const std::string& dir) {
    section("status library (real definitions through the engine)");
    Lab probe = libLab(dir);
    int buffs = 0, debuffs = 0;
    bool described = true;
    for (const std::string& id : probe.c.statusOrder) {
        const StatusDef& s = probe.c.status(id);
        (s.kind == StatusKind::Buff ? buffs : debuffs)++;
        described = described && !s.description.empty();
    }
    std::printf("    library: %d buffs + %d debuffs\n", buffs, debuffs);
    CHECK(buffs >= 55 && debuffs >= 50 && described);

    // helper: a fresh duel "a" (side 0) vs "d" (side 1) with the given setup
    auto duel = [&](std::function<void(Lab&, UnitDef&, UnitDef&)> setup, std::function<void(Battle&, Lab&)> body) {
        Lab lab = libLab(dir);
        UnitDef& a = lab.unit("a");
        UnitDef& d = lab.unit("d");
        a.stats[idx(Stat::PhysDamage)] = 10000;
        setup(lab, a, d);
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a"});
        b.addTeam(1, {"d"});
        b.start();
        body(b, lab);
    };
    auto hp = [](Battle& b, int id) { return b.units[static_cast<size_t>(id)].health; };

    // Foresight: evades the next attack, then is used up
    duel([](Lab&, UnitDef&, UnitDef& d) { Lab::aura(d, "foresight"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 100000, 1e-9);
        CHECK(!b.hasStatus(b.units[1], "foresight"));
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 90000, 1e-9);
    });
    // Blind: the attacker's next attack misses
    duel([](Lab&, UnitDef& a, UnitDef&) { Lab::aura(a, "blind"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 100000, 1e-9);
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 90000, 1e-9);
    });
    // Advantage: next attack crits (then is used up). Vulnerable: always crit against it.
    duel([](Lab&, UnitDef& a, UnitDef&) { a.stats[idx(Stat::CritDamage)] = 2.0; Lab::aura(a, "advantage"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 80000, 1e-9);
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 70000, 1e-9);
    });
    duel([](Lab&, UnitDef& a, UnitDef& d) { a.stats[idx(Stat::CritDamage)] = 2.0; Lab::aura(d, "vulnerable"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 60000, 1e-9);
    });
    // Stun means no evasion even with 100% dodge; Deflective Ward reduces hits to 1; Protection Disruption
    duel([](Lab&, UnitDef&, UnitDef& d) { d.stats[idx(Stat::Dodge)] = 1.0; Lab::aura(d, "stun"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 90000, 1e-9);
    });
    duel([](Lab&, UnitDef&, UnitDef& d) { Lab::aura(d, "deflective_ward"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 99999, 1e-9);
    });
    duel([](Lab&, UnitDef&, UnitDef& d) { d.stats[idx(Stat::MaxProtection)] = 50000; Lab::aura(d, "protection_disruption"); },
         [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 90000, 1e-9);
        CHECK_NEAR(b.units[1].protection, 50000, 1e-9);
    });
    // Burning: 15% per turn and cannot evade
    duel([](Lab&, UnitDef&, UnitDef& d) { d.stats[idx(Stat::Dodge)] = 1.0; Lab::aura(d, "burning"); }, [&](Battle& b, Lab&) {
        actBasic(b, 1, 0);                       // d's turn: burning ticks first
        CHECK_NEAR(hp(b, 1), 85000, 1e-9);
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 75000, 1e-9);       // dodge 100% ignored
    });

    // Endure and Second Wind: lethal damage is survived once
    duel([](Lab&, UnitDef& a, UnitDef& d) { a.stats[idx(Stat::PhysDamage)] = 50000; d.stats[idx(Stat::MaxHealth)] = 10000; Lab::aura(d, "endure"); },
         [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK(b.units[1].alive);
        CHECK_NEAR(hp(b, 1), 200, 1e-9);         // 1% + 1% of 10,000
        CHECK(!b.hasStatus(b.units[1], "endure"));
        actBasic(b, 0, 1);
        CHECK(!b.units[1].alive);
    });
    duel([](Lab&, UnitDef& a, UnitDef& d) {
            a.stats[idx(Stat::PhysDamage)] = 50000;
            d.stats[idx(Stat::MaxHealth)] = 10000;
            d.stats[idx(Stat::MaxProtection)] = 6000;
            Lab::aura(d, "second_wind");
        }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK(b.units[1].alive);
        CHECK_NEAR(hp(b, 1), 5100, 1e-9);                    // 1% + 50% of 10,000
        CHECK_NEAR(b.units[1].protection, 3000, 1e-9);       // +50% of max protection
        CHECK(!b.hasStatus(b.units[1], "second_wind"));
    });
    // Revive vs "can't be revived"
    auto reviveCase = [&](bool blocked) {
        bool alive = false;
        double health = 0;
        duel([&](Lab&, UnitDef& a, UnitDef& d) {
                a.stats[idx(Stat::PhysDamage)] = 500000;
                Lab::aura(d, "backup_plan");
                if (blocked) Lab::aura(d, "order_66");
            }, [&](Battle& b, Lab&) {
            actBasic(b, 0, 1);
            alive = b.units[1].alive;
            health = hp(b, 1);
        });
        return std::make_pair(alive, health);
    };
    CHECK(reviveCase(false).first && std::fabs(reviveCase(false).second - 80000) < 1e-9);
    CHECK(!reviveCase(true).first);

    // Instant defeat: kills, except Endure / Instant Defeat Immunity
    auto defeatCase = [&](const char* immunity) {
        bool alive = true;
        duel([&](Lab&, UnitDef& a, UnitDef& d) {
                EffectDef e;
                e.type = EffectType::Defeat;
                e.target = spec(Select::Selected, Side::Enemy);
                Lab::add(a, "execute", AbilityKind::Special, Pick::Enemy, 0, {e});
                if (immunity[0]) Lab::aura(d, immunity);
            }, [&](Battle& b, Lab&) {
            act(b, 0, 1, 1);
            alive = b.units[1].alive;
        });
        return alive;
    };
    CHECK(!defeatCase(""));
    CHECK(defeatCase("endure"));
    CHECK(defeatCase("instant_defeat_immunity"));

    // Trusted Agent: a basic attack is followed by a bonus attack (and does not recurse forever)
    duel([](Lab&, UnitDef& a, UnitDef&) { Lab::aura(a, "trusted_agent"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 100000 - 2 * 13000, 1e-9);  // two hits of 10,000 x 1.3
    });
    // Bulwark loses a stack when damaged; Retaliate hits back but cannot defeat
    duel([](Lab& lab, UnitDef&, UnitDef& d) {
            d.stats[idx(Stat::Armor)] = 0;
            EffectDef give = statusEffect("bulwark", spec(Select::Self, Side::Ally));
            give.stacks = 3;
            Lab::add(d, "stack", AbilityKind::Special, Pick::Self, 0, {give});
            (void)lab;
        }, [&](Battle& b, Lab&) {
        act(b, 1, 1, 1);
        CHECK(b.units[1].statuses.size() == 1 && b.units[1].statuses[0].stacks == 3);
        actBasic(b, 0, 1);
        CHECK(b.units[1].statuses[0].stacks == 2);
    });
    for (int low = 0; low < 2; ++low)
        duel([&](Lab&, UnitDef&, UnitDef& d) {
                EffectDef give = statusEffect("retaliate", spec(Select::Self, Side::Ally));
                give.stacks = 2;
                Lab::add(d, "arm", AbilityKind::Special, Pick::Self, 0, {give});
            }, [&](Battle& b, Lab&) {
            act(b, 1, 1, 1);
            if (low) b.units[0].health = 3000;
            actBasic(b, 0, 1);                                   // deals 10,000; retaliation = 20% x 2 stacks
            if (low) CHECK_NEAR(hp(b, 0), 1, 1e-9);              // can't defeat: stops at 1 health
            else CHECK_NEAR(hp(b, 0), 100000 - 4000, 1e-9);
        });
    // Deathmark / Expose / Off Balance / Stagger: extra losses when damaged by an attack
    duel([](Lab&, UnitDef&, UnitDef& d) { Lab::aura(d, "deathmark"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 100000 - 10000 - 50000, 1e-9);
    });
    duel([](Lab&, UnitDef&, UnitDef& d) { Lab::aura(d, "expose"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 100000 - 10000 - 20000, 1e-9);
    });
    duel([](Lab&, UnitDef&, UnitDef& d) { Lab::aura(d, "stagger"); }, [&](Battle& b, Lab&) {
        b.units[1].tm = 0.8;
        actBasic(b, 0, 1);
        CHECK_NEAR(b.units[1].tm, 0.0, 1e-9);
    });
    duel([](Lab&, UnitDef&, UnitDef& d) { Lab::aura(d, "off_balance"); }, [&](Battle& b, Lab&) {
        b.units[1].tm = 0.5;
        actBasic(b, 0, 1);
        CHECK_NEAR(hp(b, 1), 100000 - 10000 - 10000, 1e-9);
        CHECK_NEAR(b.units[1].tm, 0.4, 1e-9);
    });
    // Fear: skips the turn, raises cooldowns, and is removed by damage
    duel([](Lab&, UnitDef& a, UnitDef& d) {
            Lab::add(d, "spec", AbilityKind::Special, Pick::Enemy, 0, {dmgEffect(1.0)});
            Lab::add(a, "scare", AbilityKind::Special, Pick::Enemy, 0, {statusEffect("fear", spec(Select::Selected, Side::Enemy))});
        }, [&](Battle& b, Lab&) {
        act(b, 0, 1, 1);
        CHECK(b.hasStatus(b.units[1], "fear"));
        CHECK(b.units[1].abilities[1].cooldown == 1);
        actBasic(b, 0, 1);
        CHECK(!b.hasStatus(b.units[1], "fear"));
    });
    // Breach yields to Speed Down / Defense Down instead of stacking with them
    duel([](Lab&, UnitDef&, UnitDef& d) {
            d.stats[idx(Stat::Speed)] = 200;
            d.stats[idx(Stat::Armor)] = 400;
            Lab::aura(d, "breach");
        }, [&](Battle& b, Lab&) {
        CHECK_NEAR(b.stat(b.units[1], Stat::Speed), 150, 1e-9);
        CHECK_NEAR(b.stat(b.units[1], Stat::Armor), 300, 1e-9);
    });
    duel([](Lab&, UnitDef&, UnitDef& d) {
            d.stats[idx(Stat::Speed)] = 200;
            d.stats[idx(Stat::Armor)] = 400;
            Lab::aura(d, "breach");
            Lab::aura(d, "speed_down");
            Lab::aura(d, "defense_down");
        }, [&](Battle& b, Lab&) {
        CHECK_NEAR(b.stat(b.units[1], Stat::Speed), 150, 1e-9);   // breach ignored: only Speed Down counts
        CHECK_NEAR(b.stat(b.units[1], Stat::Armor), 200, 1e-9);   // breach ignored: only Defense Down counts
    });
    // Fracture sets speed to 0; Merciless blocks turn meter reduction from enemies
    duel([](Lab&, UnitDef&, UnitDef& d) { d.stats[idx(Stat::Speed)] = 250; Lab::aura(d, "fracture"); }, [&](Battle& b, Lab&) {
        CHECK_NEAR(b.stat(b.units[1], Stat::Speed), 0.0, 1e-12);
    });
    duel([](Lab&, UnitDef& a, UnitDef& d) {
            EffectDef cut;
            cut.type = EffectType::ModifyTurnMeter;
            cut.amount = -0.5;
            cut.canResist = false;
            cut.target = spec(Select::Selected, Side::Enemy);
            Lab::add(a, "slow", AbilityKind::Special, Pick::Enemy, 0, {cut});
            Lab::aura(d, "merciless");
        }, [&](Battle& b, Lab&) {
        b.units[1].tm = 0.9;
        act(b, 0, 1, 1);
        CHECK_NEAR(b.units[1].tm, 0.9, 1e-9);
    });
    // Provoked: out-of-turn attacks deal 90% less and cost the attacker 20% max health (never defeating it)
    duel([](Lab&, UnitDef&, UnitDef& d) { d.stats[idx(Stat::PhysDamage)] = 10000; Lab::aura(d, "provoked"); }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);                                       // d counters (provoked grants +100% counter)
        CHECK_NEAR(hp(b, 0), 100000 - 1000, 1e-9);               // counter deals 10,000 x 0.1
        CHECK_NEAR(hp(b, 1), 100000 - 10000 - 20000, 1e-9);      // self-inflicted 20% of max health
    });
    // Frenzy and Alert react to an ally's special ability
    duel([](Lab& lab, UnitDef& a, UnitDef&) {
            UnitDef& f = lab.unit("f");
            Lab::aura(f, "frenzy");
            Lab::add(a, "spec", AbilityKind::Special, Pick::Enemy, 0, {dmgEffect(1.0)});
        }, [&](Battle&, Lab&) {});
    {
        Lab lab = libLab(dir);
        UnitDef& a = lab.unit("a");
        a.stats[idx(Stat::PhysDamage)] = 10000;
        Lab::add(a, "spec", AbilityKind::Special, Pick::Enemy, 0, {dmgEffect(1.0)});
        UnitDef& f = lab.unit("f");
        f.stats[idx(Stat::PhysDamage)] = 10000;
        Lab::aura(f, "frenzy");
        UnitDef& w = lab.unit("w");
        w.stats[idx(Stat::PhysDamage)] = 10000;
        Lab::aura(w, "alert");
        lab.unit("d");
        Battle b(lab.c, lab.r, 1);
        b.addTeam(0, {"a", "f", "w"});
        b.addTeam(1, {"d"});
        b.start();
        act(b, 0, 1, 3);                                         // a uses its special on d
        CHECK_NEAR(b.units[1].tm, 1.0, 1e-9);                    // Frenzy: +100% turn meter
        CHECK_NEAR(b.units[3].health, 100000 - 10000 - 10000, 1e-9); // special + Alert's assist
    }
    // Cortosis Armor: an attacker loses its protection and 25% speed
    duel([](Lab& lab, UnitDef& a, UnitDef& d) {
            lab.r.statusesTickOnApplyTurn = false; // keep the 1-turn debuff visible after the attacker's own turn
            a.stats[idx(Stat::MaxProtection)] = 5000;
            Lab::aura(d, "cortosis_armor");
        }, [&](Battle& b, Lab&) {
        actBasic(b, 0, 1);
        CHECK_NEAR(b.units[0].protection, 0.0, 1e-9);
        CHECK(b.hasStatus(b.units[0], "speed_down"));
    });
    // Doomed: protection disabled, 20% max health lost at the end of each of its turns
    duel([](Lab&, UnitDef&, UnitDef& d) { d.stats[idx(Stat::MaxProtection)] = 50000; Lab::aura(d, "doomed"); }, [&](Battle& b, Lab&) {
        actBasic(b, 1, 0);
        CHECK_NEAR(hp(b, 1), 80000, 1e-9);
        CHECK_NEAR(b.units[1].protection, 50000, 1e-9);
    });
}

void testMultiFileContent() {
    section("content split across library / content / user files");
    const fs::path root = fs::temp_directory_path() / "gohclone_multifile";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "library", ec);
    fs::create_directories(root / "user", ec);
    auto write = [](const fs::path& p, const std::string& t) { std::ofstream(p) << t; };
    write(root / "library" / "s.json", R"({"statuses":[{"id":"s1","name":"S1","kind":"buff"}]})");
    write(root / "content.json",
          R"({"units":[{"id":"u1","stats":{"health":1000,"speed":100},"abilities":[{"id":"b","kind":"basic","effects":[{"type":"damage"}]}]}]})");
    write(root / "user" / "mine.json",
          R"({"units":[{"id":"mine","stats":{"health":2000,"speed":120},"abilities":[{"id":"b2","kind":"basic","effects":[{"type":"apply_status","status":"s1","target":"self"}]}]}],
              "teams":[{"id":"t","units":["u1","mine"]}]})");
    Content c = loadContent((root / "content.json").string());
    CHECK(c.units.size() == 2 && c.statuses.size() == 1 && c.teams.size() == 1);   // user file can use library statuses

    write(root / "user" / "dup.json", R"({"units":[{"id":"u1","stats":{"health":1,"speed":1},"abilities":[{"id":"x","kind":"basic","effects":[{"type":"damage"}]}]}]})");
    bool threw = false;
    try { loadContent((root / "content.json").string()); } catch (const std::exception&) { threw = true; }
    CHECK(threw);                                                                  // duplicate id across files is rejected
    fs::remove(root / "user" / "dup.json", ec);
    write(root / "user" / "broken.json", "{ nope");
    threw = false;
    try { loadContent((root / "content.json").string()); } catch (const std::exception& e) { threw = std::string(e.what()).find("broken.json") != std::string::npos; }
    CHECK(threw);                                                                  // errors name the offending file
    fs::remove_all(root, ec);
}


void testUserTools(const std::string& dir) {
    section("calibration helper, percent stats, and the shipped user examples");

    // --- percent <-> flat conversion and the calibration inverse of the damage formula
    CHECK_NEAR(percentFromFlat(flatFromPercent(41.27, 85), 85), 41.27, 1e-9);
    CHECK_NEAR(flatFromPercent(50.0, 85), 637.5, 1e-9);
    Rules rules;
    CalibrationInput in;
    in.offense = 11500;
    in.defenseRating = flatFromPercent(41.27, 85);
    const double predicted = predictDamage(rules, in, 1.645);
    CHECK_NEAR(calibrate(rules, in, {predicted}).perHit[0], 1.645, 1e-9);
    const CalibrationResult r = calibrate(rules, in, {predicted * 1.04, predicted * 0.97, predicted * 1.01});
    CHECK(r.consistent && r.low <= 1.645 && 1.645 <= r.high);
    CHECK_NEAR(r.estimate, 1.645, 0.03);
    CHECK((r.high - r.low) < 2.0 * 0.05 * 1.645);                       // three hits narrow the range
    CHECK(!calibrate(rules, in, {predicted, predicted * 1.8}).consistent); // a stray crit is detected
    CalibrationInput crit = in;
    crit.critMultiplier = 1.875;
    CHECK_NEAR(calibrate(rules, crit, {predictDamage(rules, crit, 2.2)}).estimate, 2.2, 0.12);

    // --- stats typed as the game shows them (*_pct keys)
    const fs::path p = fs::temp_directory_path() / "gohclone_pct.json";
    {
        std::ofstream f(p);
        f << R"({"units":[{"id":"u","level":85,"stats":{"health":1000,"speed":100,"armor_pct":41.27,"resistance_pct":0,
                 "potency_pct":105.2,"crit_damage_pct":187.5,"phys_crit_pct":62.5,"crit_avoid_pct":18},
                 "abilities":[{"id":"b","kind":"basic","effects":[{"type":"damage"}]}]}]})";
    }
    Content pc = loadContent(p.string());
    const UnitDef& pu = pc.unit("u");
    CHECK_NEAR(rules.mitigation(pu.stats[idx(Stat::Armor)], 85), 0.4127, 1e-4);
    CHECK_NEAR(pu.stats[idx(Stat::Resistance)], 0.0, 1e-9);
    CHECK_NEAR(pu.stats[idx(Stat::Potency)], 1.052, 1e-9);
    CHECK_NEAR(pu.stats[idx(Stat::CritDamage)], 1.875, 1e-9);
    CHECK_NEAR(pu.stats[idx(Stat::PhysCrit)], 0.625, 1e-9);
    CHECK_NEAR(pu.stats[idx(Stat::PhysCritAvoid)], 0.18, 1e-9);
    CHECK_NEAR(pu.stats[idx(Stat::SpecCritAvoid)], 0.18, 1e-9);            // alias sets both
    std::string pctJson = R"({"units":[{"id":"u","stats":{"health":1,"speed":1,"armor_pct":120},"abilities":[{"id":"b","kind":"basic","effects":[{"type":"damage"}]}]}]})";
    CHECK(!loadFromText(pctJson));                                          // 120% armor is impossible

    // --- the shipped example and cookbook files load, fight, and every recipe runs
    const fs::path root = fs::temp_directory_path() / "gohclone_user_examples";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "library", ec);
    fs::create_directories(root / "user", ec);
    const fs::path data(dir);
    const fs::path exampleSrc = data / "user" / "example_unit.json.txt", cookSrc = data / "user" / "cookbook.json.txt";
    if (!fs::exists(exampleSrc) || !fs::exists(cookSrc)) {
        std::printf("    (skipped) user example files not found in %s\n", (data / "user").string().c_str());
        return;
    }
    fs::copy_file(data / "content.json", root / "content.json", fs::copy_options::overwrite_existing, ec);
    for (const auto& e : fs::directory_iterator(data / "library"))
        fs::copy_file(e.path(), root / "library" / e.path().filename(), fs::copy_options::overwrite_existing, ec);
    fs::copy_file(exampleSrc, root / "user" / "example.json", fs::copy_options::overwrite_existing, ec);
    fs::copy_file(cookSrc, root / "user" / "cookbook.json", fs::copy_options::overwrite_existing, ec);
    Content c = loadContent((root / "content.json").string());
    Rules shipped = Rules::fromJson(json::parseFile((data / "rules.json").string()));
    CHECK(c.units.count("example_striker") && c.units.count("cookbook_unit") && c.teams.count("example_team"));

    AiController ai;
    int bad = 0, done = 0;
    for (int seed = 0; seed < 20; ++seed)
        for (const char* team : {"example_team"}) {
            Battle b(c, shipped, 700 + static_cast<std::uint64_t>(seed));
            b.addTeam(0, c.teams.at(team).units);
            b.addTeam(1, c.teams.at("syndicate_raiders").units);
            b.start();
            bool ok = true;
            while (b.step(ai, ai)) ok = ok && invariantsHold(b);
            ok = ok && invariantsHold(b);
            if (!ok) ++bad;
            if (b.winner() >= 0) ++done;
        }
    CHECK(bad == 0 && done == 20);

    // Run every recipe of the cookbook unit by force, in a battle where an ally has fallen (for the revive recipe).
    Battle b(c, shipped, 11);
    b.addTeam(0, {"cookbook_unit", "aria_vance", "kade_orlan", "elin_moss", "brask_orrin"});
    b.addTeam(1, c.teams.at("syndicate_raiders").units);
    b.start();
    int sable = -1;
    for (const BattleUnit& u : b.units)
        if (u.def->id == "sable_dray") sable = u.id;
    for (int hits = 0; hits < 40 && b.units[3].alive; ++hits) actBasic(b, sable, 3); // Elin (slot 3) falls
    CHECK(!b.units[3].alive);
    int ran = 0;
    bool allFine = true;
    for (std::size_t ai2 = 0; ai2 < b.units[0].abilities.size(); ++ai2) {
        const AbilityDef& ab = *b.units[0].abilities[ai2].def;
        if (ab.kind != AbilityKind::Basic && ab.kind != AbilityKind::Special && ab.kind != AbilityKind::Ultimate) continue;
        if (ab.id == "rc_execute") b.units[5].health = 1000; // a low-health target for the execute recipe
        b.units[0].abilities[ai2].cooldown = 0;
        b.units[0].abilities[ai2].charge = 100;
        b.units[0].health = b.maxHealth(b.units[0]);
        if (!b.abilityUsable(b.units[0], ai2)) { allFine = false; continue; }
        const std::vector<int> t = b.validTargets(b.units[0], ab);
        act(b, 0, static_cast<int>(ai2), t.empty() ? -1 : t[0]);
        allFine = allFine && invariantsHold(b);
        ++ran;
        if (b.over()) break;
    }
    std::printf("    %d cookbook recipes executed\n", ran);
    CHECK(allFine && ran >= 16);
    fs::remove_all(root, ec);
}

} // namespace

int runSelfTest(const std::string& dataDir) {
    std::printf("GoHClone self-test (data: %s)\n", dataDir.c_str());
    auto guarded = [](const char* name, const std::function<void()>& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            ++g_fail;
            std::printf("    EXCEPTION in %s: %s\n", name, e.what());
        }
    };
    guarded("json", testJson);
    guarded("formulas", testFormulas);
    guarded("loader", testLoaderValidation);
    guarded("damage", testDamageMath);
    guarded("chances", testChances);
    guarded("turn meter", testTurnMeter);
    guarded("statuses", testStatusMechanics);
    guarded("targeting", testTargeting);
    guarded("reactions", testReactions);
    guarded("full content", [&] { testFullContent(dataDir); });
    guarded("observer + arena", [&] { testObserverAndArena(dataDir); });
    guarded("random numbers", testRandomNumbers);
    guarded("presentation", [&] { testPresentationData(dataDir); });
    guarded("status library", [&] { testStatusLibrary(dataDir); });
    guarded("multi-file content", testMultiFileContent);
    guarded("tools + user examples", [&] { testUserTools(dataDir); });
    guarded("golden", [&] { testGoldenFiles(dataDir); });
    std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
