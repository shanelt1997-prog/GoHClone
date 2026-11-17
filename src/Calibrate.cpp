#include "Calibrate.h"

#include <algorithm>
#include <limits>

double flatFromPercent(double percent, int level, double levelConstant) {
    const double p = percent / 100.0;
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1e9; // 100% mitigation is unreachable; treat as "huge"
    return p * static_cast<double>(level) * levelConstant / (1.0 - p);
}

double percentFromFlat(double flat, int level, double levelConstant) {
    if (flat <= 0.0) return 0.0;
    return 100.0 * flat / (flat + static_cast<double>(level) * levelConstant);
}

double predictDamage(const Rules& rules, const CalibrationInput& in, double multiplier) {
    const double def = Rules::effectiveDefense(in.defenseRating, in.defensePen, in.armorPen);
    const double mit = rules.mitigation(def, in.level);
    return in.offense * multiplier * in.critMultiplier * in.otherMultiplier * (1.0 - mit);
}

CalibrationResult calibrate(const Rules& rules, const CalibrationInput& in, const std::vector<double>& observed) {
    CalibrationResult r;
    r.effectiveDefense = Rules::effectiveDefense(in.defenseRating, in.defensePen, in.armorPen);
    r.mitigation = rules.mitigation(r.effectiveDefense, in.level);
    const double scale = in.offense * in.critMultiplier * in.otherMultiplier * (1.0 - r.mitigation);
    if (scale <= 0.0 || observed.empty()) {
        r.consistent = false;
        return r;
    }
    // observed = scale x multiplier x v, with v somewhere in [1 - variance, 1 + variance]
    r.low = 0.0;
    r.high = std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (double obs : observed) {
        const double m = obs / scale;
        r.perHit.push_back(m);
        sum += m;
        r.low = std::max(r.low, m / (1.0 + in.variance));
        r.high = std::min(r.high, in.variance < 1.0 ? m / (1.0 - in.variance) : std::numeric_limits<double>::infinity());
    }
    r.consistent = r.low <= r.high;
    r.estimate = r.consistent ? 0.5 * (r.low + r.high) : sum / static_cast<double>(observed.size());
    return r;
}
