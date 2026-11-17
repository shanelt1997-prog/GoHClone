#include "Battle.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>

namespace {

std::string strf(const char* fmt, ...) {
    char buf[640];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return std::string(buf);
}

// 12345.6 -> "12,346"
std::string num(double v) {
    long long n = std::llround(v);
    const bool neg = n < 0;
    std::string digits = std::to_string(neg ? -n : n);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<size_t>(i), ",");
    return neg ? "-" + digits : digits;
}

bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

const char* ApplyWord(ApplyType a) {
    switch (a) {
    case ApplyType::Physical: return "physical";
    case ApplyType::Mental: return "special";
    default: return "unavoidable";
    }
}

} // namespace

#define LOG(...)                                       \
    do {                                               \
        if (logging || echo) addLog(strf(__VA_ARGS__)); \
    } while (0)

// ============================================================ setup ==========

Battle::Battle(const Content& c, const Rules& r, std::uint64_t seed) : content(c), rules(r), rng_(seed) {}

// The generator is std::mt19937_64, whose output sequence is fully specified by the C++ standard.
// The standard's *distributions* are NOT specified (compilers may differ), so the two conversions
// below are written out by hand. They use only integer arithmetic and one exact double multiply,
// so any language (C#, ...) can reproduce them bit for bit.
//
//   rand01():   take the top 53 bits of the next 64-bit output, divide by 2^53.
//   randInt(n): unbiased rejection sampling: draw r until r >= (2^64 mod n), result r mod n.
double Battle::rand01() { return static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0); }

int Battle::randInt(int n) {
    if (n <= 1) return 0;
    const std::uint64_t un = static_cast<std::uint64_t>(n);
    const std::uint64_t threshold = (0 - un) % un; // == 2^64 mod n
    std::uint64_t r;
    do {
        r = rng_();
    } while (r < threshold);
    return static_cast<int>(r % un);
}

void Battle::emit(VisKind kind, int unit, int other, double a, double b, bool flag, const std::string& text) {
    if (!observer) return;
    VisEvent e;
    e.kind = kind;
    e.unit = unit;
    e.other = other;
    e.a = a;
    e.b = b;
    e.flag = flag;
    e.text = text;
    observer->onEvent(e);
}

void Battle::addLog(const std::string& line) {
    if (logging) log.push_back(line);
    if (echo) std::printf("%s\n", line.c_str());
}

void Battle::addTeam(int side, const std::vector<std::string>& ids) {
    int slot = 0;
    for (const std::string& id : ids) {
        const UnitDef& d = content.unit(id);
        BattleUnit u;
        u.id = static_cast<int>(units.size());
        u.side = side;
        u.slot = slot++;
        u.def = &d;
        u.name = d.name;
        u.isLeader = (u.slot == 0);
        u.health = d.stats[idx(Stat::MaxHealth)];
        u.protection = d.stats[idx(Stat::MaxProtection)];
        std::size_t trig = 0;
        for (std::size_t a = 0; a < d.abilities.size(); ++a) {
            AbilityState as;
            as.def = &d.abilities[a];
            u.abilities.push_back(as);
            if (d.abilities[a].kind == AbilityKind::Basic) u.basicIdx = static_cast<int>(a);
            u.trigBase.push_back(trig);
            trig += d.abilities[a].triggers.size();
        }
        u.lastFired.assign(trig, -1);
        u.fireCount.assign(trig, 0);
        // Keep names unique so mirror matches stay readable in the log.
        for (bool clash = true; clash;) {
            clash = false;
            for (const BattleUnit& o : units)
                if (o.name == u.name) { u.name += "'"; clash = true; break; }
        }
        units.push_back(std::move(u));
    }
}

void Battle::start() {
    LOG("=== Battle begins ===");
    auto applyAuras = [&](BattleUnit& owner, const AbilityDef& ab) {
        for (const AuraDef& au : ab.auras) {
            EffectCtx c;
            c.source = owner.id;
            c.fromTrigger = true;
            std::vector<int> ts = resolveTargets(au.target, c);
            const StatusDef& sd = content.status(au.statusId);
            for (int t : ts) applyStatus(owner.id, t, sd, au.duration, 1, au.amount, false, false, true);
        }
    };
    // Documented order: enemy uniques, player uniques, player lead, enemy lead.
    for (int side : {1, 0})
        for (BattleUnit& u : units)
            if (u.side == side)
                for (AbilityState& as : u.abilities)
                    if (as.def->kind == AbilityKind::Unique) applyAuras(u, *as.def);
    for (int side : {0, 1})
        for (BattleUnit& u : units)
            if (u.side == side && u.isLeader)
                for (AbilityState& as : u.abilities)
                    if (as.def->kind == AbilityKind::Leader) applyAuras(u, *as.def);
    for (BattleUnit& u : units) {
        EventData ev;
        ev.type = EventType::StartOfBattle;
        ev.subject = u.id;
        fire(ev);
    }
}

// ============================================================ queries ========

double Battle::stat(const BattleUnit& u, Stat s) const {
    if (s == Stat::Speed && hasFlag(u, SF_SpeedZero)) return 0.0;
    const double base = u.def->stats[idx(s)];
    double add = 0.0, pct = 0.0;
    for (const StatusInst& si : u.statuses) {
        if (!si.def->excludes.empty() && excludedBy(u, *si.def)) continue; // e.g. Breach yields to Speed Down
        for (const StatMod& m : si.def->mods)
            if (m.stat == s) {
                const double v = m.value * si.stacks;
                if (m.op == ModOp::Add) add += v;
                else pct += v;
            }
    }
    return base + add + pct * base;
}

double Battle::maxHealth(const BattleUnit& u) const { return std::max(1.0, stat(u, Stat::MaxHealth)); }
double Battle::maxProtection(const BattleUnit& u) const { return std::max(0.0, stat(u, Stat::MaxProtection)) + u.bonusProt; }
double Battle::healthFrac(const BattleUnit& u) const { return u.health / maxHealth(u); }
double Battle::survivabilityFrac(const BattleUnit& u) const {
    return (u.health + u.protection) / (maxHealth(u) + maxProtection(u));
}

bool Battle::hasFlag(const BattleUnit& u, std::uint32_t flag) const {
    for (const StatusInst& si : u.statuses)
        if (si.def->flags & flag) return true;
    return false;
}

bool Battle::hasStatus(const BattleUnit& u, const std::string& id) const {
    for (const StatusInst& si : u.statuses)
        if (si.def->id == id) return true;
    return false;
}

int Battle::stackCount(const BattleUnit& u, const std::string& id) const {
    for (const StatusInst& si : u.statuses)
        if (si.def->id == id) return si.stacks;
    return 0;
}

bool Battle::excludedBy(const BattleUnit& u, const StatusDef& d) const {
    for (const std::string& id : d.excludes)
        for (const StatusInst& si : u.statuses)
            if (si.def->id == id) return true;
    return false;
}

// Uses up one stack of the first status carrying `flag` (Foresight, Advantage, Blind, ...).
void Battle::consumeFlagStatus(BattleUnit& u, std::uint32_t flag) {
    for (std::size_t i = 0; i < u.statuses.size(); ++i)
        if (u.statuses[i].def->flags & flag) {
            if (u.statuses[i].stacks > 1) u.statuses[i].stacks--;
            else removeStatusIndices(u, {i});
            return;
        }
}

int Battle::statusCount(const BattleUnit& u, StatusKind kind) const {
    int n = 0;
    for (const StatusInst& si : u.statuses)
        if (si.def->kind == kind) ++n;
    return n;
}

int Battle::aliveCount(int side) const {
    int n = 0;
    for (const BattleUnit& u : units)
        if (u.side == side && u.alive) ++n;
    return n;
}

bool Battle::over() const { return timedOut || aliveCount(0) == 0 || aliveCount(1) == 0; }

int Battle::winner() const {
    const int a = aliveCount(0), b = aliveCount(1);
    if (a == 0 && b == 0) return 2;
    if (b == 0) return 0;
    if (a == 0) return 1;
    return timedOut ? 2 : -1;
}

std::vector<int> Battle::validTargets(const BattleUnit& src, const AbilityDef& ab) const {
    std::vector<int> out;
    switch (ab.pick) {
    case Pick::Enemy: {
        std::vector<int> all, open;
        for (const BattleUnit& u : units)
            if (u.alive && u.side != src.side) {
                all.push_back(u.id);
                if (!hasFlag(u, SF_Stealth)) open.push_back(u.id);
            }
        // Stealthed units can't be targeted unless every enemy is stealthed.
        std::vector<int>& pool = open.empty() ? all : open;
        std::vector<int> taunters;
        if (!hasFlag(src, SF_IgnoreTaunt))
            for (int id : pool)
                if (hasFlag(units[static_cast<size_t>(id)], SF_Taunt)) taunters.push_back(id);
        out = taunters.empty() ? pool : taunters;
        break;
    }
    case Pick::Ally:
        for (const BattleUnit& u : units)
            if (u.alive && u.side == src.side) out.push_back(u.id);
        break;
    case Pick::DeadAlly:
        for (const BattleUnit& u : units)
            if (!u.alive && u.side == src.side) out.push_back(u.id);
        break;
    case Pick::Self: out.push_back(src.id); break;
    case Pick::None: break;
    }
    return out;
}

bool Battle::abilityUsable(const BattleUnit& u, std::size_t ai, std::string* why) const {
    const AbilityState& as = u.abilities[ai];
    const AbilityDef& ab = *as.def;
    auto no = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (ab.kind == AbilityKind::Leader || ab.kind == AbilityKind::Unique) return no("passive");
    if (ab.kind != AbilityKind::Basic) {
        if (hasFlag(u, SF_AbilityBlock)) return no("Ability Block");
        if (as.cooldown > 0) return no("cooldown " + std::to_string(as.cooldown));
        if (ab.kind == AbilityKind::Ultimate && as.charge < ab.ultimateCharge)
            return no("charging " + std::to_string(as.charge) + "/" + std::to_string(ab.ultimateCharge));
    }
    if ((ab.pick == Pick::Enemy || ab.pick == Pick::Ally || ab.pick == Pick::DeadAlly) && validTargets(u, ab).empty())
        return no("no valid target");
    return true;
}

// ============================================================ targeting ======

std::vector<int> Battle::candidates(const TargetSpec& t, const EffectCtx& c) const {
    std::vector<int> out;
    const BattleUnit& src = units[static_cast<size_t>(c.source)];
    for (const BattleUnit& u : units) {
        if (t.side == Side::Enemy && u.side == src.side) continue;
        if (t.side == Side::Ally && u.side != src.side) continue;
        if (t.health == HealthState::Alive && !u.alive) continue;
        if (t.health == HealthState::Dead && u.alive) continue;
        if (t.excludeSelf && u.id == src.id) continue;
        if (t.excludeSelected && u.id == c.selected) continue;
        if (!t.anyTags.empty()) {
            bool ok = false;
            for (const std::string& tag : t.anyTags)
                if (u.def->hasTag(tag)) { ok = true; break; }
            if (!ok) continue;
        }
        bool banned = false;
        for (const std::string& tag : t.noTags)
            if (u.def->hasTag(tag)) { banned = true; break; }
        if (banned) continue;
        if (!t.hasStatus.empty() && !hasStatus(u, t.hasStatus)) continue;
        if (!t.lacksStatus.empty() && hasStatus(u, t.lacksStatus)) continue;
        if (t.healthBelow >= 0 && healthFrac(u) > t.healthBelow) continue;
        if (t.healthAbove >= 0 && healthFrac(u) < t.healthAbove) continue;
        out.push_back(u.id);
    }
    return out;
}

std::vector<int> Battle::resolveTargets(const TargetSpec& t, const EffectCtx& c) {
    auto single = [&](int id) {
        std::vector<int> v;
        if (id < 0 || id >= static_cast<int>(units.size())) return v;
        const BattleUnit& u = units[static_cast<size_t>(id)];
        if (t.health == HealthState::Alive && !u.alive) return v;
        if (t.health == HealthState::Dead && u.alive) return v;
        v.push_back(id);
        return v;
    };
    switch (t.select) {
    case Select::Self: return {c.source};
    case Select::EventSubject: return single(c.evSubject);
    case Select::EventOther: return single(c.evOther);
    case Select::HitTarget: return single(c.hitTarget);
    default: break;
    }

    std::vector<int> cand = candidates(t, c);
    if (cand.empty()) return cand;
    switch (t.select) {
    case Select::All: return cand;
    case Select::Selected:
        // A chosen target that is no longer a legal candidate (e.g. it died earlier in the
        // same ability) yields no target; effects must never be redirected to a bystander.
        if (c.selected >= 0) return contains(cand, c.selected) ? std::vector<int>{c.selected} : std::vector<int>{};
        return {cand[static_cast<size_t>(randInt(static_cast<int>(cand.size())))]};
    case Select::Random: {
        std::vector<int> out;
        int n = std::min(std::max(1, t.count), static_cast<int>(cand.size()));
        for (int i = 0; i < n; ++i) {
            int k = randInt(static_cast<int>(cand.size()));
            out.push_back(cand[static_cast<size_t>(k)]);
            cand.erase(cand.begin() + k);
        }
        return out;
    }
    case Select::LowestSurvivability:
    case Select::HighestSurvivability: {
        const bool lowest = t.select == Select::LowestSurvivability;
        int best = cand[0];
        double bv = survivabilityFrac(units[static_cast<size_t>(best)]);
        for (int id : cand) {
            const double v = survivabilityFrac(units[static_cast<size_t>(id)]);
            if (lowest ? v < bv : v > bv) { bv = v; best = id; }
        }
        return {best};
    }
    case Select::Leader:
        for (int id : cand)
            if (units[static_cast<size_t>(id)].isLeader) return {id};
        return {};
    default: return {};
    }
}

// ============================================================ effects ========

void Battle::execEffects(const std::vector<EffectDef>& list, EffectCtx& c) {
    for (const EffectDef& e : list) execEffect(e, c);
}

double Battle::basisValue(const std::string& b, const BattleUnit& s, const BattleUnit& t, double evAmount) const {
    if (b == "event_amount") return evAmount;
    if (b == "source_max_health") return maxHealth(s);
    if (b == "target_max_health") return maxHealth(t);
    if (b == "source_health") return s.health;
    if (b == "target_health") return t.health;
    if (b == "target_missing_health") return maxHealth(t) - t.health;
    if (b == "source_physical_damage") return stat(s, Stat::PhysDamage);
    if (b == "source_special_damage") return stat(s, Stat::SpecDamage);
    if (b == "target_protection") return t.protection;
    return 0.0;
}

bool Battle::tryEvade(BattleUnit& src, BattleUnit& tgt, ApplyType apply, const AbilityDef* ab, bool attack) {
    if (apply == ApplyType::Unavoidable || src.side == tgt.side) return false;

    // A blinded attacker misses its next attack.
    if (attack && hasFlag(src, SF_Blind)) {
        consumeFlagStatus(src, SF_Blind);
        LOG("    %s is blinded and misses %s", src.name.c_str(), tgt.name.c_str());
        emit(VisKind::Evade, tgt.id, src.id);
        return true;
    }
    // Stunned / feared / burning units cannot avoid attacks at all.
    if (hasFlag(tgt, SF_NoEvade)) return false;

    bool evaded = false;
    if (attack && hasFlag(tgt, SF_Foresight)) { // Foresight: guaranteed evasion of the next attack
        consumeFlagStatus(tgt, SF_Foresight);
        LOG("    %s foresees the attack and evades", tgt.name.c_str());
        evaded = true;
    } else {
        const double ev = Rules::evadeChance(stat(tgt, apply == ApplyType::Physical ? Stat::Dodge : Stat::Deflection),
                                             stat(src, Stat::Accuracy));
        if (ev <= 0.0 || rand01() >= ev) return false;
        LOG("    %s evades (%s)", tgt.name.c_str(), ApplyWord(apply));
        evaded = true;
    }
    if (!evaded) return false;
    tgt.st.dodges++;
    emit(VisKind::Evade, tgt.id, src.id);
    EventData e1;
    e1.type = EventType::Dodged;
    e1.subject = tgt.id;
    e1.other = src.id;
    e1.ability = ab;
    fire(e1);
    EventData e2;
    e2.type = EventType::WasDodged;
    e2.subject = src.id;
    e2.other = tgt.id;
    e2.ability = ab;
    fire(e2);
    return true;
}

bool Battle::tryResist(BattleUnit& src, BattleUnit& tgt, const StatusDef* st) {
    const double r = rules.resistChance(stat(tgt, Stat::Tenacity), stat(src, Stat::Potency));
    if (rand01() >= r) return false;
    tgt.st.resists++;
    LOG("    %s resists%s%s", tgt.name.c_str(), st ? " " : "", st ? st->name.c_str() : "");
    emit(VisKind::Resist, tgt.id, src.id, 0.0, 0.0, false, st ? st->name : std::string());
    EventData e1;
    e1.type = EventType::Resisted;
    e1.subject = tgt.id;
    e1.other = src.id;
    e1.status = st;
    fire(e1);
    EventData e2;
    e2.type = EventType::WasResisted;
    e2.subject = src.id;
    e2.other = tgt.id;
    e2.status = st;
    fire(e2);
    return true;
}

bool Battle::hit(const EffectDef& e, EffectCtx& c, BattleUnit& src, BattleUnit& tgt) {
    if (!tgt.alive) return false;
    if (e.canEvade && tryEvade(src, tgt, e.apply, c.ability, true)) return false;

    // Damage dealt by an effect to itself, or flagged no_reactions (true damage from a status, a retaliation),
    // is not an attack: it must not trigger "when damaged" reactions, counters or health steal.
    const bool reactions = !e.noReactions && src.id != tgt.id;

    // 1. source damage
    double base;
    if (!e.basis.empty()) base = basisValue(e.basis, src, tgt, c.evAmount);
    else base = stat(src, e.damageType == DamageType::AbilityPower ? Stat::SpecDamage : Stat::PhysDamage);
    double mult = e.multiplier;
    if (!e.scaleByStatus.empty()) mult *= stackCount(src, e.scaleByStatus);
    double dmg = base * mult * c.scale + e.additive;

    // 2. critical hit
    bool crit = false;
    if (e.canCrit && e.damageType != DamageType::Unmitigable) {
        const bool phys = e.damageType == DamageType::AttackDamage;
        double chance = Rules::critChance(stat(src, phys ? Stat::PhysCrit : Stat::SpecCrit),
                                          stat(tgt, phys ? Stat::PhysCritAvoid : Stat::SpecCritAvoid));
        if (e.forceCrit) chance = 1.0;
        if (hasFlag(src, SF_Advantage)) { chance = 1.0; c.usedAdvantage = true; } // Advantage: next attack crits
        if (hasFlag(tgt, SF_Vulnerable)) chance = 1.0;                           // Vulnerable: will be crit
        if (hasFlag(tgt, SF_CritImmunity)) chance = 0.0;
        crit = chance >= 1.0 || (chance > 0.0 && rand01() < chance);
        if (crit) dmg *= std::max(1.0, stat(src, Stat::CritDamage));
    }

    // 3. variance
    const double var = e.variance >= 0.0 ? e.variance : rules.variance;
    if (var > 0.0) dmg *= 1.0 + (rand01() * 2.0 - 1.0) * var;

    // 4. damage modification
    dmg *= stat(src, Stat::DamageDealtMult) * stat(tgt, Stat::DamageTakenMult);
    if (c.outOfTurn && !c.fromTrigger) dmg *= stat(src, Stat::OutOfTurnDamageMult);

    // 5. mitigation
    if (e.damageType != DamageType::Unmitigable) {
        const bool phys = e.damageType == DamageType::AttackDamage;
        const double def = Rules::effectiveDefense(stat(tgt, phys ? Stat::Armor : Stat::Resistance),
                                                   stat(src, Stat::DefensePen),
                                                   stat(src, phys ? Stat::ArmorPen : Stat::ResistancePen));
        dmg *= 1.0 - rules.mitigation(def, tgt.def->level);
    }
    const bool immune = hasFlag(tgt, SF_DamageImmunity);
    if (immune) dmg = 0.0;
    if (!immune && hasFlag(tgt, SF_DamageToOne) && dmg > 0.0) dmg = 1.0; // Deflective Ward
    if (rules.roundDamage) dmg = std::floor(dmg + 0.5);
    if (dmg < 0.0) dmg = 0.0;

    // 6. apply
    const bool skipProt = e.ignoreProtection || hasFlag(tgt, SF_ProtectionDisabled) ||
                          (c.outOfTurn && !c.fromTrigger && hasFlag(src, SF_RiposteIgnoreProt));
    if (e.cannotDefeat) { // true damage that may not finish the target
        const double avail = skipProt ? tgt.health : tgt.health + tgt.protection;
        dmg = std::min(dmg, std::max(0.0, avail - 1.0));
    }
    const DamageOutcome o = applyDamage(tgt, dmg, skipProt);
    const double total = o.prot + o.hp;
    if (src.id != tgt.id) src.st.damageDealt += static_cast<long long>(total);
    tgt.st.damageTaken += static_cast<long long>(total);
    if (crit) src.st.crits++;
    c.dealtDamage = true;
    c.totalDamage += total;
    if (c.firstHit < 0) c.firstHit = tgt.id;
    if (immune) {
        LOG("    %s hits %s - immune to damage", src.name.c_str(), tgt.name.c_str());
    } else {
        LOG("    %s hits %s for %s%s  [prot -%s, hp -%s]", src.name.c_str(), tgt.name.c_str(), num(total).c_str(),
            crit ? " CRIT" : "", num(o.prot).c_str(), num(o.hp).c_str());
    }

    emit(VisKind::Damage, tgt.id, src.id, o.prot, o.hp, crit, immune ? std::string("immune") : std::string());

    // health steal
    const double steal = stat(src, Stat::HealthSteal);
    if (reactions && steal > 0.0 && total > 0.0)
        healUnit(src, std::floor(steal * (rules.healthStealIncludesProtection ? total : o.hp)), 0.0, src.id);

    // events
    if (reactions && total > 0.0) {
        EventData ev;
        ev.type = EventType::ReceiveDamage;
        ev.subject = tgt.id;
        ev.other = src.id;
        ev.amount = total;
        ev.ability = c.ability;
        ev.inTurn = !c.outOfTurn;
        fire(ev);
    }
    if (reactions && crit) {
        EventData a;
        a.type = EventType::Crit;
        a.subject = src.id;
        a.other = tgt.id;
        a.ability = c.ability;
        a.inTurn = !c.outOfTurn;
        fire(a);
        EventData b;
        b.type = EventType::ReceiveCrit;
        b.subject = tgt.id;
        b.other = src.id;
        b.ability = c.ability;
        b.inTurn = !c.outOfTurn;
        fire(b);
    }
    if (o.lethal) {
        onLethal(tgt, src.id);
    } else if (reactions && !c.isCounter && tgt.alive && tgt.side != src.side && total > 0.0 && !hasFlag(tgt, SF_NoCounter) &&
               !hasFlag(src, SF_Uncounterable)) {
        const double cc = stat(tgt, Stat::CounterChance);
        if (c.counteredBy.size() != units.size()) c.counteredBy.assign(units.size(), 0);
        if (cc > 0.0 && !c.counteredBy[static_cast<size_t>(tgt.id)] && rand01() < cc) {
            c.counteredBy[static_cast<size_t>(tgt.id)] = 1;
            counters_.push_back({tgt.id, src.id, 1.0, true});
        }
    }
    return true;
}

Battle::DamageOutcome Battle::applyDamage(BattleUnit& t, double dmg, bool ignoreProtection) {
    DamageOutcome o;
    if (!t.alive || dmg <= 0.0) return o;
    double rem = dmg;
    if (!ignoreProtection && t.protection > 0.0) {
        const double p = std::min(t.protection, rem);
        t.protection -= p;
        rem -= p;
        o.prot = p;
        if (!rules.damageOverflowToHealth) rem = 0.0;
    }
    if (rem > 0.0) {
        const double h = std::min(t.health, rem);
        t.health -= h;
        o.hp = h;
    }
    if (t.health <= 0.0) {
        t.health = 0.0;
        o.lethal = true;
    }
    return o;
}

void Battle::onLethal(BattleUnit& t, int killer, bool instant) {
    // 1) "Survive" effects (Endure / Second Wind): lethal damage leaves the unit at 1% health plus the
    //    listed recovery, and uses up one stack of the effect. Instant defeats bypass them.
    if (!instant) {
        for (std::size_t i = 0; i < t.statuses.size(); ++i) {
            const StatusDef* d = t.statuses[i].def;
            if (!d->survive) continue;
            if (t.statuses[i].stacks > 1) t.statuses[i].stacks--;
            else removeStatusIndices(t, {i});
            t.health = std::min(maxHealth(t), std::max(1.0, std::floor(maxHealth(t) * (0.01 + d->surviveHealth))));
            if (d->surviveProtection > 0.0 && !hasFlag(t, SF_ProtectionDisabled))
                t.protection = std::min(maxProtection(t), t.protection + std::floor(maxProtection(t) * d->surviveProtection));
            LOG("    %s survives with %s health", t.name.c_str(), num(t.health).c_str());
            emit(VisKind::Revive, t.id, -1, t.health);
            return;
        }
    }
    // 2) Revive effects: the unit comes back unless something forbids it.
    if (!hasFlag(t, SF_NoRevive)) {
        for (std::size_t i = 0; i < t.statuses.size(); ++i) {
            const StatusDef* d = t.statuses[i].def;
            if (d->reviveHealth <= 0.0) continue;
            const double frac = d->reviveHealth, protFrac = d->reviveProtection, tmGain = d->reviveTurnMeter;
            const bool bonus = d->reviveBonusTurn;
            removeStatusIndices(t, {i});
            t.health = std::min(maxHealth(t), std::max(1.0, std::floor(maxHealth(t) * frac)));
            t.protection = protFrac > 0.0 && !hasFlag(t, SF_ProtectionDisabled)
                               ? std::min(maxProtection(t), std::floor(maxProtection(t) * protFrac))
                               : 0.0;
            t.tm = tmGain;
            if (bonus) t.bonusTurn = true;
            LOG("    %s is revived with %s health", t.name.c_str(), num(t.health).c_str());
            emit(VisKind::Revive, t.id, -1, t.health);
            return;
        }
    }
    // 3) Defeat.
    t.cannotRevive = hasFlag(t, SF_NoRevive);
    t.alive = false;
    t.health = 0.0;
    t.protection = 0.0;
    t.bonusProt = 0.0;
    t.tm = 0.0;
    t.bonusTurn = false;
    t.statuses.clear();
    LOG("    %s is defeated", t.name.c_str());
    emit(VisKind::Defeat, t.id, killer);
    if (killer >= 0 && killer != t.id) units[static_cast<size_t>(killer)].st.kills++;
    EventData d;
    d.type = EventType::Death;
    d.subject = t.id;
    d.other = killer;
    fire(d);
    if (killer >= 0 && killer != t.id) {
        EventData k;
        k.type = EventType::Kill;
        k.subject = killer;
        k.other = t.id;
        fire(k);
    }
}

bool Battle::healUnit(BattleUnit& u, double hp, double prot, int srcId) {
    if (!u.alive || hasFlag(u, SF_HealingImmunity)) return false;
    const double h = std::max(0.0, std::min(hp, maxHealth(u) - u.health));
    const bool noProt = hasFlag(u, SF_ProtectionDisabled) || hasFlag(u, SF_NoProtectionRecovery);
    const double p = noProt ? 0.0 : std::max(0.0, std::min(prot, maxProtection(u) - u.protection));
    u.health += h;
    u.protection += p;
    if (h + p <= 0.0) return false;
    if (srcId >= 0) units[static_cast<size_t>(srcId)].st.healingDone += static_cast<long long>(h + p);
    emit(VisKind::Heal, u.id, srcId, h, p);
    if (h > 0 && p > 0) LOG("    %s recovers %s health and %s protection", u.name.c_str(), num(h).c_str(), num(p).c_str());
    else if (h > 0) LOG("    %s recovers %s health", u.name.c_str(), num(h).c_str());
    else LOG("    %s recovers %s protection", u.name.c_str(), num(p).c_str());
    EventData ev;
    ev.type = EventType::ReceiveHeal;
    ev.subject = u.id;
    ev.other = srcId;
    ev.amount = h + p;
    fire(ev);
    return true;
}

void Battle::clampPools(BattleUnit& u) {
    u.health = std::min(u.health, maxHealth(u));
    u.protection = std::min(u.protection, maxProtection(u));
}

void Battle::removeStatusIndices(BattleUnit& u, std::vector<std::size_t> indices) {
    if (indices.empty()) return;
    std::sort(indices.begin(), indices.end(), std::greater<std::size_t>());
    std::vector<const StatusDef*> removed;
    for (std::size_t i : indices) {
        if (i >= u.statuses.size()) continue;
        const StatusInst& si = u.statuses[i];
        if (si.bonusApplied > 0.0) u.bonusProt = std::max(0.0, u.bonusProt - si.bonusApplied);
        removed.push_back(si.def);
        u.statuses.erase(u.statuses.begin() + static_cast<std::ptrdiff_t>(i));
    }
    clampPools(u);
    for (const StatusDef* d : removed) {
        LOG("    %s loses %s", u.name.c_str(), d->name.c_str());
        emit(VisKind::StatusLose, u.id, -1, 0.0, 0.0, false, d->name);
        EventData ev;
        ev.type = EventType::StatusRemoved;
        ev.subject = u.id;
        ev.status = d;
        fire(ev);
    }
}

Battle::ApplyResult Battle::applyStatus(int srcId, int tgtId, const StatusDef& def, int duration, int stacks,
                                        double amount, bool allowResist, bool canResist, bool fromAura) {
    BattleUnit& t = units[static_cast<size_t>(tgtId)];
    if (!t.alive) return ApplyResult::Failed;
    const bool debuff = def.kind == StatusKind::Debuff;
    const bool buff = def.kind == StatusKind::Buff;
    if (!fromAura) {
        if ((debuff && hasFlag(t, SF_DebuffImmunity)) || (buff && hasFlag(t, SF_BuffImmunity))) {
            LOG("    %s is immune to %s", t.name.c_str(), def.name.c_str());
            return ApplyResult::Immune;
        }
        if (def.mode == AmountMode::BonusProtection && hasFlag(t, SF_ProtectionDisabled)) {
            LOG("    %s cannot gain protection", t.name.c_str());
            return ApplyResult::Immune;
        }
        if (buff && hasFlag(t, SF_NoBuffsOffTurn) && actorNow_ != t.id) {
            LOG("    %s cannot gain %s during another unit's turn", t.name.c_str(), def.name.c_str());
            return ApplyResult::Immune;
        }
        for (const StatusInst& si : t.statuses)
            for (const std::string& b : si.def->blocks)
                if (b == def.id || (debuff && b == "debuff") || (buff && b == "buff")) {
                    LOG("    %s is immune to %s (%s)", t.name.c_str(), def.name.c_str(), si.def->name.c_str());
                    return ApplyResult::Immune;
                }
        if (allowResist && canResist && debuff && srcId >= 0 && units[static_cast<size_t>(srcId)].side != t.side) {
            if (tryResist(units[static_cast<size_t>(srcId)], t, &def)) return ApplyResult::Resisted;
        }
    }
    const int dur = fromAura ? -1 : (duration == -999 ? def.duration : duration);
    const double amt = amount > 0.0 ? amount : def.amount;

    for (StatusInst& si : t.statuses) {
        if (si.def != &def) continue;
        if (def.maxStacks > 1) si.stacks = std::min(def.maxStacks, si.stacks + std::max(1, stacks));
        si.remaining = dur; // re-applying refreshes the duration
        si.appliedSeq = turnSeq;
        si.source = srcId;
        if (def.mode != AmountMode::None && def.mode != AmountMode::BonusProtection) si.amount = amt;
        LOG("    %s's %s is refreshed", t.name.c_str(), def.name.c_str());
        return ApplyResult::Applied;
    }

    StatusInst ni;
    ni.def = &def;
    ni.stacks = std::clamp(stacks, 1, std::max(1, def.maxStacks));
    ni.remaining = dur;
    ni.source = srcId;
    ni.fromAura = fromAura;
    ni.amount = amt;
    ni.appliedSeq = turnSeq;
    ni.lastFired.assign(def.triggers.size(), -1);
    ni.fireCount.assign(def.triggers.size(), 0);
    if (def.mode == AmountMode::BonusProtection) {
        const double g = std::floor(amt * maxHealth(t));
        ni.bonusApplied = g;
        t.bonusProt += g;
        t.protection += g;
    }
    t.statuses.push_back(ni);
    if (debuff && srcId >= 0 && units[static_cast<size_t>(srcId)].side != t.side)
        units[static_cast<size_t>(srcId)].st.debuffsLanded++;
    if (!fromAura) {
        if (dur > 0) LOG("    %s gains %s (%d turn%s)", t.name.c_str(), def.name.c_str(), dur, dur == 1 ? "" : "s");
        else LOG("    %s gains %s", t.name.c_str(), def.name.c_str());
        emit(VisKind::StatusGain, tgtId, srcId, 0.0, 0.0, debuff, def.name);
    }
    EventData ev;
    ev.type = EventType::StatusAdded;
    ev.subject = tgtId;
    ev.other = srcId;
    ev.status = &def;
    fire(ev);
    return ApplyResult::Applied;
}

int Battle::dispel(int tgtId, const std::string& kind, int count) {
    BattleUnit& t = units[static_cast<size_t>(tgtId)];
    std::vector<std::size_t> pool;
    for (std::size_t i = 0; i < t.statuses.size(); ++i) {
        const StatusInst& si = t.statuses[i];
        if (!si.def->dispellable || si.fromAura) continue;
        const bool b = si.def->kind == StatusKind::Buff, d = si.def->kind == StatusKind::Debuff;
        if ((kind == "buff" && b) || (kind == "debuff" && d) || (kind == "all" && (b || d))) pool.push_back(i);
    }
    std::vector<std::size_t> chosen;
    if (count <= 0 || count >= static_cast<int>(pool.size())) {
        chosen = pool;
    } else {
        for (int i = 0; i < count; ++i) {
            const int k = randInt(static_cast<int>(pool.size()));
            chosen.push_back(pool[static_cast<size_t>(k)]);
            pool.erase(pool.begin() + k);
        }
    }
    const int n = static_cast<int>(chosen.size());
    if (n > 0) removeStatusIndices(t, chosen);
    return n;
}

bool Battle::modifyTurnMeter(BattleUnit& src, BattleUnit& tgt, double amount, const EffectDef& e, const AbilityDef* ab) {
    if (!tgt.alive) return false;
    if (amount > 0.0) {
        if (hasFlag(tgt, SF_NoTMGain)) {
            LOG("    %s cannot gain turn meter", tgt.name.c_str());
            return false;
        }
        tgt.tm = std::min(tgt.tm + amount, rules.tmCap);
        LOG("    %s gains %d%% turn meter (now %d%%)", tgt.name.c_str(), static_cast<int>(std::lround(amount * 100)),
            static_cast<int>(std::lround(tgt.tm * 100)));
        emit(VisKind::TurnMeter, tgt.id, src.id, amount);
        return true;
    }
    if (amount < 0.0) {
        if (src.side != tgt.side && hasFlag(tgt, SF_TMImmune)) {
            LOG("    %s is immune to turn meter reduction", tgt.name.c_str());
            return false;
        }
        if (src.side != tgt.side) {
            if (e.canEvade && tryEvade(src, tgt, e.apply, ab)) return false;
            if (e.canResist && tryResist(src, tgt, nullptr)) return false;
        }
        tgt.tm = std::max(0.0, tgt.tm + amount);
        LOG("    %s loses %d%% turn meter (now %d%%)", tgt.name.c_str(), static_cast<int>(std::lround(-amount * 100)),
            static_cast<int>(std::lround(tgt.tm * 100)));
        emit(VisKind::TurnMeter, tgt.id, src.id, amount);
        return true;
    }
    return false;
}

void Battle::execEffect(const EffectDef& e, EffectCtx& c) {
    BattleUnit& src = units[static_cast<size_t>(c.source)];
    if (!src.alive && !c.fromTrigger) return;
    if (!e.ifSourceHasStatus.empty() && !hasStatus(src, e.ifSourceHasStatus)) return;
    if (!e.ifSourceLacksStatus.empty() && hasStatus(src, e.ifSourceLacksStatus)) return;

    const std::vector<int> targets = resolveTargets(e.target, c);
    for (int tid : targets) {
        BattleUnit& tgt = units[static_cast<size_t>(tid)];
        if (!e.ifTargetHasStatus.empty() && !hasStatus(tgt, e.ifTargetHasStatus)) continue;
        if (!e.ifTargetLacksStatus.empty() && hasStatus(tgt, e.ifTargetLacksStatus)) continue;
        if (e.ifTargetHealthBelow >= 0 && healthFrac(tgt) > e.ifTargetHealthBelow) continue;
        if (e.chance < 1.0 && rand01() >= e.chance) continue;

        bool landed = false;
        switch (e.type) {
        case EffectType::Damage:
            for (int h = 0; h < e.hits; ++h) {
                if (!tgt.alive) break;
                if (hit(e, c, src, tgt)) landed = true;
            }
            break;

        case EffectType::Heal: {
            const double basis = e.basis.empty() ? maxHealth(tgt) : basisValue(e.basis, src, tgt, c.evAmount);
            const double hp = std::floor(basis * e.multiplier + e.additive);
            const double pr = std::floor(basis * e.protectionMultiplier);
            landed = healUnit(tgt, hp, pr, src.id);
            break;
        }

        case EffectType::ApplyStatus: {
            const StatusDef& sd = content.status(e.statusId);
            if (sd.kind == StatusKind::Debuff && e.canEvade && tryEvade(src, tgt, e.apply, c.ability)) break;
            const ApplyResult r = applyStatus(c.source, tid, sd, e.duration, e.stacks, e.amount, true, e.canResist, false);
            landed = r == ApplyResult::Applied;
            break;
        }

        case EffectType::ModifyTurnMeter:
            landed = modifyTurnMeter(src, tgt, e.amount, e, c.ability);
            break;

        case EffectType::ModifyCooldown: {
            const int delta = static_cast<int>(std::lround(e.amount));
            for (AbilityState& as : tgt.abilities) {
                const AbilityKind k = as.def->kind;
                if (k == AbilityKind::Basic || k == AbilityKind::Leader || k == AbilityKind::Unique) continue;
                if (e.abilityRef != "*" && !e.abilityRef.empty() && as.def->id != e.abilityRef) continue;
                const int before = as.cooldown;
                as.cooldown = std::max(0, as.cooldown + delta);
                if (as.cooldown != before) {
                    LOG("    %s's %s cooldown %d -> %d", tgt.name.c_str(), as.def->name.c_str(), before, as.cooldown);
                    landed = true;
                }
            }
            break;
        }

        case EffectType::ModifyUltimate:
            for (AbilityState& as : tgt.abilities)
                if (as.def->kind == AbilityKind::Ultimate) {
                    const int before = as.charge;
                    as.charge = std::clamp(as.charge + static_cast<int>(std::lround(e.amount)), 0, as.def->ultimateCharge);
                    if (as.charge != before) {
                        LOG("    %s's ultimate charge %d -> %d", tgt.name.c_str(), before, as.charge);
                        landed = true;
                    }
                }
            break;

        case EffectType::Dispel: {
            if (e.dispelKind == "buff" && tgt.side != src.side) {
                if (e.canEvade && tryEvade(src, tgt, e.apply, c.ability)) break;
                if (e.canResist && tryResist(src, tgt, nullptr)) break;
            }
            landed = dispel(tid, e.dispelKind, e.count) > 0;
            break;
        }

        case EffectType::Revive:
            if (!tgt.alive && !tgt.cannotRevive) {
                tgt.alive = true;
                tgt.health = std::max(1.0, std::floor(maxHealth(tgt) * (e.amount > 0 ? e.amount : 1.0)));
                tgt.protection = 0.0;
                tgt.tm = 0.0;
                landed = true;
                LOG("    %s is revived with %s health", tgt.name.c_str(), num(tgt.health).c_str());
                emit(VisKind::Revive, tgt.id, -1, tgt.health);
            }
            break;

        case EffectType::BonusMove:
            if (tgt.alive) {
                tgt.bonusTurn = true;
                landed = true;
                LOG("    %s will take a bonus turn", tgt.name.c_str());
            }
            break;

        case EffectType::Assist: {
            if (hasFlag(tgt, SF_NoAssist)) break;
            int victim = c.selected >= 0 ? c.selected : c.evOther;
            if (victim < 0 || !units[static_cast<size_t>(victim)].alive || !isEnemy(tid, victim)) {
                // no sensible target in the event (e.g. an area ability): attack a random living enemy
                std::vector<int> foes;
                for (const BattleUnit& o : units)
                    if (o.alive && o.side != tgt.side) foes.push_back(o.id);
                victim = foes.empty() ? -1 : foes[static_cast<size_t>(randInt(static_cast<int>(foes.size())))];
            }
            if (victim >= 0) {
                assists_.push_back({tid, victim, e.multiplier, false});
                landed = true;
            }
            break;
        }

        case EffectType::Counter:
            if (!c.isCounter && c.evOther >= 0 && units[static_cast<size_t>(c.evOther)].alive &&
                isEnemy(c.source, c.evOther) && !hasFlag(src, SF_NoCounter)) {
                counters_.push_back({c.source, c.evOther, 1.0, true});
                landed = true;
            }
            break;

        case EffectType::UseAbility: {
            std::size_t ai = src.abilities.size();
            if (e.abilityRef.empty() || e.abilityRef == "basic") ai = static_cast<std::size_t>(src.basicIdx);
            else
                for (std::size_t k = 0; k < src.abilities.size(); ++k)
                    if (src.abilities[k].def->id == e.abilityRef) ai = k;
            if (ai >= src.abilities.size() || !tgt.alive || useDepth_ >= 3) break;
            ++useDepth_;
            useAbility(src, ai, tid, e.outOfTurn, e.multiplier, c.isCounter);
            --useDepth_;
            landed = true;
            break;
        }

        case EffectType::Defeat:
            if (!tgt.alive) break;
            if (hasFlag(tgt, SF_InstantDefeatImmune)) {
                LOG("    %s cannot be instantly defeated", tgt.name.c_str());
                break;
            }
            LOG("    %s is instantly defeated", tgt.name.c_str());
            tgt.health = 0.0;
            landed = true;
            onLethal(tgt, src.id, true);
            break;

        case EffectType::RemoveStatus:
            for (std::size_t k = 0; k < tgt.statuses.size(); ++k) {
                StatusInst& si = tgt.statuses[k];
                if (si.def->id != e.statusId) continue;
                if (e.count > 0 && si.stacks > e.count) {
                    si.stacks -= e.count;
                    LOG("    %s loses %d stack(s) of %s", tgt.name.c_str(), e.count, si.def->name.c_str());
                } else {
                    removeStatusIndices(tgt, {k});
                }
                landed = true;
                break;
            }
            break;
        }

        if (landed && !e.onHit.empty()) {
            const int saved = c.hitTarget;
            c.hitTarget = tid;
            execEffects(e.onHit, c);
            c.hitTarget = saved;
        }
    }
}

// ============================================================ events =========

bool Battle::scopeMatches(const BattleUnit& owner, Scope scope, const EventData& ev) const {
    if (scope == Scope::Any) return true;
    if (ev.subject < 0) return false;
    const BattleUnit& s = units[static_cast<size_t>(ev.subject)];
    switch (scope) {
    case Scope::Self: return s.id == owner.id;
    case Scope::Ally: return s.id != owner.id && s.side == owner.side;
    case Scope::AnyAlly: return s.side == owner.side;
    case Scope::Enemy: return s.side != owner.side;
    default: return true;
    }
}

bool Battle::triggerMatches(const BattleUnit& owner, const TriggerDef& tr, const EventData& ev) const {
    if (tr.on != ev.type) return false;
    if (!owner.alive && !(ev.type == EventType::Death && ev.subject == owner.id)) return false;
    if (!scopeMatches(owner, tr.scope, ev)) return false;
    if (tr.requireInTurn && !ev.inTurn) return false;
    if (tr.requireOutOfTurn && ev.inTurn) return false;
    if (!tr.tags.empty()) {
        if (ev.subject < 0) return false;
        bool ok = false;
        for (const std::string& tag : tr.tags)
            if (units[static_cast<size_t>(ev.subject)].def->hasTag(tag)) { ok = true; break; }
        if (!ok) return false;
    }
    if (!tr.abilityKinds.empty()) {
        if (!ev.ability) return false;
        if (std::find(tr.abilityKinds.begin(), tr.abilityKinds.end(), ev.ability->kind) == tr.abilityKinds.end())
            return false;
    }
    if (tr.statusKind >= 0 && (!ev.status || static_cast<int>(ev.status->kind) != tr.statusKind)) return false;
    if (!tr.statusId.empty() && (!ev.status || ev.status->id != tr.statusId)) return false;
    if (tr.healthBelow >= 0 && ev.subject >= 0 && healthFrac(units[static_cast<size_t>(ev.subject)]) > tr.healthBelow)
        return false;
    return true;
}

void Battle::fire(const EventData& ev) {
    if (depth_ >= rules.maxEventDepth) return;
    ++depth_;

    auto run = [&](BattleUnit& owner, const TriggerDef& tr) {
        EffectCtx c;
        c.source = owner.id;
        c.selected = ev.other;
        c.evSubject = ev.subject;
        c.evOther = ev.other;
        c.ability = ev.ability;
        c.outOfTurn = !ev.inTurn;
        c.fromTrigger = true;
        c.evAmount = ev.amount;
        execEffects(tr.effects, c);
    };

    // 1. triggers written on abilities (leader / unique passives)
    for (std::size_t ui = 0; ui < units.size(); ++ui) {
        BattleUnit& owner = units[ui];
        for (std::size_t ai = 0; ai < owner.abilities.size(); ++ai) {
            const AbilityDef& ab = *owner.abilities[ai].def;
            if (ab.triggers.empty()) continue;
            if (ab.kind == AbilityKind::Leader && !owner.isLeader) continue;
            for (std::size_t ti = 0; ti < ab.triggers.size(); ++ti) {
                const TriggerDef& tr = ab.triggers[ti];
                if (!triggerMatches(owner, tr, ev)) continue;
                const std::size_t k = owner.trigBase[ai] + ti;
                if (tr.oncePerTurn && owner.lastFired[k] == turnSeq) continue;
                if (tr.limit > 0 && owner.fireCount[k] >= tr.limit) continue;
                if (tr.chance < 1.0 && rand01() >= tr.chance) continue;
                owner.lastFired[k] = turnSeq;
                owner.fireCount[k]++;
                run(owner, tr);
            }
        }
    }

    // 2. triggers carried by status effects. Decide first (reactions may add or remove statuses), then run.
    struct Pending {
        int owner;
        const TriggerDef* tr;
    };
    std::vector<Pending> pending;
    for (std::size_t ui = 0; ui < units.size(); ++ui) {
        BattleUnit& owner = units[ui];
        for (StatusInst& si : owner.statuses) {
            const StatusDef& d = *si.def;
            for (std::size_t ti = 0; ti < d.triggers.size(); ++ti) {
                const TriggerDef& tr = d.triggers[ti];
                if (!triggerMatches(owner, tr, ev)) continue;
                if (tr.oncePerTurn && si.lastFired[ti] == turnSeq) continue;
                if (tr.limit > 0 && si.fireCount[ti] >= tr.limit) continue;
                if (tr.chance < 1.0 && rand01() >= tr.chance) continue;
                si.lastFired[ti] = turnSeq;
                si.fireCount[ti]++;
                pending.push_back({owner.id, &tr});
            }
        }
    }
    for (const Pending& p : pending) run(units[static_cast<size_t>(p.owner)], *p.tr);
    --depth_;
}

// ============================================================ turn flow ======

int Battle::nextActor() {
    while (!bonusQueue_.empty()) {
        const int id = bonusQueue_.front();
        bonusQueue_.pop_front();
        if (units[static_cast<size_t>(id)].alive) return id;
    }
    const double eps = 1e-9;
    for (int guard = 0; guard < 1000000; ++guard) {
        // Anyone at/over 100%? The highest meter goes first (overflow matters); exact ties are random.
        double best = -1.0;
        std::vector<int> ties;
        for (const BattleUnit& u : units) {
            if (!u.alive || u.tm < 1.0 - eps) continue;
            if (u.tm > best + eps) { best = u.tm; ties.assign(1, u.id); }
            else if (std::fabs(u.tm - best) <= eps) ties.push_back(u.id);
        }
        if (!ties.empty()) return ties.size() == 1 ? ties[0] : ties[static_cast<size_t>(randInt(static_cast<int>(ties.size())))];

        // Otherwise advance time until the first unit reaches 100%. Natural fill never overflows.
        double dt = std::numeric_limits<double>::infinity();
        for (const BattleUnit& u : units) {
            if (!u.alive) continue;
            const double sp = stat(u, Stat::Speed);
            if (sp > 0.0) dt = std::min(dt, (1.0 - u.tm) / sp);
        }
        if (!std::isfinite(dt)) return -1;
        for (BattleUnit& u : units) {
            if (!u.alive) continue;
            const double sp = stat(u, Stat::Speed);
            if (sp <= 0.0) continue;
            u.tm = std::min(1.0, u.tm + sp * dt);
            if (u.tm >= 1.0 - eps) u.tm = 1.0;
        }
    }
    return -1;
}

void Battle::tickStartOfTurn(BattleUnit& u) {
    struct Tick {
        AmountMode mode;
        double amount;
        int stacks;
        std::uint32_t flags;
        int source;
        const StatusDef* def;
    };
    std::vector<Tick> ticks;
    for (const StatusInst& si : u.statuses)
        if (si.def->mode == AmountMode::Dot || si.def->mode == AmountMode::Hot || si.def->mode == AmountMode::ProtOverTime)
            ticks.push_back({si.def->mode, si.amount, si.stacks, si.def->flags, si.source, si.def});

    // Documented order: heal-over-time first, then damage-over-time.
    for (const Tick& t : ticks) {
        if (!u.alive) return;
        if (t.mode == AmountMode::Dot) continue;
        const double v = std::floor(t.amount * maxHealth(u) * t.stacks);
        healUnit(u, t.mode == AmountMode::Hot ? v : 0.0, t.mode == AmountMode::ProtOverTime ? v : 0.0, t.source);
    }
    for (const Tick& t : ticks) {
        if (!u.alive) return;
        if (t.mode != AmountMode::Dot) continue;
        double v = std::floor(t.amount * maxHealth(u) * t.stacks);
        if (hasFlag(u, SF_DamageImmunity)) v = 0.0;
        const DamageOutcome o = applyDamage(u, v, (t.flags & SF_IgnoreProtection) != 0);
        const double total = o.prot + o.hp;
        if (t.source >= 0) units[static_cast<size_t>(t.source)].st.damageDealt += static_cast<long long>(total);
        u.st.damageTaken += static_cast<long long>(total);
        LOG("    %s takes %s damage from %s", u.name.c_str(), num(total).c_str(), t.def->name.c_str());
        emit(VisKind::Damage, u.id, t.source, o.prot, o.hp, false, t.def->name);
        if (o.lethal) onLethal(u, t.source);
    }
}

void Battle::tickDurations(BattleUnit& u) {
    for (int pass = 0; pass < 2; ++pass) { // buffs first, then debuffs
        std::vector<std::size_t> expired;
        for (std::size_t i = 0; i < u.statuses.size(); ++i) {
            StatusInst& si = u.statuses[i];
            const bool isDebuff = si.def->kind == StatusKind::Debuff;
            if ((pass == 0) == isDebuff) continue;
            if (!rules.statusesTickOnApplyTurn && si.appliedSeq == turnSeq) continue;
            if (si.remaining > 0 && --si.remaining == 0) expired.push_back(i);
        }
        removeStatusIndices(u, expired);
    }
}

void Battle::tickCooldowns(BattleUnit& u) {
    for (AbilityState& as : u.abilities) {
        if (as.cooldown <= 0) continue;
        if (!rules.cooldownCountsUseTurn && as.usedSeq == turnSeq) continue;
        --as.cooldown;
    }
}

void Battle::useAbility(BattleUnit& u, std::size_t ai, int target, bool outOfTurn, double scale, bool isCounter) {
    AbilityState& as = u.abilities[ai];
    const AbilityDef& ab = *as.def;
    if (!outOfTurn) {
        as.cooldown = ab.cooldown;
        as.usedSeq = turnSeq;
        if (ab.kind == AbilityKind::Ultimate) as.charge = 0;
    }
    if (target >= 0 && target != u.id) LOG("  %s uses %s on %s", u.name.c_str(), ab.name.c_str(), units[static_cast<size_t>(target)].name.c_str());
    else LOG("  %s uses %s", u.name.c_str(), ab.name.c_str());
    emit(VisKind::UseAbility, u.id, target, 0.0, 0.0, outOfTurn, ab.name);

    EventData used;
    used.type = EventType::UsedAbility;
    used.subject = u.id;
    used.other = target;
    used.ability = &ab;
    used.inTurn = !outOfTurn;
    fire(used);

    EffectCtx c;
    c.source = u.id;
    c.selected = target;
    c.ability = &ab;
    c.outOfTurn = outOfTurn;
    c.isCounter = isCounter;
    c.scale = scale;
    execEffects(ab.effects, c);
    if (c.usedAdvantage) consumeFlagStatus(u, SF_Advantage);

    if (c.dealtDamage) {
        EventData atk;
        atk.type = EventType::Attack;
        atk.subject = u.id;
        atk.other = c.firstHit;
        atk.amount = c.totalDamage;
        atk.ability = &ab;
        atk.inTurn = !outOfTurn;
        fire(atk);
    }
}

void Battle::performDecision(BattleUnit& u, const Decision& d) {
    std::size_t ai = d.ability >= 0 && d.ability < static_cast<int>(u.abilities.size()) ? static_cast<std::size_t>(d.ability)
                                                                                         : static_cast<std::size_t>(u.basicIdx);
    if (!abilityUsable(u, ai)) ai = static_cast<std::size_t>(u.basicIdx);
    const AbilityDef& ab = *u.abilities[ai].def;
    int target = -1;
    const std::vector<int> valid = validTargets(u, ab);
    if (ab.pick == Pick::Self) target = u.id;
    else if (!valid.empty()) target = contains(valid, d.target) ? d.target : valid[static_cast<size_t>(randInt(static_cast<int>(valid.size())))];
    useAbility(u, ai, target, false, 1.0, false);
}

void Battle::processPending() {
    while (!assists_.empty() || !counters_.empty()) {
        if (over() || pendingBudget_ <= 0) break;
        --pendingBudget_;
        Pending p;
        if (!assists_.empty()) { p = assists_.front(); assists_.pop_front(); }
        else { p = counters_.front(); counters_.pop_front(); }
        BattleUnit& a = units[static_cast<size_t>(p.unit)];
        if (!a.alive) continue;
        if (p.isCounter ? hasFlag(a, SF_NoCounter) : hasFlag(a, SF_NoAssist)) continue;
        if (p.target < 0 || !units[static_cast<size_t>(p.target)].alive || !isEnemy(p.unit, p.target)) continue;
        LOG("  %s %s", a.name.c_str(), p.isCounter ? "counterattacks!" : "assists!");
        emit(VisKind::Reaction, a.id, p.target, 0.0, 0.0, p.isCounter);
        useAbility(a, static_cast<std::size_t>(a.basicIdx), p.target, true, p.scale, p.isCounter);
    }
    assists_.clear();
    counters_.clear();
}

void Battle::takeTurn(int actorId, Controller& ctrl) {
    BattleUnit& u = units[static_cast<size_t>(actorId)];
    ++turnSeq;
    ++turnsTaken;
    actorNow_ = actorId;
    u.st.turns++;
    u.tm = 0.0;
    pendingBudget_ = rules.maxPendingPerTurn;
    assists_.clear();
    counters_.clear();
    LOG("--- Turn %d: %s ---", turnsTaken, u.name.c_str());
    emit(VisKind::TurnStart, u.id);

    tickStartOfTurn(u); // heal-over-time, then damage-over-time
    if (u.alive) {
        EventData ev;
        ev.type = EventType::StartOfTurn;
        ev.subject = u.id;
        fire(ev);
    }
    if (u.alive && !over()) {
        if (hasFlag(u, SF_Stun)) {
            LOG("  %s is stunned and loses the turn", u.name.c_str());
            emit(VisKind::Stunned, u.id);
        } else {
            const Decision d = ctrl.decide(*this, u);
            performDecision(u, d);
        }
    }
    processPending(); // assists first, then counters
    if (u.alive) {
        EventData ev;
        ev.type = EventType::EndOfTurn;
        ev.subject = u.id;
        fire(ev);
        tickDurations(u);
        tickCooldowns(u);
    }
    for (BattleUnit& o : units)
        if (o.bonusTurn && o.alive) {
            o.bonusTurn = false;
            bonusQueue_.push_back(o.id);
        }
    emit(VisKind::TurnEnd, u.id);
    actorNow_ = -1;
}

bool Battle::step(Controller& c0, Controller& c1) {
    if (over()) return false;
    if (turnsTaken >= rules.maxTurns) {
        timedOut = true;
        return false;
    }
    const int id = nextActor();
    if (id < 0) {
        timedOut = true;
        return false;
    }
    takeTurn(id, units[static_cast<size_t>(id)].side == 0 ? c0 : c1);
    return !over();
}

void Battle::run(Controller& c0, Controller& c1) {
    while (step(c0, c1)) {
    }
}
