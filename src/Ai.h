// Auto-battle AI: ability choice via data-defined decision nodes, target choice via
// progressive targeting rules.
#pragma once
#include "Battle.h"

#include <string>

class AiController : public Controller {
public:
    Decision decide(Battle& battle, BattleUnit& actor) override;
};

// True if the targeting rule string is understood by the AI (used for data validation).
bool isKnownTargetRule(const std::string& rule);
