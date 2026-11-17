// Data model. Everything gameplay-relevant is data-driven: abilities are lists of
// effects, passives are triggers on battle events, leaders/uniques apply auras.
#pragma once
#include "Types.h"
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------- enums ------

enum class EffectType {
    Damage,
    Heal,
    ApplyStatus,
    ModifyTurnMeter,
    ModifyCooldown,
    ModifyUltimate,
    Dispel,
    Revive,
    BonusMove,
    Assist,
    Counter,
    UseAbility,   // the source uses one of its own abilities on the target (bonus / follow-up attack)
    Defeat,       // instant defeat
    RemoveStatus  // remove stacks of one specific status
};

enum class ApplyType { Unavoidable, Physical, Mental }; // which evasion stat applies
enum class DamageType { AttackDamage, AbilityPower, Unmitigable };
enum class Side { Enemy, Ally, Both };
enum class Select {
    Selected,
    All,
    Self,
    Random,
    LowestSurvivability,
    HighestSurvivability,
    Leader,
    EventSubject,
    EventOther,
    HitTarget
};
enum class HealthState { Alive, Dead, Any };
enum class AbilityKind { Basic, Special, Leader, Unique, Ultimate };
enum class Pick { Enemy, Ally, DeadAlly, Self, None }; // what the player must select
enum class StatusKind { Buff, Debuff, Neutral };
enum class ModOp { Add, PctBase };
enum class AmountMode { None, Dot, Hot, ProtOverTime, BonusProtection };
enum class EventType {
    StartOfBattle,
    StartOfTurn,
    EndOfTurn,
    UsedAbility,
    Attack,
    ReceiveDamage,
    Crit,
    ReceiveCrit,
    Dodged,     // subject evaded an attack
    WasDodged,  // subject's attack was evaded
    Kill,
    Death,
    StatusAdded,
    StatusRemoved,
    Resisted,    // subject resisted a debuff
    WasResisted, // subject's debuff was resisted
    ReceiveHeal,
    Count
};
enum class Scope { Self, Ally, AnyAlly, Enemy, Any }; // relation of event subject to trigger owner

// Status behaviour flags.
enum StatusFlag : std::uint32_t {
    SF_None = 0,
    SF_Taunt = 1u << 0,
    SF_Stealth = 1u << 1,
    SF_Stun = 1u << 2,          // owner's turn is skipped
    SF_AbilityBlock = 1u << 3,  // cannot use special/ultimate abilities
    SF_NoAssist = 1u << 4,
    SF_NoCounter = 1u << 5,
    SF_NoTMGain = 1u << 6,
    SF_HealingImmunity = 1u << 7,
    SF_DamageImmunity = 1u << 8,
    SF_CritImmunity = 1u << 9,  // cannot be critically hit
    SF_DebuffImmunity = 1u << 10,
    SF_BuffImmunity = 1u << 11,
    SF_IgnoreProtection = 1u << 12, // DoT ticks bypass protection
    SF_NoEvade = 1u << 13,          // cannot evade attacks (stun, fear, burning)
    SF_Foresight = 1u << 14,        // evades the next attack (consumed)
    SF_Advantage = 1u << 15,        // the next attack is a critical hit (consumed)
    SF_Blind = 1u << 16,            // the next attack misses (consumed)
    SF_Vulnerable = 1u << 17,       // will be critically hit if possible
    SF_SpeedZero = 1u << 18,        // speed is set to 0
    SF_NoRevive = 1u << 19,         // cannot be revived once defeated
    SF_InstantDefeatImmune = 1u << 20,
    SF_ProtectionDisabled = 1u << 21, // protection does nothing; cannot gain it
    SF_DamageToOne = 1u << 22,      // every hit deals 1 damage
    SF_TMImmune = 1u << 23,         // immune to turn meter reduction
    SF_NoBuffsOffTurn = 1u << 24,   // cannot gain buffs during another unit's turn (isolate)
    SF_IgnoreTaunt = 1u << 25,      // may target past taunting / marked units
    SF_Uncounterable = 1u << 26,    // this unit's attacks cannot be countered
    SF_RiposteIgnoreProt = 1u << 27,// out-of-turn attacks ignore protection
    SF_NoProtectionRecovery = 1u << 28,
    SF_Daze = SF_NoAssist | SF_NoCounter | SF_NoTMGain
};

// --------------------------------------------------------------- statuses ----

struct StatMod {
    Stat stat = Stat::MaxHealth;
    ModOp op = ModOp::Add;
    double value = 0.0; // Add: in stat units. PctBase: fraction of the unit's base stat.
};


// ---------------------------------------------------------------- effects ----

struct TargetSpec {
    Select select = Select::Selected;
    Side side = Side::Enemy;
    std::vector<std::string> anyTags;  // candidate must have at least one
    std::vector<std::string> noTags;   // candidate must have none
    HealthState health = HealthState::Alive;
    bool excludeSelf = false;
    bool excludeSelected = false;
    std::string hasStatus;             // candidate must have this status id
    std::string lacksStatus;           // candidate must not have it
    int count = 1;                     // for Select::Random
    double healthBelow = -1.0;         // health fraction <= value
    double healthAbove = -1.0;         // health fraction >= value
};

struct EffectDef {
    EffectType type = EffectType::Damage;
    TargetSpec target;
    double chance = 1.0;               // rolled per target

    // Damage / healing amounts: basisValue * multiplier + additive
    DamageType damageType = DamageType::AttackDamage;
    ApplyType apply = ApplyType::Unavoidable;
    std::string basis;                 // "" = natural stat; or target_max_health, source_max_health, ...
    double multiplier = 1.0;
    double additive = 0.0;
    double variance = -1.0;            // < 0: use Rules::variance
    int hits = 1;
    bool canCrit = true;
    bool forceCrit = false;
    bool canResist = true;             // false: unresistable
    bool canEvade = true;

    // Heal
    double protectionMultiplier = 0.0; // fraction of basis healed as protection

    // ApplyStatus / Dispel / TM / cooldown / ultimate
    std::string statusId;
    int duration = -999;               // -999 = use the status default
    int stacks = 1;
    double amount = 0.0;               // status amount override, TM fraction, cooldown delta, ult charge
    std::string abilityRef;            // ModifyCooldown: ability id, "*" = all non-basic
    std::string dispelKind;            // "buff", "debuff", "all"
    int count = 0;                     // Dispel: how many (0 = all)

    // Conditions
    std::string ifTargetHasStatus, ifTargetLacksStatus, ifSourceHasStatus, ifSourceLacksStatus;
    double ifTargetHealthBelow = -1.0;

    // Damage options
    bool noReactions = false;          // not an "attack": no on-damage triggers, counters or health steal
    bool ignoreProtection = false;     // "true damage" that goes straight to health
    bool cannotDefeat = false;         // damage can't reduce the target below 1 health
    std::string scaleByStatus;         // multiply by the stack count of this status on the source
    bool outOfTurn = false;            // UseAbility: treat the action as out of turn

    // Executed after each landed hit/application on that target (HitTarget = that target).
    std::vector<EffectDef> onHit;
};

struct TriggerDef {
    EventType on = EventType::Attack;
    Scope scope = Scope::Self;
    std::vector<std::string> tags;       // event subject must have one
    std::vector<AbilityKind> abilityKinds; // event ability kind filter
    int statusKind = -1;                 // -1 any, else StatusKind
    std::string statusId;
    double chance = 1.0;
    bool oncePerTurn = false;
    int limit = 0;                       // max firings per battle (0 = unlimited)
    double healthBelow = -1.0;           // event subject's health fraction <= value
    bool requireOutOfTurn = false;
    bool requireInTurn = false;
    std::vector<EffectDef> effects;
};

// ------------------------------------------------------------- abilities -----

struct DecisionNode { // auto-battle decision test (mirrors the original data model's decision nodes)
    std::string type;      // single_health, median_health, number_of_targets, persistent_effect, ...
    std::string group = "enemy"; // self, ally, enemy, dead_ally
    std::string modifier = "greater_than"; // greater_than, less_than, equal_to
    double param = 0.0;
    std::string paramStr;
    std::vector<std::string> unitIds;
    double chance = 1.0;
    bool negate = false;
};

struct AiParams {
    std::vector<std::string> enemyRules; // targeting rules, applied as progressive filters
    std::vector<std::string> allyRules;
    std::vector<DecisionNode> nodes;     // all must pass for the AI to use the ability
};

struct AuraDef { // applied at battle start by leader / unique abilities
    TargetSpec target;
    std::string statusId;
    int duration = -1;
    double amount = 0.0;
};

struct StatusDef {
    std::string id;
    std::string name;
    std::string description;   // short human-readable summary (shown by the 'statuses' command)
    StatusKind kind = StatusKind::Buff;
    int duration = 2;          // default duration in the owner's turns; -1 = permanent
    int maxStacks = 1;
    bool dispellable = true;
    std::vector<StatMod> mods; // applied per stack
    std::uint32_t flags = SF_None;
    AmountMode mode = AmountMode::None;
    double amount = 0.0;       // fraction of max health (meaning depends on mode)

    // "Revive" effects: the unit is defeated first, then comes back.
    double reviveHealth = 0.0; // > 0: revive at this health fraction
    double reviveProtection = 0.0;
    double reviveTurnMeter = 0.0;
    bool reviveBonusTurn = false;
    // "Survive" effects (Endure / Second Wind style): lethal damage leaves the unit at 1% health
    // instead, plus the recovery below; the status (one stack) is used up.
    bool survive = false;
    double surviveHealth = 0.0;
    double surviveProtection = 0.0;

    std::vector<std::string> blocks;   // status ids (or "buff"/"debuff") that cannot be applied while this is active
    std::vector<std::string> excludes; // this status' stat mods are ignored while any of these statuses is present
    std::vector<TriggerDef> triggers;  // reactions that fire while a unit has this status (owner = the unit)
};

// Presentation hints. Pure data for front ends (the arena, a Unity client): the simulation never
// reads them. Anything left out in the data file is derived automatically by the loader.
struct AbilityPresentation {
    std::string tmpl;  // melee | ranged | area | self | support | none  (how the move is staged)
    std::string anim;  // animation clip name (default: the ability id)
    std::string vfx;   // optional visual-effect key
    std::string sfx;   // optional sound key
};

struct UnitPresentation {
    std::string prefab;   // content-pack key of the character model   (default "units/<id>")
    std::string portrait; // content-pack key of the portrait sprite   (default "portraits/<id>")
    std::map<std::string, std::string> clips; // idle / hit / death / victory -> clip name
};

struct StageDef {
    std::string id;
    std::string name;
    std::string prefab; // content-pack key of the battle environment
};

struct AbilityDef {
    std::string id;
    std::string name;
    std::string description;
    AbilityKind kind = AbilityKind::Basic;
    Pick pick = Pick::Enemy;
    int cooldown = 0;
    int ultimateCharge = 0;              // charge needed for Ultimate kind
    std::vector<EffectDef> effects;
    std::vector<TriggerDef> triggers;
    std::vector<AuraDef> auras;
    AiParams ai;
    AbilityPresentation presentation;
};

struct UnitDef {
    std::string id;
    std::string name;
    std::vector<std::string> tags;
    int level = 85;
    StatArray stats = defaultStats();
    std::vector<AbilityDef> abilities;
    UnitPresentation presentation;

    bool hasTag(const std::string& t) const {
        for (const std::string& x : tags)
            if (x == t) return true;
        return false;
    }
};

struct TeamDef {
    std::string id;
    std::string name;
    std::vector<std::string> units; // first unit is the leader
    std::string stage;              // optional stage id for this team's battles
};

// --------------------------------------------------------------- content -----

struct Content {
    std::map<std::string, StatusDef> statuses;
    std::map<std::string, UnitDef> units;
    std::map<std::string, TeamDef> teams;
    std::vector<StageDef> stages;
    std::vector<std::string> statusOrder;
    std::vector<std::string> unitOrder;
    std::vector<std::string> teamOrder;

    const StatusDef& status(const std::string& id) const {
        auto it = statuses.find(id);
        if (it == statuses.end()) throw std::runtime_error("unknown status id: " + id);
        return it->second;
    }
    const UnitDef& unit(const std::string& id) const {
        auto it = units.find(id);
        if (it == units.end()) throw std::runtime_error("unknown unit id: " + id);
        return it->second;
    }
    const TeamDef* team(const std::string& id) const {
        auto it = teams.find(id);
        return it == teams.end() ? nullptr : &it->second;
    }
};

// Loader (Loader.cpp). Validates every cross reference and throws std::runtime_error
// with a descriptive message on any problem.
Content loadContent(const std::string& path);
