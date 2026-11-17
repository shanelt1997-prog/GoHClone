// Core stat model. Percent-style stats are stored as fractions (0.25 == 25%).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class Stat {
    MaxHealth,
    MaxProtection,
    Speed,
    PhysDamage,
    SpecDamage,
    Armor,        // flat rating; converted to % mitigation by Rules::mitigation
    Resistance,   // flat rating; same conversion, used against special damage
    PhysCrit,     // physical critical chance
    SpecCrit,     // special critical chance
    CritDamage,   // crit multiplier (1.5 == a crit deals 150% damage)
    Potency,
    Tenacity,
    Accuracy,
    PhysCritAvoid,
    SpecCritAvoid,
    Dodge,        // evasion vs physical attacks
    Deflection,   // evasion vs special attacks
    HealthSteal,
    ArmorPen,     // flat armor ignored
    ResistancePen,
    DefensePen,   // fraction of defense ignored
    CounterChance,
    DamageDealtMult,
    DamageTakenMult,
    OutOfTurnDamageMult, // multiplies damage dealt by assists / counters / bonus attacks
    Count
};

constexpr std::size_t kStatCount = static_cast<std::size_t>(Stat::Count);
using StatArray = std::array<double, kStatCount>;

inline std::size_t idx(Stat s) { return static_cast<std::size_t>(s); }

inline StatArray defaultStats() {
    StatArray a{};
    a.fill(0.0);
    a[idx(Stat::CritDamage)] = 1.5;
    a[idx(Stat::Dodge)] = 0.02;
    a[idx(Stat::Deflection)] = 0.02;
    a[idx(Stat::DamageDealtMult)] = 1.0;
    a[idx(Stat::DamageTakenMult)] = 1.0;
    a[idx(Stat::OutOfTurnDamageMult)] = 1.0;
    return a;
}

inline const char* statName(Stat s) {
    switch (s) {
    case Stat::MaxHealth: return "Health";
    case Stat::MaxProtection: return "Protection";
    case Stat::Speed: return "Speed";
    case Stat::PhysDamage: return "Physical Damage";
    case Stat::SpecDamage: return "Special Damage";
    case Stat::Armor: return "Armor";
    case Stat::Resistance: return "Resistance";
    case Stat::PhysCrit: return "Phys Crit Chance";
    case Stat::SpecCrit: return "Spec Crit Chance";
    case Stat::CritDamage: return "Crit Damage";
    case Stat::Potency: return "Potency";
    case Stat::Tenacity: return "Tenacity";
    case Stat::Accuracy: return "Accuracy";
    case Stat::PhysCritAvoid: return "Phys Crit Avoid";
    case Stat::SpecCritAvoid: return "Spec Crit Avoid";
    case Stat::Dodge: return "Dodge";
    case Stat::Deflection: return "Deflection";
    case Stat::HealthSteal: return "Health Steal";
    case Stat::ArmorPen: return "Armor Pen";
    case Stat::ResistancePen: return "Resistance Pen";
    case Stat::DefensePen: return "Defense Pen";
    case Stat::CounterChance: return "Counter Chance";
    case Stat::DamageDealtMult: return "Damage Dealt x";
    case Stat::DamageTakenMult: return "Damage Taken x";
    case Stat::OutOfTurnDamageMult: return "Out-of-turn Damage x";
    default: return "?";
    }
}

// Maps a data-file key to one or more stats ("offense" -> both damage stats).
// Returns false for unknown keys.
inline bool statsFromKey(const std::string& k, std::vector<Stat>& out) {
    out.clear();
    if (k == "health") out = {Stat::MaxHealth};
    else if (k == "protection") out = {Stat::MaxProtection};
    else if (k == "speed") out = {Stat::Speed};
    else if (k == "physical_damage") out = {Stat::PhysDamage};
    else if (k == "special_damage") out = {Stat::SpecDamage};
    else if (k == "offense") out = {Stat::PhysDamage, Stat::SpecDamage};
    else if (k == "armor") out = {Stat::Armor};
    else if (k == "resistance") out = {Stat::Resistance};
    else if (k == "defense") out = {Stat::Armor, Stat::Resistance};
    else if (k == "phys_crit") out = {Stat::PhysCrit};
    else if (k == "spec_crit") out = {Stat::SpecCrit};
    else if (k == "crit_chance") out = {Stat::PhysCrit, Stat::SpecCrit};
    else if (k == "crit_damage") out = {Stat::CritDamage};
    else if (k == "potency") out = {Stat::Potency};
    else if (k == "tenacity") out = {Stat::Tenacity};
    else if (k == "accuracy") out = {Stat::Accuracy};
    else if (k == "phys_crit_avoid") out = {Stat::PhysCritAvoid};
    else if (k == "spec_crit_avoid") out = {Stat::SpecCritAvoid};
    else if (k == "crit_avoid") out = {Stat::PhysCritAvoid, Stat::SpecCritAvoid};
    else if (k == "dodge") out = {Stat::Dodge};
    else if (k == "deflection") out = {Stat::Deflection};
    else if (k == "evasion") out = {Stat::Dodge, Stat::Deflection};
    else if (k == "health_steal") out = {Stat::HealthSteal};
    else if (k == "armor_pen") out = {Stat::ArmorPen};
    else if (k == "resistance_pen") out = {Stat::ResistancePen};
    else if (k == "defense_pen") out = {Stat::DefensePen};
    else if (k == "counter_chance") out = {Stat::CounterChance};
    else if (k == "damage_dealt_mult") out = {Stat::DamageDealtMult};
    else if (k == "damage_taken_mult") out = {Stat::DamageTakenMult};
    else if (k == "out_of_turn_damage_mult") out = {Stat::OutOfTurnDamageMult};
    else if (k == "penetration") out = {Stat::ArmorPen, Stat::ResistancePen}; // flat, both defenses
    else return false;
    return true;
}
