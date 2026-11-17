// Calibration helper.
//
// The in-game ability text never shows the hidden damage multiplier of an ability, but you can measure
// it: hit something with known stats and note the damage number. This module inverts the damage formula
//
//     damage = offense x multiplier x (crit) x (other modifiers) x (1 - mitigation) x (1 +/- variance)
//
// to estimate the multiplier, and combines several observations into a tighter range.
#pragma once
#include "Rules.h"

#include <vector>

struct CalibrationInput {
    double offense = 0;          // attacker's Physical or Special Damage at the moment of the hit
    double defenseRating = 0;    // target's flat Armor (physical hits) or Resistance (special hits)
    int level = 85;              // target's level
    double armorPen = 0;         // attacker's flat armor/resistance penetration
    double defensePen = 0;       // attacker's defense penetration (fraction, 0.2 = 20%)
    double critMultiplier = 1.0; // 1.0 for a normal hit; the attacker's Critical Damage stat for a crit
    double otherMultiplier = 1.0;// any other damage modifier in play (damage dealt/taken up or down)
    double variance = 0.05;      // the game's per-hit random spread
};

struct CalibrationResult {
    double mitigation = 0;       // damage reduction fraction used
    double effectiveDefense = 0;
    std::vector<double> perHit;  // multiplier implied by each observation (if variance were zero)
    double low = 0, high = 0;    // multipliers consistent with ALL observations (intersection)
    double estimate = 0;         // best single estimate
    bool consistent = true;      // false if no single multiplier explains every observation
};

CalibrationResult calibrate(const Rules& rules, const CalibrationInput& in, const std::vector<double>& observedDamage);

// Expected damage of a hit with the given multiplier (no variance).
double predictDamage(const Rules& rules, const CalibrationInput& in, double multiplier);

// Converts the armor / resistance percentage shown on the game's stat screen into the flat rating the
// engine uses, and back.
double flatFromPercent(double percent, int level, double levelConstant = 7.5);
double percentFromFlat(double flat, int level, double levelConstant = 7.5);
