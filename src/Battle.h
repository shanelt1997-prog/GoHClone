// Battle engine: turn-meter scheduler, effect executor, status system, event triggers.
#pragma once
#include "Defs.h"
#include "Rules.h"

#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

struct StatusInst {
    const StatusDef* def = nullptr;
    int stacks = 1;
    int remaining = -1;        // owner turns left; -1 = permanent
    int source = -1;           // unit that applied it
    bool fromAura = false;     // leader/unique aura: permanent, not dispellable
    double amount = 0.0;       // effective amount (override or definition default)
    double bonusApplied = 0.0; // BonusProtection currently granted by this status
    long appliedSeq = -1;      // turn sequence number when applied / last refreshed
    std::vector<long> lastFired; // per status trigger: turn sequence of the last firing
    std::vector<int> fireCount;  // per status trigger: firings so far
};

struct AbilityState {
    const AbilityDef* def = nullptr;
    int cooldown = 0;
    int charge = 0;      // ultimate charge
    long usedSeq = -1;   // turn sequence number of the last in-turn use
};

struct UnitStats { // per-unit battle statistics (used by the simulator)
    long long damageDealt = 0;
    long long damageTaken = 0;
    long long healingDone = 0;
    int kills = 0;
    int turns = 0;
    int crits = 0;
    int dodges = 0;
    int resists = 0;
    int debuffsLanded = 0;
};

struct BattleUnit {
    int id = 0;
    int side = 0;
    int slot = 0;
    const UnitDef* def = nullptr;
    std::string name;
    bool isLeader = false;

    double health = 0;
    double protection = 0;
    double bonusProt = 0;  // extra protection capacity granted by Bonus Protection statuses
    double tm = 0;         // turn meter as a fraction; 1.0 == 100%
    bool alive = true;
    bool bonusTurn = false;
    bool cannotRevive = false; // defeated while under a "can't be revived" effect

    std::vector<StatusInst> statuses;
    std::vector<AbilityState> abilities;
    int basicIdx = 0;

    std::vector<std::size_t> trigBase; // per ability: base index into lastFired/fireCount
    std::vector<long> lastFired;       // per trigger: turn sequence of the last firing
    std::vector<int> fireCount;        // per trigger: firings this battle

    UnitStats st;
};

struct EventData {
    EventType type = EventType::Attack;
    int subject = -1;
    int other = -1;
    double amount = 0.0;
    const AbilityDef* ability = nullptr;
    const StatusDef* status = nullptr;
    bool inTurn = true; // false for assists / counters / other out-of-turn actions
};

struct Decision {
    int ability = 0; // index into BattleUnit::abilities
    int target = -1;
};

// Structured events for front ends (the animated arena view). The text log is unaffected.
enum class VisKind {
    TurnStart,
    TurnEnd,
    UseAbility,
    Reaction,    // an out-of-turn attack is about to happen (flag: true = counter, false = assist)
    Damage,      // unit lost a = protection, b = health (flag: critical; text: DoT name / "immune")
    Heal,        // unit recovered a = health, b = protection
    Evade,       // unit evaded an attack from `other`
    Resist,      // unit resisted a debuff from `other` (text: status name)
    StatusGain,  // text: status name (flag: is a debuff)
    StatusLose,  // text: status name
    Defeat,
    Revive,
    TurnMeter,   // a = change as a fraction
    Stunned
};

struct VisEvent {
    VisKind kind = VisKind::TurnStart;
    int unit = -1;   // main subject
    int other = -1;  // secondary unit (attacker / target / source)
    double a = 0.0;
    double b = 0.0;
    bool flag = false;
    std::string text;
};

struct BattleObserver {
    virtual ~BattleObserver() = default;
    virtual void onEvent(const VisEvent& ev) = 0;
};

class Battle;

struct Controller {
    virtual ~Controller() = default;
    virtual Decision decide(Battle& battle, BattleUnit& actor) = 0;
};

// Context threaded through effect execution.
struct EffectCtx {
    int source = -1;
    int selected = -1;     // player/AI selected primary target (or event "other" for triggers)
    int hitTarget = -1;    // target of the parent hit when running on_hit effects
    int evSubject = -1;
    int evOther = -1;
    const AbilityDef* ability = nullptr;
    bool outOfTurn = false;
    bool isCounter = false;
    bool fromTrigger = false;
    double scale = 1.0;    // damage scale (assists)
    bool dealtDamage = false;
    double totalDamage = 0.0;
    int firstHit = -1;
    std::vector<char> counteredBy; // defenders that already queued a counter during this action
    double evAmount = 0.0;         // amount carried by the triggering event (e.g. damage received)
    bool usedAdvantage = false;    // an Advantage buff made one of this action's hits critical
};

class Battle {
public:
    Battle(const Content& content, const Rules& rules, std::uint64_t seed);

    // Setup
    void addTeam(int side, const std::vector<std::string>& unitIds); // first id is the leader
    void start();

    // State
    bool over() const;
    int winner() const; // -1 ongoing, 0 / 1 winning side, 2 draw
    int aliveCount(int side) const;

    // Flow
    int nextActor();                                  // advances turn meters; -1 if nobody can act
    void takeTurn(int actorId, Controller& ctrl);
    bool step(Controller& side0, Controller& side1);  // one turn; false when the battle is over
    void run(Controller& side0, Controller& side1);

    // Queries (also used by AI / UI)
    double stat(const BattleUnit& u, Stat s) const;
    double maxHealth(const BattleUnit& u) const;
    double maxProtection(const BattleUnit& u) const;
    double healthFrac(const BattleUnit& u) const;
    double survivabilityFrac(const BattleUnit& u) const;
    bool hasFlag(const BattleUnit& u, std::uint32_t flag) const;
    bool hasStatus(const BattleUnit& u, const std::string& id) const;
    int statusCount(const BattleUnit& u, StatusKind kind) const;
    bool abilityUsable(const BattleUnit& u, std::size_t abilityIdx, std::string* why = nullptr) const;
    std::vector<int> validTargets(const BattleUnit& src, const AbilityDef& ab) const;
    bool isEnemy(int a, int b) const { return units[(size_t)a].side != units[(size_t)b].side; }

    double rand01();
    int randInt(int n); // [0, n)

    // Public state
    const Content& content;
    Rules rules;
    std::vector<BattleUnit> units;
    std::vector<std::string> log;
    BattleObserver* observer = nullptr; // optional; receives structured events as they happen
    bool logging = false; // collect log lines
    bool echo = false;    // also print each log line to stdout as it happens
    long turnSeq = 0;
    int turnsTaken = 0;
    bool timedOut = false;

private:
    struct Pending {
        int unit;
        int target;
        double scale;
        bool isCounter;
    };
    enum class ApplyResult { Applied, Resisted, Immune, Evaded, Failed };
    struct DamageOutcome {
        double prot = 0;
        double hp = 0;
        bool lethal = false;
    };

    std::mt19937_64 rng_;
    std::deque<int> bonusQueue_;
    std::deque<Pending> assists_;
    std::deque<Pending> counters_;
    int pendingBudget_ = 0;
    int depth_ = 0;
    int useDepth_ = 0;   // nesting of UseAbility effects (guards against runaway chains)
    int actorNow_ = -1;  // unit whose turn is being processed

    void addLog(const std::string& line);
    void emit(VisKind kind, int unit, int other = -1, double a = 0.0, double b = 0.0, bool flag = false,
              const std::string& text = std::string());

    // targeting
    std::vector<int> candidates(const TargetSpec& t, const EffectCtx& c) const;
    std::vector<int> resolveTargets(const TargetSpec& t, const EffectCtx& c);

    // effects
    void execEffects(const std::vector<EffectDef>& list, EffectCtx& c);
    void execEffect(const EffectDef& e, EffectCtx& c);
    bool hit(const EffectDef& e, EffectCtx& c, BattleUnit& src, BattleUnit& tgt);
    double basisValue(const std::string& basis, const BattleUnit& src, const BattleUnit& tgt, double evAmount = 0.0) const;
    int stackCount(const BattleUnit& u, const std::string& statusId) const;
    bool excludedBy(const BattleUnit& u, const StatusDef& d) const;
    void consumeFlagStatus(BattleUnit& u, std::uint32_t flag);
    bool triggerMatches(const BattleUnit& owner, const TriggerDef& tr, const EventData& ev) const;
    bool tryEvade(BattleUnit& src, BattleUnit& tgt, ApplyType apply, const AbilityDef* ab, bool attack = false);
    bool tryResist(BattleUnit& src, BattleUnit& tgt, const StatusDef* st);
    ApplyResult applyStatus(int srcId, int tgtId, const StatusDef& def, int duration, int stacks, double amount,
                            bool allowResist, bool canResist, bool fromAura);
    int dispel(int tgtId, const std::string& kind, int count);
    bool healUnit(BattleUnit& u, double hp, double prot, int srcId);
    DamageOutcome applyDamage(BattleUnit& t, double dmg, bool ignoreProtection);
    void onLethal(BattleUnit& t, int killer, bool instant = false);
    void removeStatusIndices(BattleUnit& u, std::vector<std::size_t> indices);
    void clampPools(BattleUnit& u);
    bool modifyTurnMeter(BattleUnit& src, BattleUnit& tgt, double amount, const EffectDef& e, const AbilityDef* ab);

    // events / triggers
    void fire(const EventData& ev);
    bool scopeMatches(const BattleUnit& owner, Scope scope, const EventData& ev) const;

    // turn processing
    void tickStartOfTurn(BattleUnit& u);
    void tickDurations(BattleUnit& u);
    void tickCooldowns(BattleUnit& u);
    void performDecision(BattleUnit& u, const Decision& d);
    void useAbility(BattleUnit& u, std::size_t abilityIdx, int target, bool outOfTurn, double scale, bool isCounter);
    void processPending();
};
