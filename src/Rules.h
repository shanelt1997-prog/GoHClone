// Combat formulas and tunable conventions.
//
// Each formula below is taken from community-documented behaviour of the
// original game (EA forum threads, community wikis). They are approximations
// verified only against published examples - see README.md "Calibration".
// Everything uncertain is a switch/constant loaded from data/rules.json so it
// can be tuned without touching code.
#pragma once
#include "Json.h"
#include <algorithm>
#include <cmath>

struct Rules {
    // Mitigation = defense / (defense + level * levelConstant).
    // Community consensus is 7.5 (an older constant of 10 appears in outdated threads).
    double levelConstant = 7.5;

    // Per-hit random damage spread. Community reports "typically 5%".
    double variance = 0.05;

    // Debuffs always have at least this chance to be resisted:
    //   resist = clamp(max(tenacity - potency, resistFloor), resistFloor, 1)
    double resistFloor = 0.15;

    // Health steal heals a % of damage dealt. Whether protection damage counts
    // is not firmly documented, so it's a switch.
    bool healthStealIncludesProtection = true;

    // Cooldown convention. false: an ability used this turn is not decremented at the
    // end of that same turn (cooldown N => N more turns of the owner pass first).
    bool cooldownCountsUseTurn = false;

    // Damage beyond remaining protection carries over into health.
    bool damageOverflowToHealth = true;

    // Does a status that lands on a unit during that unit's own turn (a self-buff, or a debuff
    // applied by a counterattack) lose a turn of duration at the end of that same turn?
    // Sources conflict, so it is a switch.
    bool statusesTickOnApplyTurn = true;

    // Turn meter may exceed 100% from gains (overflow decides who goes first).
    double tmCap = 10.0;

    // Safety valve: battle ends in a draw after this many turns.
    int maxTurns = 600;

    // Round final damage to whole numbers (the game displays integers).
    bool roundDamage = true;

    // Safety valves for out-of-turn actions per turn and event recursion depth.
    int maxPendingPerTurn = 24;
    int maxEventDepth = 6;

    static Rules fromJson(const json::Value& v) {
        Rules r;
        r.levelConstant = v.num("level_constant", r.levelConstant);
        r.variance = v.num("variance", r.variance);
        r.resistFloor = v.num("resist_floor", r.resistFloor);
        r.healthStealIncludesProtection = v.boolean("health_steal_includes_protection", r.healthStealIncludesProtection);
        r.cooldownCountsUseTurn = v.boolean("cooldown_counts_use_turn", r.cooldownCountsUseTurn);
        r.damageOverflowToHealth = v.boolean("damage_overflow_to_health", r.damageOverflowToHealth);
        r.statusesTickOnApplyTurn = v.boolean("statuses_tick_on_apply_turn", r.statusesTickOnApplyTurn);
        r.tmCap = v.num("tm_cap", r.tmCap);
        r.maxTurns = v.integer("max_turns", r.maxTurns);
        r.roundDamage = v.boolean("round_damage", r.roundDamage);
        r.maxPendingPerTurn = v.integer("max_pending_per_turn", r.maxPendingPerTurn);
        r.maxEventDepth = v.integer("max_event_depth", r.maxEventDepth);
        return r;
    }

    // --- Formulas -----------------------------------------------------------

    // Damage reduction fraction from a flat armor/resistance rating.
    double mitigation(double flatDefense, int level) const {
        if (flatDefense <= 0.0) return 0.0;
        return flatDefense / (flatDefense + static_cast<double>(level) * levelConstant);
    }

    // Defense after defense-penetration (fraction) and flat armor/resistance penetration.
    static double effectiveDefense(double flatDefense, double defensePen, double flatPen) {
        double d = flatDefense * (1.0 - std::clamp(defensePen, 0.0, 1.0));
        d -= flatPen;
        return d > 0.0 ? d : 0.0;
    }

    // Chance the target resists a detrimental effect.
    double resistChance(double tenacity, double potency) const {
        double r = tenacity - potency;
        if (r < resistFloor) r = resistFloor;
        if (r > 1.0) r = 1.0;
        return r;
    }

    // Chance an attack is critical: crit chance minus the defender's crit avoidance.
    static double critChance(double crit, double avoidance) { return std::clamp(crit - avoidance, 0.0, 1.0); }

    // Chance an attack is evaded: dodge/deflection minus the attacker's accuracy.
    static double evadeChance(double evasion, double accuracy) { return std::clamp(evasion - accuracy, 0.0, 1.0); }
};
