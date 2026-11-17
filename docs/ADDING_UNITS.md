# Adding your own units

Put a `.json` file in `data/user/` (copy `example_unit.json.txt` to start). It is loaded automatically after the shipped data.
Mistakes are reported with the file name and the unit / ability involved, e.g. `unit 'x'/ability 'y': unknown status 'z'`.

## 1. Stats: type what the game shows
Keys ending in `_pct` mean "the percentage number the game shows". Other keys are plain numbers.

| Stat screen | Key | Example |
|---|---|---|
| Health / Protection / Speed | `health`, `protection`, `speed` | `41250` |
| Physical Damage / Special Damage | `physical_damage`, `special_damage` | `9120` |
| Armor (the % shown) | `armor_pct` | `41.27` (converted to the engine's flat rating) |
| Resistance (the % shown) | `resistance_pct` | `28.5` |
| Physical / Special Critical Chance | `phys_crit_pct`, `spec_crit_pct` | `62.5` |
| Critical Damage | `crit_damage_pct` | `187.5` |
| Potency / Tenacity | `potency_pct`, `tenacity_pct` | `105.2` |
| Accuracy | `accuracy_pct` | `12` (the engine has one accuracy stat) |
| Critical Avoidance | `crit_avoid_pct` (or `phys_crit_avoid_pct`, `spec_crit_avoid_pct`) | `18` |
| Dodge / Deflection | `dodge_pct`, `deflection_pct` | `8` |
| Health Steal | `health_steal_pct` | `15` |
| Armor / Resistance Penetration (flat) | `armor_pen`, `resistance_pen` | `120` |

Also set `"level"` (default 85) and `"tags"` (factions, roles: anything your abilities refer to).

## 2. Abilities
Each ability has `id`, `name`, `kind` and a list of `effects`.

* `kind`: `basic` (exactly one per unit), `special` (+ `cooldown`), `ultimate` (+ `ultimate_charge`), `leader`, `unique` (the last two are passives).
* `pick` says what the player must choose: `enemy`, `ally`, `dead_ally`, `self`, `none` (area / team abilities).
* An effect picks who it hits with `target`: `selected`, `selected_ally`, `all_enemies`, `all_allies`, `other_allies`, `self`,
  `random_enemy`, `lowest_ally`, `hit_target` (the unit a damage effect just hit, the default inside `on_hit`), `event_other`... or an
  object with `side`, `any_tags`, `health_below`, `has_status` and more (see README "Data format").
* `chance` (0-1) is rolled per target. `can_resist: false` makes an effect unresistable. `duration` is in the target's turns.
* **Every effect type, trigger event and status is listed in `docs/TURN_ACTIONS.md` and `docs/STATUSES.md`.**
* `data/user/cookbook.json.txt` has a working example of each of these patterns: single / special / area damage, debuff chances,
  heals, team buffs, self buffs, turn-meter control, cooldown resets, bonus attacks, assists, true damage, conditional bonuses,
  revives, executes, ultimates, passives, start-of-battle effects, leader auras.

## 3. Translating an in-game ability description
| The game says | Write |
|---|---|
| "Deal Physical damage to target enemy" | `{"type":"damage","damage_type":"attack_damage","multiplier": ?}` |
| "Deal Special damage to all enemies" | `damage_type: ability_power`, `"target":"all_enemies"` |
| "... with a 50% chance to inflict Bleed for 2 turns" | `"on_hit":[{"type":"apply_status","status":"bleed","chance":0.5,"duration":2}]` |
| "All allies gain Offense Up for 2 turns" | `apply_status`, `"target":"all_allies"`, `"duration":2` |
| "Reduce the target's Turn Meter by 50%" | `{"type":"modify_turn_meter","amount":-0.5,"target":"selected"}` |
| "Gain a bonus turn" | `{"type":"bonus_move","target":"self"}` |
| "Ally X assists" | `{"type":"assist","target":{...the allies...}}` |
| "Whenever this unit is damaged, ..." | a `unique` ability with `"triggers":[{"on":"receive_damage","scope":"self","effects":[...]}]` |
| "Allies with tag T gain ... for the whole battle" (leader) | `kind: leader`, `"auras":[{"target":{"select":"all","side":"ally","any_tags":["T"]},"status":"..."}]` |
| "Cooldown: 3" | `"cooldown": 3` |

A leader ability only works while its unit is the **first** unit of the team.

## 4. The hidden multiplier
The game never shows an ability's damage multiplier. Hit a target, note the damage, and run the calibration helper (main menu 7):

    GoHClone calibrate --stat 11500 --defense-pct 41.27 --damage 17120,17400,16900

It prints the multiplier (and the range that fits every hit); paste it into `"multiplier"`. Use plain hits, unchanged stats.

## 5. Check your work
* `GoHClone show <unit id>` prints the unit as the engine understood it (stats converted, abilities listed).
* `GoHClone selftest` re-validates every file in `data/user/`.
* `GoHClone sim --a <your units> --b <other units>` runs a thousand battles; see `docs/VALIDATING.md` to compare with the real game.
