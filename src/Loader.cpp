#include "Defs.h"
#include "Json.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace {

[[noreturn]] void bad(const std::string& where, const std::string& msg) {
    throw std::runtime_error(where + ": " + msg);
}

template <typename T>
T pickEnum(const std::string& where, const std::string& what, const std::string& s,
           const std::vector<std::pair<const char*, T>>& table) {
    for (const auto& p : table)
        if (s == p.first) return p.second;
    std::string opts;
    for (const auto& p : table) opts += std::string(opts.empty() ? "" : ", ") + p.first;
    bad(where, "unknown " + what + " '" + s + "' (expected one of: " + opts + ")");
}

EffectType parseEffectType(const std::string& w, const std::string& s) {
    return pickEnum<EffectType>(w, "effect type", s,
        {{"damage", EffectType::Damage},
         {"heal", EffectType::Heal},
         {"apply_status", EffectType::ApplyStatus},
         {"modify_turn_meter", EffectType::ModifyTurnMeter},
         {"modify_cooldown", EffectType::ModifyCooldown},
         {"modify_ultimate", EffectType::ModifyUltimate},
         {"dispel", EffectType::Dispel},
         {"revive", EffectType::Revive},
         {"bonus_move", EffectType::BonusMove},
         {"assist", EffectType::Assist},
         {"counter", EffectType::Counter},
         {"use_ability", EffectType::UseAbility},
         {"defeat", EffectType::Defeat},
         {"remove_status", EffectType::RemoveStatus}});
}

DamageType parseDamageType(const std::string& w, const std::string& s) {
    return pickEnum<DamageType>(w, "damage_type", s,
        {{"attack_damage", DamageType::AttackDamage},
         {"ability_power", DamageType::AbilityPower},
         {"unmitigable", DamageType::Unmitigable}});
}

ApplyType parseApply(const std::string& w, const std::string& s) {
    return pickEnum<ApplyType>(w, "apply type", s,
        {{"unavoidable", ApplyType::Unavoidable},
         {"physical", ApplyType::Physical},
         {"mental", ApplyType::Mental}});
}

EventType parseEvent(const std::string& w, const std::string& s) {
    return pickEnum<EventType>(w, "event", s,
        {{"start_of_battle", EventType::StartOfBattle},
         {"start_of_turn", EventType::StartOfTurn},
         {"end_of_turn", EventType::EndOfTurn},
         {"used_ability", EventType::UsedAbility},
         {"attack", EventType::Attack},
         {"receive_damage", EventType::ReceiveDamage},
         {"crit", EventType::Crit},
         {"receive_crit", EventType::ReceiveCrit},
         {"dodged", EventType::Dodged},
         {"was_dodged", EventType::WasDodged},
         {"kill", EventType::Kill},
         {"death", EventType::Death},
         {"status_added", EventType::StatusAdded},
         {"status_removed", EventType::StatusRemoved},
         {"resisted", EventType::Resisted},
         {"was_resisted", EventType::WasResisted},
         {"receive_heal", EventType::ReceiveHeal}});
}

Scope parseScope(const std::string& w, const std::string& s) {
    return pickEnum<Scope>(w, "scope", s,
        {{"self", Scope::Self}, {"ally", Scope::Ally}, {"any_ally", Scope::AnyAlly},
         {"enemy", Scope::Enemy}, {"any", Scope::Any}});
}

AbilityKind parseKind(const std::string& w, const std::string& s) {
    return pickEnum<AbilityKind>(w, "ability kind", s,
        {{"basic", AbilityKind::Basic}, {"special", AbilityKind::Special}, {"leader", AbilityKind::Leader},
         {"unique", AbilityKind::Unique}, {"ultimate", AbilityKind::Ultimate}});
}

Pick parsePick(const std::string& w, const std::string& s) {
    return pickEnum<Pick>(w, "pick", s,
        {{"enemy", Pick::Enemy}, {"ally", Pick::Ally}, {"dead_ally", Pick::DeadAlly},
         {"self", Pick::Self}, {"none", Pick::None}});
}

StatusKind parseStatusKind(const std::string& w, const std::string& s) {
    return pickEnum<StatusKind>(w, "status kind", s,
        {{"buff", StatusKind::Buff}, {"debuff", StatusKind::Debuff}, {"neutral", StatusKind::Neutral}});
}

std::uint32_t parseFlag(const std::string& w, const std::string& s) {
    return pickEnum<std::uint32_t>(w, "status flag", s,
        {{"taunt", SF_Taunt}, {"stealth", SF_Stealth}, {"stun", SF_Stun}, {"ability_block", SF_AbilityBlock},
         {"no_assist", SF_NoAssist}, {"no_counter", SF_NoCounter}, {"no_tm_gain", SF_NoTMGain},
         {"healing_immunity", SF_HealingImmunity}, {"damage_immunity", SF_DamageImmunity},
         {"crit_immunity", SF_CritImmunity}, {"debuff_immunity", SF_DebuffImmunity},
         {"buff_immunity", SF_BuffImmunity}, {"ignore_protection", SF_IgnoreProtection},
         {"no_evade", SF_NoEvade}, {"foresight", SF_Foresight}, {"advantage", SF_Advantage}, {"blind", SF_Blind},
         {"vulnerable", SF_Vulnerable}, {"speed_zero", SF_SpeedZero}, {"no_revive", SF_NoRevive},
         {"instant_defeat_immune", SF_InstantDefeatImmune}, {"protection_disabled", SF_ProtectionDisabled},
         {"damage_to_one", SF_DamageToOne}, {"tm_immune", SF_TMImmune}, {"no_buffs_off_turn", SF_NoBuffsOffTurn},
         {"ignore_taunt", SF_IgnoreTaunt}, {"uncounterable", SF_Uncounterable}, {"riposte", SF_RiposteIgnoreProt},
         {"no_protection_recovery", SF_NoProtectionRecovery}, {"daze", SF_Daze}});
}

AmountMode parseMode(const std::string& w, const std::string& s) {
    return pickEnum<AmountMode>(w, "status mode", s,
        {{"none", AmountMode::None}, {"dot", AmountMode::Dot}, {"hot", AmountMode::Hot},
         {"prot_over_time", AmountMode::ProtOverTime}, {"bonus_protection", AmountMode::BonusProtection}});
}

HealthState parseHealthState(const std::string& w, const std::string& s) {
    return pickEnum<HealthState>(w, "health state", s,
        {{"alive", HealthState::Alive}, {"dead", HealthState::Dead}, {"any", HealthState::Any}});
}

// Shorthand target strings -> spec.
TargetSpec shorthandTarget(const std::string& w, const std::string& s) {
    TargetSpec t;
    if (s == "selected") { t.select = Select::Selected; t.side = Side::Enemy; }
    else if (s == "selected_ally") { t.select = Select::Selected; t.side = Side::Ally; }
    else if (s == "selected_dead_ally") { t.select = Select::Selected; t.side = Side::Ally; t.health = HealthState::Dead; }
    else if (s == "all_enemies") { t.select = Select::All; t.side = Side::Enemy; }
    else if (s == "all_allies") { t.select = Select::All; t.side = Side::Ally; }
    else if (s == "other_allies") { t.select = Select::All; t.side = Side::Ally; t.excludeSelf = true; }
    else if (s == "self") { t.select = Select::Self; t.side = Side::Ally; }
    else if (s == "random_enemy") { t.select = Select::Random; t.side = Side::Enemy; }
    else if (s == "random_ally") { t.select = Select::Random; t.side = Side::Ally; }
    else if (s == "lowest_ally") { t.select = Select::LowestSurvivability; t.side = Side::Ally; }
    else if (s == "lowest_enemy") { t.select = Select::LowestSurvivability; t.side = Side::Enemy; }
    else if (s == "highest_enemy") { t.select = Select::HighestSurvivability; t.side = Side::Enemy; }
    else if (s == "hit_target") { t.select = Select::HitTarget; t.side = Side::Both; t.health = HealthState::Any; }
    else if (s == "event_subject") { t.select = Select::EventSubject; t.side = Side::Both; t.health = HealthState::Any; }
    else if (s == "event_other") { t.select = Select::EventOther; t.side = Side::Both; t.health = HealthState::Any; }
    else if (s == "leader_ally") { t.select = Select::Leader; t.side = Side::Ally; }
    else bad(w, "unknown target shorthand '" + s + "'");
    return t;
}

TargetSpec parseTarget(const std::string& w, const json::Value& v) {
    if (v.isStr()) return shorthandTarget(w, v.s);
    if (!v.isObj()) bad(w, "target must be a string or an object");
    TargetSpec t;
    std::string sel = v.str("select", "selected");
    t.select = pickEnum<Select>(w, "select", sel,
        {{"selected", Select::Selected}, {"all", Select::All}, {"self", Select::Self},
         {"random", Select::Random}, {"lowest_survivability", Select::LowestSurvivability},
         {"highest_survivability", Select::HighestSurvivability}, {"leader", Select::Leader},
         {"event_subject", Select::EventSubject}, {"event_other", Select::EventOther},
         {"hit_target", Select::HitTarget}});
    t.side = pickEnum<Side>(w, "side", v.str("side", "enemy"),
        {{"enemy", Side::Enemy}, {"ally", Side::Ally}, {"both", Side::Both}});
    t.anyTags = v.strArray("any_tags");
    t.noTags = v.strArray("no_tags");
    t.health = parseHealthState(w, v.str("health", "alive"));
    t.excludeSelf = v.boolean("exclude_self", false);
    t.excludeSelected = v.boolean("exclude_selected", false);
    t.hasStatus = v.str("has_status");
    t.lacksStatus = v.str("lacks_status");
    t.count = v.integer("count", 1);
    t.healthBelow = v.num("health_below", -1.0);
    t.healthAbove = v.num("health_above", -1.0);
    return t;
}

StatMod parseMod(const std::string& w, const json::Value& v, std::vector<StatMod>& out) {
    std::string key = v.str("stat");
    std::vector<Stat> stats;
    if (!statsFromKey(key, stats)) bad(w, "unknown stat '" + key + "'");
    ModOp op = pickEnum<ModOp>(w, "mod op", v.str("op", "add"),
        {{"add", ModOp::Add}, {"pct_base", ModOp::PctBase}});
    StatMod last;
    for (Stat s : stats) {
        StatMod m;
        m.stat = s;
        m.op = op;
        m.value = v.num("value", 0.0);
        out.push_back(m);
        last = m;
    }
    return last;
}

EffectDef parseEffect(const std::string& w, const json::Value& v, bool nested, const Content& c) {
    if (!v.isObj()) bad(w, "effect must be an object");
    EffectDef e;
    e.type = parseEffectType(w, v.str("type"));

    // damage defaults
    e.damageType = parseDamageType(w, v.str("damage_type", "attack_damage"));
    if (v.has("apply")) e.apply = parseApply(w, v.str("apply"));
    else if (e.type == EffectType::Damage) {
        e.apply = e.damageType == DamageType::AttackDamage ? ApplyType::Physical
                  : e.damageType == DamageType::AbilityPower ? ApplyType::Mental
                                                             : ApplyType::Unavoidable;
    }
    e.chance = v.num("chance", 1.0);
    e.basis = v.str("basis");
    e.multiplier = v.num("multiplier", 1.0);
    e.additive = v.num("additive", 0.0);
    e.variance = v.num("variance", -1.0);
    e.hits = v.integer("hits", 1);
    e.canCrit = v.boolean("can_crit", e.damageType != DamageType::Unmitigable);
    e.forceCrit = v.boolean("force_crit", false);
    e.canResist = v.boolean("can_resist", true);
    e.canEvade = v.boolean("can_evade", true);
    e.protectionMultiplier = v.num("protection_multiplier", 0.0);
    e.statusId = v.str("status");
    e.duration = v.integer("duration", -999);
    e.stacks = v.integer("stacks", 1);
    e.amount = v.num("amount", 0.0);
    e.abilityRef = v.str("ability");
    e.dispelKind = v.str("dispel");
    e.count = v.integer("count", 0);
    e.ifTargetHasStatus = v.str("if_target_has_status");
    e.ifTargetLacksStatus = v.str("if_target_lacks_status");
    e.ifSourceHasStatus = v.str("if_source_has_status");
    e.ifSourceLacksStatus = v.str("if_source_lacks_status");
    e.ifTargetHealthBelow = v.num("if_target_health_below", -1.0);
    e.noReactions = v.boolean("no_reactions", false);
    e.ignoreProtection = v.boolean("ignore_protection", false);
    e.cannotDefeat = v.boolean("cannot_defeat", false);
    e.scaleByStatus = v.str("scale_by_status");
    e.outOfTurn = v.boolean("out_of_turn", false);

    // target (explicit or defaulted by effect type)
    if (v.has("target")) {
        e.target = parseTarget(w, *v.find("target"));
    } else if (nested) {
        e.target = shorthandTarget(w, "hit_target");
    } else {
        switch (e.type) {
        case EffectType::Damage: e.target = shorthandTarget(w, "selected"); break;
        case EffectType::ApplyStatus: {
            auto it = c.statuses.find(e.statusId);
            bool debuff = it != c.statuses.end() && it->second.kind == StatusKind::Debuff;
            e.target = shorthandTarget(w, debuff ? "selected" : "self");
            break;
        }
        case EffectType::Dispel: e.target = shorthandTarget(w, e.dispelKind == "debuff" ? "self" : "selected"); break;
        case EffectType::Heal: e.target = shorthandTarget(w, "selected_ally"); break;
        case EffectType::Revive: e.target = shorthandTarget(w, "selected_dead_ally"); break;
        case EffectType::Defeat:
        case EffectType::UseAbility: e.target = shorthandTarget(w, "selected"); break;
        default: e.target = shorthandTarget(w, "self"); break;
        }
    }

    static const std::set<std::string> bases = {
        "", "source_max_health", "target_max_health", "source_health", "target_health",
        "target_missing_health", "source_physical_damage", "source_special_damage", "target_protection",
        "event_amount"};
    if (!bases.count(e.basis)) bad(w, "unknown basis '" + e.basis + "'");
    if (e.chance < 0.0 || e.chance > 1.0) bad(w, "chance must be between 0 and 1");
    if (e.hits < 1) bad(w, "hits must be >= 1");
    if (e.type == EffectType::ApplyStatus && e.statusId.empty()) bad(w, "apply_status requires 'status'");
    if (e.type == EffectType::RemoveStatus && e.statusId.empty()) bad(w, "remove_status requires 'status'");
    if (e.type == EffectType::Dispel && e.dispelKind != "buff" && e.dispelKind != "debuff" && e.dispelKind != "all")
        bad(w, "dispel requires \"dispel\": \"buff\" | \"debuff\" | \"all\"");

    for (const json::Value& h : v.arr("on_hit"))
        e.onHit.push_back(parseEffect(w + " > on_hit", h, true, c));
    return e;
}

TriggerDef parseTrigger(const std::string& w, const json::Value& v, const Content& c) {
    TriggerDef t;
    t.on = parseEvent(w, v.str("on"));
    t.scope = parseScope(w, v.str("scope", "self"));
    t.tags = v.strArray("tags");
    for (const std::string& k : v.strArray("ability_kind")) t.abilityKinds.push_back(parseKind(w, k));
    if (v.has("status_kind")) t.statusKind = (int)parseStatusKind(w, v.str("status_kind"));
    t.statusId = v.str("status");
    t.chance = v.num("chance", 1.0);
    t.oncePerTurn = v.boolean("once_per_turn", false);
    t.limit = v.integer("limit", 0);
    t.healthBelow = v.num("health_below", -1.0);
    t.requireOutOfTurn = v.boolean("out_of_turn", false);
    t.requireInTurn = v.boolean("in_turn", false);
    for (const json::Value& e : v.arr("effects")) t.effects.push_back(parseEffect(w + " > trigger effect", e, false, c));
    if (t.effects.empty()) bad(w, "trigger has no effects");
    return t;
}

void parseDecisionNode(const std::string& w, const json::Value& v, DecisionNode& n) {
    n.type = v.str("type");
    n.group = v.str("group", "enemy");
    n.modifier = v.str("modifier", "greater_than");
    n.param = v.num("param", 0.0);
    n.paramStr = v.str("param_str");
    n.unitIds = v.strArray("unit_ids");
    n.chance = v.num("chance", 1.0);
    n.negate = v.boolean("negate", false);
    static const std::set<std::string> types = {
        "single_health", "median_health", "number_of_targets", "persistent_effect", "number_of_enemies_alive",
        "outnumbered", "single_survivability", "single_protection", "single_speed_fill", "target_by_unit_ids",
        "reinforce_available", "summon_available"};
    if (!types.count(n.type)) bad(w, "unknown decision node type '" + n.type + "'");
    if (n.group != "self" && n.group != "ally" && n.group != "enemy" && n.group != "dead_ally")
        bad(w, "unknown decision group '" + n.group + "'");
    if (n.modifier != "greater_than" && n.modifier != "less_than" && n.modifier != "equal_to")
        bad(w, "unknown decision modifier '" + n.modifier + "'");
}

struct EffectShape {
    bool damage = false, special = false, enemyAll = false, enemySingle = false, allyOnly = true, anyAlly = false,
         selfOnly = true;
};

void scanShape(const std::vector<EffectDef>& list, EffectShape& s) {
    for (const EffectDef& e : list) {
        const bool all = e.target.select == Select::All || (e.target.select == Select::Random && e.target.count > 1);
        if (e.type == EffectType::Damage) {
            s.damage = true;
            if (e.damageType == DamageType::AbilityPower) s.special = true;
        }
        if (e.target.side == Side::Enemy) {
            s.allyOnly = false;
            if (all) s.enemyAll = true;
            else s.enemySingle = true;
        } else if (e.target.side == Side::Ally) {
            s.anyAlly = true;
            if (e.target.select != Select::Self) s.selfOnly = false;
        }
        scanShape(e.onHit, s);
    }
}

// How the move should be staged when the data doesn't say.
std::string inferTemplate(const AbilityDef& a) {
    if (a.kind == AbilityKind::Leader || a.kind == AbilityKind::Unique) return "none";
    EffectShape s;
    scanShape(a.effects, s);
    if (s.enemyAll) return "area";
    if (s.damage) return s.special ? "ranged" : "melee";
    if (s.enemySingle) return "ranged";
    if (s.anyAlly) return s.selfOnly ? "self" : "support";
    return "ranged";
}

bool validTemplate(const std::string& t) {
    return t == "melee" || t == "ranged" || t == "area" || t == "self" || t == "support" || t == "none";
}

AbilityDef parseAbility(const std::string& w, const json::Value& v, const Content& c) {
    AbilityDef a;
    a.id = v.str("id");
    if (a.id.empty()) bad(w, "ability needs an id");
    a.name = v.str("name", a.id);
    a.description = v.str("description");
    a.kind = parseKind(w, v.str("kind", "basic"));
    bool passive = a.kind == AbilityKind::Leader || a.kind == AbilityKind::Unique;
    a.pick = parsePick(w, v.str("pick", passive ? "none" : "enemy"));
    a.cooldown = v.integer("cooldown", 0);
    a.ultimateCharge = v.integer("ultimate_charge", a.kind == AbilityKind::Ultimate ? 100 : 0);
    for (const json::Value& e : v.arr("effects")) a.effects.push_back(parseEffect(w + "/" + a.id, e, false, c));
    for (const json::Value& t : v.arr("triggers")) a.triggers.push_back(parseTrigger(w + "/" + a.id, t, c));
    for (const json::Value& au : v.arr("auras")) {
        AuraDef d;
        d.target = au.has("target") ? parseTarget(w + "/" + a.id, *au.find("target")) : shorthandTarget(w, "self");
        d.statusId = au.str("status");
        d.duration = au.integer("duration", -1);
        d.amount = au.num("amount", 0.0);
        a.auras.push_back(d);
    }
    if (const json::Value* ai = v.find("ai")) {
        a.ai.enemyRules = ai->strArray("enemy_rules");
        a.ai.allyRules = ai->strArray("ally_rules");
        for (const json::Value& n : ai->arr("nodes")) {
            DecisionNode node;
            parseDecisionNode(w + "/" + a.id, n, node);
            a.ai.nodes.push_back(node);
        }
    }
    if (!passive && a.effects.empty()) bad(w + "/" + a.id, "active ability has no effects");

    // presentation hints (optional; derived when absent)
    const json::Value* pr = v.find("presentation");
    a.presentation.tmpl = pr ? pr->str("template", inferTemplate(a)) : inferTemplate(a);
    a.presentation.anim = pr ? pr->str("anim", a.id) : a.id;
    a.presentation.vfx = pr ? pr->str("vfx") : std::string();
    a.presentation.sfx = pr ? pr->str("sfx") : std::string();
    if (!validTemplate(a.presentation.tmpl))
        bad(w + "/" + a.id, "unknown presentation template '" + a.presentation.tmpl +
                                "' (expected melee, ranged, area, self, support or none)");
    return a;
}

// ---- validation ----

void checkStatusRef(const Content& c, const std::string& where, const std::string& id) {
    if (!id.empty() && !c.statuses.count(id)) bad(where, "references unknown status '" + id + "'");
}

void checkTarget(const Content& c, const std::string& where, const TargetSpec& t) {
    checkStatusRef(c, where, t.hasStatus);
    checkStatusRef(c, where, t.lacksStatus);
}

void checkEffect(const Content& c, const std::string& where, const EffectDef& e) {
    checkTarget(c, where, e.target);
    if (e.type == EffectType::ApplyStatus || e.type == EffectType::RemoveStatus) checkStatusRef(c, where, e.statusId);
    checkStatusRef(c, where, e.scaleByStatus);
    checkStatusRef(c, where, e.ifTargetHasStatus);
    checkStatusRef(c, where, e.ifTargetLacksStatus);
    checkStatusRef(c, where, e.ifSourceHasStatus);
    checkStatusRef(c, where, e.ifSourceLacksStatus);
    for (const EffectDef& n : e.onHit) checkEffect(c, where, n);
}

void validate(const Content& c) {
    for (const auto& up : c.units) {
        const UnitDef& u = up.second;
        const std::string w = "unit '" + u.id + "'";
        int basics = 0;
        std::set<std::string> ids;
        for (const AbilityDef& a : u.abilities) {
            if (!ids.insert(a.id).second) bad(w, "duplicate ability id '" + a.id + "'");
            if (a.kind == AbilityKind::Basic) ++basics;
            for (const EffectDef& e : a.effects) checkEffect(c, w + "/" + a.id, e);
            for (const TriggerDef& t : a.triggers) {
                checkStatusRef(c, w + "/" + a.id, t.statusId);
                for (const EffectDef& e : t.effects) checkEffect(c, w + "/" + a.id, e);
            }
            for (const AuraDef& au : a.auras) {
                checkStatusRef(c, w + "/" + a.id, au.statusId);
                checkTarget(c, w + "/" + a.id, au.target);
            }
            for (const DecisionNode& n : a.ai.nodes)
                if (n.type == "persistent_effect" && !n.paramStr.empty() && n.paramStr != "buff" && n.paramStr != "debuff")
                    checkStatusRef(c, w + "/" + a.id, n.paramStr);
        }
        if (basics != 1) bad(w, "must have exactly one basic ability (has " + std::to_string(basics) + ")");
        if (u.stats[idx(Stat::MaxHealth)] <= 0) bad(w, "health must be > 0");
        if (u.stats[idx(Stat::Speed)] <= 0) bad(w, "speed must be > 0");
    }
    for (const auto& sp : c.statuses) {
        const std::string w = "status '" + sp.first + "'";
        for (const std::string& b : sp.second.blocks)
            if (b != "buff" && b != "debuff" && !c.statuses.count(b)) bad(w, "blocks unknown status '" + b + "'");
        for (const std::string& x : sp.second.excludes)
            if (!c.statuses.count(x)) bad(w, "excludes unknown status '" + x + "'");
        for (const TriggerDef& t : sp.second.triggers) {
            checkStatusRef(c, w, t.statusId);
            for (const EffectDef& e : t.effects) checkEffect(c, w, e);
        }
    }
    for (const auto& tp : c.teams) {
        if (tp.second.units.empty()) bad("team '" + tp.first + "'", "has no units");
        for (const std::string& u : tp.second.units)
            if (!c.units.count(u)) bad("team '" + tp.first + "'", "unknown unit '" + u + "'");
    }
}

} // namespace

namespace {

std::vector<std::string> jsonFilesIn(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json") out.push_back(entry.path().string());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

// Loads <data>/library/*.json, then content.json, then <data>/user/*.json and merges them. Splitting the
// data lets the shared status library live apart from the roster, and lets you drop in your own units
// without touching the shipped files. A path that isn't named content.json loads just that one file.
Content loadContent(const std::string& path) {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    const fs::path p(path);
    if (p.filename() == "content.json") {
        const fs::path dir = p.parent_path();
        for (const std::string& f : jsonFilesIn(dir / "library")) files.push_back(f);
        files.push_back(path);
        for (const std::string& f : jsonFilesIn(dir / "user")) files.push_back(f);
    } else {
        files.push_back(path);
    }
    std::vector<json::Value> roots;
    for (const std::string& f : files) roots.push_back(json::parseFile(f));

    Content c;

    // pass 1: every status (effects consult them for default targets)
    for (size_t r = 0; r < roots.size(); ++r)
        for (const json::Value& s : roots[r].arr("statuses")) {
            StatusDef d;
            d.id = s.str("id");
            if (d.id.empty()) bad(files[r], "status needs an id");
            if (c.statuses.count(d.id)) bad(files[r], "duplicate status id '" + d.id + "'");
            const std::string w = "status '" + d.id + "'";
            d.name = s.str("name", d.id);
            d.description = s.str("description");
            d.kind = parseStatusKind(w, s.str("kind", "buff"));
            d.duration = s.integer("duration", 2);
            d.maxStacks = s.integer("max_stacks", 1);
            d.dispellable = s.boolean("dispellable", true);
            for (const json::Value& m : s.arr("mods")) parseMod(w, m, d.mods);
            for (const std::string& f : s.strArray("flags")) d.flags |= parseFlag(w, f);
            d.mode = parseMode(w, s.str("mode", "none"));
            d.amount = s.num("amount", 0.0);
            d.reviveHealth = s.num("revive_health", 0.0);
            d.reviveProtection = s.num("revive_protection", 0.0);
            d.reviveTurnMeter = s.num("revive_turn_meter", 0.0);
            d.reviveBonusTurn = s.boolean("revive_bonus_turn", false);
            d.survive = s.boolean("survive", false);
            d.surviveHealth = s.num("survive_health", 0.0);
            d.surviveProtection = s.num("survive_protection", 0.0);
            d.blocks = s.strArray("blocks");
            d.excludes = s.strArray("excludes");
            c.statusOrder.push_back(d.id);
            c.statuses[d.id] = d;
        }
    // pass 2: reactions carried by statuses (they may mention statuses from any file)
    for (size_t r = 0; r < roots.size(); ++r)
        for (const json::Value& s : roots[r].arr("statuses"))
            if (s.has("triggers")) {
                StatusDef& d = c.statuses[s.str("id")];
                for (const json::Value& t : s.arr("triggers")) d.triggers.push_back(parseTrigger("status '" + d.id + "'", t, c));
            }

    for (size_t r = 0; r < roots.size(); ++r)
        for (const json::Value& u : roots[r].arr("units")) {
            UnitDef d;
            d.id = u.str("id");
            if (d.id.empty()) bad(files[r], "unit needs an id");
            if (c.units.count(d.id)) bad(files[r], "duplicate unit id '" + d.id + "'");
            const std::string w = "unit '" + d.id + "'";
            d.name = u.str("name", d.id);
            d.tags = u.strArray("tags");
            d.level = u.integer("level", 85);
            d.stats = defaultStats();
            if (const json::Value* st = u.find("stats")) {
                for (size_t i = 0; i < st->keys.size(); ++i) {
                    std::string key = st->keys[i];
                    double value = st->vals[i].n;
                    // Percent form, as printed on the game's stat screen: "potency_pct": 105.2 means 105.2%.
                    // armor_pct / resistance_pct are the damage-reduction percentages and are converted to the
                    // flat rating the engine uses (flat = p x level x 7.5 / (1 - p)).
                    if (key.size() > 4 && key.compare(key.size() - 4, 4, "_pct") == 0) {
                        key.erase(key.size() - 4);
                        if (key == "armor" || key == "resistance") {
                            if (value < 0 || value >= 100) bad(w, "'" + st->keys[i] + "' must be between 0 and 100");
                            value = value / 100.0 * d.level * 7.5 / (1.0 - value / 100.0);
                        } else {
                            value /= 100.0;
                        }
                    }
                    std::vector<Stat> stats;
                    if (!statsFromKey(key, stats)) bad(w, "unknown stat '" + st->keys[i] + "'");
                    for (Stat s : stats) d.stats[idx(s)] = value;
                }
            }
            for (const json::Value& a : u.arr("abilities")) d.abilities.push_back(parseAbility(w, a, c));
            {
                const json::Value* pr = u.find("presentation");
                d.presentation.prefab = pr ? pr->str("prefab", "units/" + d.id) : "units/" + d.id;
                d.presentation.portrait = pr ? pr->str("portrait", "portraits/" + d.id) : "portraits/" + d.id;
                d.presentation.clips = {{"idle", "idle"}, {"hit", "hit"}, {"death", "death"}, {"victory", "victory"}};
                if (pr)
                    if (const json::Value* cl = pr->find("clips"))
                        for (size_t i = 0; i < cl->keys.size(); ++i)
                            if (cl->vals[i].isStr()) d.presentation.clips[cl->keys[i]] = cl->vals[i].s;
            }
            c.unitOrder.push_back(d.id);
            c.units[d.id] = std::move(d);
        }

    for (size_t r = 0; r < roots.size(); ++r)
        for (const json::Value& s : roots[r].arr("stages")) {
            StageDef d;
            d.id = s.str("id");
            if (d.id.empty()) bad(files[r], "stage needs an id");
            for (const StageDef& o : c.stages)
                if (o.id == d.id) bad(files[r], "duplicate stage id '" + d.id + "'");
            d.name = s.str("name", d.id);
            d.prefab = s.str("prefab", "stages/" + d.id);
            c.stages.push_back(d);
        }

    for (size_t r = 0; r < roots.size(); ++r)
        for (const json::Value& t : roots[r].arr("teams")) {
            TeamDef d;
            d.id = t.str("id");
            if (d.id.empty()) bad(files[r], "team needs an id");
            if (c.teams.count(d.id)) bad(files[r], "duplicate team id '" + d.id + "'");
            d.name = t.str("name", d.id);
            d.units = t.strArray("units");
            d.stage = t.str("stage");
            if (!d.stage.empty()) {
                bool found = false;
                for (const StageDef& s : c.stages) found = found || s.id == d.stage;
                if (!found) bad("team '" + d.id + "'", "unknown stage '" + d.stage + "'");
            }
            c.teamOrder.push_back(d.id);
            c.teams[d.id] = d;
        }

    validate(c);
    return c;
}
