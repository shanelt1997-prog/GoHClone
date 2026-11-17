# Turn actions

Everything a unit can do on or around a turn, and the data-file piece that produces it.

## What happens in a turn (in order)
1. Heal-over-time ticks, then damage-over-time ticks (a unit can be defeated by its own DoT).
2. "Start of turn" triggers.
3. The action: the chosen ability (skipped if the unit is stunned or feared).
4. Assists (out-of-turn attacks by allies), then counters.
5. "End of turn" triggers, then buff durations tick, then debuff durations tick, then cooldowns tick.
6. Bonus turns that were earned are taken next, before the turn meter is consulted again.

## Actions available to a unit
| Action | In data | Notes |
|---|---|---|
| Basic attack | `kind: basic` | exactly one per unit, no cooldown |
| Special | `kind: special`, `cooldown` | blocked by Ability Block |
| Ultimate | `kind: ultimate`, `ultimate_charge` | needs charge; `modify_ultimate` effects fill it |
| Skip a turn | status flag `stun` | Stun and Fear; the unit also cannot evade |
| **Bonus turn** | `bonus_move` | a full extra turn straight after this one |
| **Assist** | `assist` | allies attack out of turn with their basic; `multiplier` scales the damage |
| **Counterattack** | `counter_chance` stat, or `counter` effect | once per attacker per action; counters never trigger counters |
| **Follow-up / bonus attack** | `use_ability` | the unit uses any of its own abilities again (`out_of_turn`, `multiplier` = damage scale) |
| Turn meter gain / loss | `modify_turn_meter` | loss is resisted by tenacity and can be evaded; Daze / Shock block gains; some buffs block loss |
| Cooldown change | `modify_cooldown` | `-99` resets; positive numbers add turns |
| Ultimate charge | `modify_ultimate` | |
| Revive an ally | `revive` | not if the unit has a "cannot be revived" effect |
| Revive on defeat | status with `revive_health` | Backup Plan, Hatred... |
| Survive a lethal hit | status with `survive` | Endure, Second Wind |
| Instant defeat | `defeat` | blocked by Endure / Instant Defeat Immunity |
| Dispel | `dispel` (`buff`, `debuff`, `all`, `count`) | enemy dispels can be resisted; permanent effects can't be dispelled |
| Remove stacks | `remove_status` (`count`) | |
| Heal / recover protection | `heal` (`multiplier`, `protection_multiplier`, `basis`) | |
| Damage | `damage` | physical / special / true (`unmitigable`); `hits`, `basis`, `ignore_protection`, `cannot_defeat`, `can_evade`, `can_crit`, `force_crit` |
| Conditional effects | `if_target_has_status`, `if_target_lacks_status`, `if_source_has_status`, `if_target_health_below` | |
| Reactions | `triggers` on abilities **and on statuses** | 17 events: start/end of turn, ability used, attack, damage received, crit, dodge, kill, death, status gained / lost, resisted, heal received... |

## Not implemented
Summoning / reinforcements, swapping positions, ship actions, stack-tier bonuses (effects that change at 5, 10, 15 stacks),
copying or transferring effects between units, and reacting to *another* unit's turn-meter gains.
