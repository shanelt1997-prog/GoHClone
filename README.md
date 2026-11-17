# GoHClone — text-mode squad battler engine (C++17)

A data-driven, turn-meter-based squad battle engine modelled on the *structure* of a
well-known mobile squad collector. It runs entirely in the terminal for now; the engine
is UI-independent, so a Unity/other front end can be bolted on later.

* All characters, abilities and numbers in `data/` are **original placeholder content**.
* No code, art, audio, names or data from any commercial game is included.
* The combat formulas are facts about game behaviour gathered from public community
  documentation (see *Sources*). They are **approximations** — see *Calibration*.

---------------------------------------------------------------------------------------

## Build & run

### Visual Studio 2022
1. Open `GoHClone.sln`, choose **x64 / Debug** (or Release), press **F5**.
2. The program opens an **interactive menu** (play, watch, simulate, browse, inspect, self-test, display settings).
   Use **Ctrl+F5** so the console stays open. Leave *Project → Properties → Debugging →
   Command Arguments* empty; if you type arguments there the menu is skipped and that
   single command runs instead.
3. A post-build step copies `data/` next to the exe; the program also searches `./data`,
   `../data`, `../../data` and relative to the exe, or use `--data <dir>`.

> The `.vcxproj` targets toolset **v143** (VS 2022). It was written by hand and verified to
> be well-formed, but it has **not** been compiled with MSVC. The code itself is plain
> ISO C++17 and is warning-free under `g++ -Wall -Wextra -Wpedantic`. If MSVC complains,
> please send me the first error.

### Command line (Linux / macOS / MinGW)
```
make            # builds build/GoHClone
make test       # builds and runs the self-test suite
```

### Commands
Run with **no arguments** for the menu. For scripting, every menu action is also a command:
```
GoHClone play  [--a TEAM] [--b TEAM] [--seed N] [--arena] [--speed S]   you control Team A, AI controls Team B
GoHClone auto  [--a TEAM] [--b TEAM] [--seed N] [--arena] [--speed S] [--quiet]   AI vs AI
GoHClone sim   [--a TEAM] [--b TEAM] [--runs N] [--seed N]  balance statistics
GoHClone list                                      teams and units
GoHClone show <unit_id>                            stats and abilities
GoHClone statuses [buff|debuff|text]               every buff and debuff in the library (65 + 58)
GoHClone calibrate --stat N --defense-pct P --damage a,b,c   damage numbers from the game -> ability multiplier
GoHClone selftest                                  252 automated checks
GoHClone golden [--write]                          verify / regenerate the battle fingerprints
```
`TEAM` is a team id from `content.json` (e.g. `concord_strike`) or a comma list of unit
ids (first = leader): `--a aria_vance,kade_orlan,elin_moss`.
Every run prints its seed; re-using `--seed` reproduces the battle exactly.

In `play`: pick an ability by number, then a target. `i N` inspects unit #N, `a` hands the
rest of the battle to the AI, `q` quits.

### Two ways to watch a battle
Before every battle the menu asks how you want to see it:

* **Arena view** (default when the terminal supports it) - a clean, animated screen that is
  redrawn in place: both teams as cards with health, protection and turn-meter bars that drain as
  hits land, a floating damage number beside the unit that was hit (`!` = critical), `>` marking
  who is acting and `<` who is targeted, an action banner and a short feed of recent events.
  In `play` mode your ability and target prompts appear directly beneath the arena.
  Playback speed: Slow / Normal / Fast / Instant (jump to the result). A battle report with
  damage, healing and kills per unit is printed at the end.
* **Full details** - the complete turn-by-turn text log of every roll and effect.

The arena needs a window of at least 118 x 28 characters (the default Windows console/Terminal is
120 x 30) and a terminal with ANSI support (Windows 10+ consoles qualify). The menu checks the size
and tells you if you need to enlarge the window. If the bars look broken, switch to plain ASCII
in *Display settings* or pass `--ascii`. Extra flags: `--arena`, `--speed slow|normal|fast|instant`,
`--no-color`, `--ascii`.

---------------------------------------------------------------------------------------

## Architecture

```
src/Types.h     Stat enum + alias-aware stat keys
src/Rules.h     every combat formula as a pure function + tunable constants
src/Defs.h      data model: Effect, Trigger, Aura, Ability, Status, Unit, Team
src/Loader.cpp  JSON -> Content, with strict cross-reference validation
src/Battle.*    turn-meter scheduler, damage pipeline, effects, statuses, events
src/Ai.*        auto-battle: decision nodes + targeting rules
src/Arena.*     animated arena view (listens to structured battle events)
src/Calibrate.* turns observed in-game damage into ability multipliers
src/Golden.*    deterministic battle fingerprints (regression tests / port acceptance tests)
src/Ui.*        terminal detection, text rendering, battle report, interactive controller
src/SelfTest.cpp  formulas, mechanics, statistics, determinism, invariants
data/rules.json   tunable formula constants / conventions
data/library/     the status-effect library (every buff and debuff)  - shared, rarely edited
data/content.json the demo roster, roster-specific auras, teams
data/user/        YOUR units: any .json file here is loaded automatically (see data/user/README.txt)
golden/           battle fingerprints written by `golden --write`
docs/             STATUSES.md, TURN_ACTIONS.md, ADDING_UNITS.md, VALIDATING.md
```

The shape follows what a client analysis showed about the original's design:

| Design element | Here |
|---|---|
| Abilities are lists of **effects** (damage, heal, apply status, dispel, revive, summon, TM / cooldown / ultimate modifiers, …) | `EffectDef`, 11 effect types in `EffectType` |
| Effects carry chance, target spec, apply type (unavoidable / physical / mental), damage type (attack / ability power / unmitigable), multiplier + additive + variance | same fields on `EffectDef` |
| Passives are **triggers on battle events** (start/end of turn, on crit, on dodge, on kill, on death, on status add/remove, on resist, …) | `TriggerDef` + `EventType` (17 events) |
| Leader & unique abilities apply permanent stat auras at battle start | `AuraDef`, applied in the documented order |
| Targeting is its own layer (select mode, side, tags, health state, status filters) | `TargetSpec` |
| Auto-battle AI is a tree of decision nodes + weighted targeting rules | `DecisionNode`, `AiParams` |
| Unit state: health, protection, turn meter, statuses, cooldowns, ultimate charge | `BattleUnit` |

The engine knows nothing about the screen. It emits structured events (`VisEvent`: turn start/end,
ability used, damage with protection/health split and crit flag, heal, evade, resist, status
gained/lost, defeat, revive, turn-meter change, counter/assist) to an optional `BattleObserver`.
The arena is one such observer; a Unity front end would be another. A test checks that the event
stream agrees exactly with the engine's own damage/healing/defeat totals.

Turn flow (`Battle::takeTurn`): heal-over-time tick → damage-over-time tick → start-of-turn
triggers → action → assists → counters / auto-revive → end-of-turn triggers → buff
durations tick → debuff durations tick → cooldown tick → bonus turns queued.

Damage pipeline (`Battle::hit`): evasion check → source damage (stat × multiplier + additive)
→ crit roll → variance → damage-dealt/taken modifiers → armor/resistance mitigation →
damage immunity → apply to protection then health → health steal → events → death/revive →
counter queue.

---------------------------------------------------------------------------------------

## Formulas and how sure I am

| Rule | Implementation | Confidence |
|---|---|---|
| Armor/resistance mitigation | `def / (def + level × 7.5)` (an older constant of 10 appears in outdated threads) | high — several independent sources and worked examples agree |
| Debuff resist | `resist = clamp(max(tenacity − potency, 15%), 15%, 100%)`, one check after the effect's own chance. Tenacity Up/Down are ±9999% | high |
| Crit chance | `crit − crit avoidance`, clamped 0–1; a crit multiplies damage by the Crit Damage stat (base 1.5) | medium-high |
| Evasion | `dodge/deflection − accuracy`, clamped 0–1; evaded hits apply no damage or effects | medium |
| Turn meter | 0–100% bar, continuous fill proportional to speed, natural fill caps at 100%, gains may overflow and the highest meter acts first, exact ties are random, meter resets to 0 when a unit starts its turn | medium-high |
| Damage variance | ±5% uniform per hit | medium (sources say "typically 5%") |
| Damage over time | ticks at turn start; 5%-of-max-health statuses; plague-style DoT ignores protection, ordinary DoT hits protection first | medium |
| Event order within a turn | HoT → DoT → action → assists → reactions (counters, revive) → buff tick → debuff tick | medium (from a community post that itself says the list may be incomplete) |
| Taunt / stealth | stealthed units can't be single-targeted unless all are stealthed; then taunters restrict the choice | medium |
| Armor pen / defense pen | flat armor pen subtracts, defense pen is a fraction; pen applies after the fraction | low-medium |

### Switches for the uncertain conventions (`data/rules.json`)
| Key | Meaning |
|---|---|
| `cooldown_counts_use_turn` | does the turn an ability is used count toward its cooldown? |
| `statuses_tick_on_apply_turn` | does a status landing on a unit during its own turn lose a turn at the end of that turn? |
| `health_steal_includes_protection` | is health steal based on damage to protection too? |
| `variance`, `resist_floor`, `level_constant` | numeric constants |

---------------------------------------------------------------------------------------

## Calibration — how to close the remaining gap

I could not run the original game, so nothing here has been compared against it. Because
the formulas live in `Rules.h` / `rules.json`, calibration is a matter of measuring and
flipping switches. Suggested experiments against the real game (fixed teams, training
dummies or a weak opponent):

1. **Cooldown timing** — use a special with a 3-turn cooldown and write down on which of
   the unit's turns it becomes usable again. Set `cooldown_counts_use_turn` to match.
2. **Self-buff lifetime** — give a unit a "1 turn" buff on its own turn; see whether it is
   still there at the start of its next turn. Set `statuses_tick_on_apply_turn`.
3. **Variance** — hit a dummy 50+ times with a non-crit ability and record min/max damage.
4. **Armor constant** — compare damage dealt to targets with known armor (check against
   `mitigation()` in `Rules.h`).
5. **Health steal base** — attack a target that has protection with a health-steal unit
   and compare the heal with damage dealt to protection vs health.
6. **Crit multiplier / avoidance** — compare crit and non-crit damage; test avoidance.
7. **Turn-meter ties and overflow** — set up identical-speed units and units with a
   turn-meter gain that pushes past 100%.

Each result is either "matches" or a one-line change in `rules.json` / `Rules.h`.
The simulator (`sim`) then tells you how a change moves the win rates.

---------------------------------------------------------------------------------------

## Data format (JSON; `//` comments allowed)

Percent-style values are fractions (`0.25` = 25%). Unknown keys, bad references and
impossible values are rejected at load time with a message naming the offender.

**Status** — `id, name, kind (buff|debuff|neutral), duration (-1 = permanent), max_stacks,
dispellable, mods[{stat, op (add|pct_base), value}], flags[], mode (dot|hot|prot_over_time|
bonus_protection), amount, revive_health, revive_bonus_turn, blocks[]`.
Flags: `taunt stealth stun ability_block no_assist no_counter no_tm_gain healing_immunity
damage_immunity crit_immunity debuff_immunity buff_immunity ignore_protection daze`.
`pct_base` modifiers are fractions of the unit's *base* stat and add together.
Stat keys: `health protection speed physical_damage special_damage armor resistance phys_crit
spec_crit crit_damage potency tenacity accuracy phys_crit_avoid spec_crit_avoid dodge
deflection health_steal armor_pen resistance_pen defense_pen counter_chance
damage_dealt_mult damage_taken_mult`, plus aliases `offense defense crit_chance crit_avoid
evasion`.

**Unit** — `id, name, tags[], level, stats{}, abilities[]` (exactly one `basic`).

**Ability** — `id, name, description, kind (basic|special|leader|unique|ultimate),
pick (enemy|ally|dead_ally|self|none), cooldown, ultimate_charge, effects[], triggers[],
auras[], ai{enemy_rules, ally_rules, nodes}`.

**Effect** — `type` (`damage heal apply_status modify_turn_meter modify_cooldown
modify_ultimate dispel revive bonus_move assist counter`), `target`, `chance` (per target),
`damage_type`, `apply`, `basis`, `multiplier`, `additive`, `variance`, `hits`, `can_crit`,
`force_crit`, `can_resist`, `can_evade`, `protection_multiplier`, `status`, `duration`,
`stacks`, `amount`, `ability`, `dispel`, `count`, `if_target_has_status`,
`if_target_lacks_status`, `if_source_has_status`, `if_source_lacks_status`,
`if_target_health_below`, and `on_hit[]` (nested effects; their default target is the unit just hit).

**Target** — a shorthand string (`selected`, `selected_ally`, `all_enemies`, `all_allies`,
`other_allies`, `self`, `random_enemy`, `random_ally`, `lowest_ally`, `hit_target`,
`event_subject`, `event_other`, `leader_ally`) or an object with `select, side, any_tags,
no_tags, health, exclude_self, exclude_selected, has_status, lacks_status, count,
health_below, health_above`.

**Trigger** — `on, scope (self|ally|any_ally|enemy|any), tags[], ability_kind[], status_kind,
status, chance, once_per_turn, limit, health_below, in_turn, out_of_turn, effects[]`.

**AI decision nodes** — `type` (`single_health median_health number_of_targets
persistent_effect number_of_enemies_alive outnumbered single_survivability single_protection
single_speed_fill target_by_unit_ids`), `group (self|ally|enemy|dead_ally)`, `modifier
(greater_than|less_than|equal_to)`, `param`, `param_str`, `unit_ids`, `chance`, `negate`.
Percent metrics use 0–100. All nodes of an ability must pass for the AI to use it.
Targeting rules: `random lowest_health lowest_health_pct lowest_survivability highest_damage
highest_speed highest_tm has_buffs has_debuffs tag:X status:X no_status:X`.

---------------------------------------------------------------------------------------

## Statuses, turn actions and your own units
* **`docs/STATUSES.md`** - all 123 implemented buffs and debuffs, plus an honest list of the ones that aren't (ship-only effects,
  effects tied to one unit's kit, effects needing mechanics the engine lacks) and why.
* **`docs/TURN_ACTIONS.md`** - every action a unit can take on or around a turn: bonus turns, assists, counters, follow-up
  attacks, turn-meter and cooldown manipulation, revives, survive effects, instant defeat, dispels...
* **`docs/ADDING_UNITS.md`** + **`data/user/`** - enter a unit from the game's stat screen (type percentages exactly as shown, e.g.
  `"potency_pct": 105.2`), with a tested cookbook (`data/user/cookbook.json.txt`) of ready-to-copy ability recipes.
* **Main menu 7 / `calibrate`** - the game never displays an ability's damage multiplier; measure it from the damage numbers
  you see. **`docs/VALIDATING.md`** describes how to compare the simulator with battles you know.

The shipped roster is original demo content. The engine is built so that real units can be entered privately in `data/user/`
from your own numbers instead of being bundled.

## Not implemented yet

Summons / reinforcements and blackboard variables · stack-tier statuses (effects that change at 5/10/15 stacks) · ships and crews · gear, relics, mods and
stat progression (units carry final stats) · speed-based cooldowns · ability upgrade tiers ·
Marked / forced-target debuffs · assist/counter chains beyond one level · multiplayer/netcode ·
anything graphical.

## Sources used for the formulas
* Mechanics reference — https://swgoh.wiki/wiki/Mechanics
* Armor as a function of defense — https://gaming-fans.com/?p=5010
* Buff/debuff list — https://gaming-fans.com/?p=957
* Turn meter and speed — https://forums.ea.com/discussions/swgoh-general-discussion-en/turn-meters-and-speed---math-and-mechanics-/3887835/replies/3887841
* Potency vs tenacity — https://forums.ea.com/discussions/swgoh-strategy-and-tips-en/how-is-debuff-resisting-calculated/4606160/replies/4606170 and https://forums.ea.com/discussions/swgoh-general-discussion-en/how-exactly-does-potency-and-tenacity-work/4385160
* Critical avoidance — https://forums.ea.com/discussions/swgoh-strategy-and-tips-en/what-exactly-does-critical-avoidance-do/3691888
* Order of events in a turn — https://forums.ea.com/discussions/swgoh-technical-issues-en/the-big-problem-with-buffs--debuffs-in-the-game/10889886
* Duration quirk report — https://forums.galaxy-of-heroes.starwars.ea.com/discussion/7403/bug-counter-applied-debuffs-lose-a-turn-of-duration
