# Status effects

Generated from `data/library/statuses.json` (the numbers come from the community wiki lists of buffs and debuffs).
**65 buffs and 58 debuffs** are implemented. Use the **id** in data files
(`"status": "offense_up"`). Run `GoHClone statuses` for a live listing, or `GoHClone statuses stun` to search.

Durations come from the ability that applies the effect (`"duration": 2`), not from the status.
Percent modifiers (`+50% offense`) scale the unit's *base* stat and add together.

## Buffs

| id | name | what it does |
|---|---|---|
| `accuracy_up` | Accuracy Up | +15% accuracy |
| `advantage` | Advantage | the next attack is a critical hit (consumed) |
| `crit_avoid_up` | Critical Avoidance Up | +50% critical avoidance |
| `crit_chance_up` | Critical Chance Up | +25% critical chance |
| `crit_damage_up` | Critical Damage Up | +50% critical damage |
| `crit_immunity` | Critical Hit Immunity | cannot be critically hit |
| `damage_immunity` | Damage Immunity | prevents all damage |
| `defense_pen_up` | Defense Penetration Up | +150 armor and resistance penetration |
| `defense_up` | Defense Up | +50% defense |
| `endure` | Endure | cannot be instantly defeated; the first time lethal damage would land, one stack is used up and the unit survives at 1% health (+1% recovered) |
| `evasion_up` | Evasion Up | +15% dodge chance |
| `foresight` | Foresight | evades the next attack (if able; consumed) |
| `frenzy` | Frenzy | gains 100% turn meter whenever an ally uses a special ability |
| `health_steal_up` | Health Steal Up | +50% health steal |
| `health_up` | Health Up | +15% max health |
| `heal_over_time` | Heal Over Time | recovers a variable share of max health at the start of each turn |
| `protection_over_time` | Protection Over Time | recovers a variable share of max health as protection each turn |
| `protection_up` | Protection Up | bonus protection equal to a share of max health for the duration |
| `bonus_protection` | Bonus Protection | variable bonus protection for the rest of the battle *(cannot be dispelled)* |
| `instant_defeat_immunity` | Instant Defeat Immunity | cannot be instantly defeated *(cannot be dispelled)* |
| `offense_up` | Offense Up | +50% offense |
| `potency_up` | Potency Up | +50% potency |
| `resilient_defense` | Resilient Defense | enemies must target this unit; loses one stack each time it is damaged by an attack |
| `retaliate` | Retaliate | when damaged by an attack, deals 20% of the damage received (per stack) back to the attacker as true damage that cannot defeat |
| `retribution` | Retribution | +100% counter chance |
| `riposte` | Riposte | out-of-turn attacks ignore protection |
| `second_wind` | Second Wind | the first time lethal damage would land, the unit instead survives at 1% health, recovers 50% health and protection, and loses this buff |
| `speed_up` | Speed Up | +25% speed |
| `stealth` | Stealth | cannot be targeted directly while other allies are visible |
| `taunt` | Taunt | enemies must target this unit |
| `tenacity_up` | Tenacity Up | resists negative status effects |
| `debuff_immunity` | Debuff Immunity | cannot gain debuffs (engine extra, not in the wiki list) |
| `alert` | Alert | assists whenever an ally uses a special ability (the bonus-turn-meter half of the original is not modelled) |
| `backup_plan` | Backup Plan | recovers 10% health per turn; when defeated, revives with 80% health and 30% turn meter; cannot be dispelled *(cannot be dispelled)* |
| `blessed` | Blessed | +20% max health and offense; immune to instant defeat, stun and turn meter reduction (the assist half is not modelled) *(cannot be dispelled)* |
| `bounty_hunters_resolve` | Bounty Hunter's Resolve | ignores taunt; revives with 100% health when defeated *(cannot be dispelled)* |
| `bulwark` | Bulwark | +150% defense per stack; loses one stack when damaged *(cannot be dispelled)* |
| `call_to_action` | Call to Action | ignores taunt; +50% accuracy, critical chance and critical damage *(cannot be dispelled)* |
| `chiewab_medpac` | Chiewab Medpac | recovers 5% health and protection at the start of every unit's turn *(cannot be dispelled)* |
| `cortosis_armor` | Cortosis Armor | an enemy that damages this unit loses all its protection and 25% speed for 1 turn *(cannot be dispelled)* |
| `deep_cover` | Deep Cover | +10% defense penetration and potency per stack |
| `deflective_ward` | Deflective Ward | every hit is reduced to 1 damage *(cannot be dispelled)* |
| `dramatic_entrance` | Dramatic Entrance | +25% offense; attacks deal bonus true damage equal to 10% of the target's max health (cannot be evaded); defeating an enemy grants 20% turn meter and refreshes this buff *(cannot be dispelled)* |
| `force_connection` | Force Connection | +2% defense and offense per stack *(cannot be dispelled)* |
| `hatred` | Hatred | +100% offense and defense, +25% critical chance; when defeated, revives with 100% health and 100% turn meter *(cannot be dispelled)* |
| `high_ground` | High Ground | +35% damage with out-of-turn attacks, +100% counter chance, -35% speed (the ultimate-charge block is not modelled) *(cannot be dispelled)* |
| `hired_muscle` | Hired Muscle | +30% tenacity; immune to daze and fear |
| `jedis_will` | Jedi's Will | +100% counter chance, +25% offense, +25% speed *(cannot be dispelled)* |
| `keen_stratagem` | Keen Stratagem | +25% offense and potency *(cannot be dispelled)* |
| `lifeblood` | Lifeblood | takes 30% less damage and deals 30% more *(cannot be dispelled)* |
| `loyal_hand` | Loyal Hand | +10% max protection and offense per stack (max 20) *(cannot be dispelled)* |
| `masters_training` | Master's Training | +25% accuracy, defense, offense, potency and tenacity (the Jedi doubling is not modelled) *(cannot be dispelled)* |
| `merciless` | Merciless | +50% offense, +25% critical chance, +50% critical damage; immune to fear and turn meter reduction |
| `morais_wisdom` | Morai's Wisdom | +15 speed, takes 10% less damage, immune to turn meter reduction *(cannot be dispelled)* |
| `my_kind_of_scum` | My Kind of Scum | +10% max health and defense per stack *(cannot be dispelled)* |
| `outmaneuver` | Outmaneuver | +25% evasion; attacks cannot be countered; cannot be targeted while other allies are visible |
| `overcharge` | Overcharge | +20% offense and defense per stack (max 5) |
| `perfect_defense` | Perfect Defense | +200% defense; resists negative effects; immune to Defense Up and Tenacity Up |
| `queens_protection` | Queen's Protection | per stack: +3% max health and protection, +5% potency and tenacity, +5 speed *(cannot be dispelled)* |
| `ransom` | Ransom | +2% max health and tenacity per stack *(cannot be dispelled)* |
| `spare_parts` | Spare Parts | +30% critical chance and offense; when defeated, revives with 100% health (the follow-up buffs are not modelled) *(cannot be dispelled)* |
| `target_practice` | Target Practice | per stack: +5% critical chance, +5% offense, +10% critical damage *(cannot be dispelled)* |
| `tactical_supremacy` | Tactical Supremacy | +30% critical damage and potency |
| `the_emperors_trap` | The Emperor's Trap | +6% offense and potency per stack *(cannot be dispelled)* |
| `trusted_agent` | Trusted Agent | +30% defense penetration and offense; attacks again after performing a basic attack *(cannot be dispelled)* |

## Debuffs

| id | name | what it does |
|---|---|---|
| `ability_block` | Ability Block | special abilities are unusable |
| `accuracy_down` | Accuracy Down | -15% accuracy |
| `blind` | Blind | misses the next attack (consumed) |
| `breach` | Breach | -25% speed and defense; does not stack with Speed Down or Defense Down |
| `buff_immunity` | Buff Immunity | cannot gain buffs |
| `buff_disruption` | Buff Disruption | immune to buffs until damaged by an attack; once damaged, loses 100% turn meter |
| `burning` | Burning | loses 15% max health per turn and cannot evade; does not stack |
| `crit_chance_down` | Critical Chance Down | -25% critical chance |
| `crit_damage_down` | Critical Damage Down | -50% critical damage |
| `damage_over_time` | Damage Over Time | loses 5% max health at the start of each turn |
| `daze` | Daze | cannot assist, counterattack or gain bonus turn meter |
| `deathmark` | Deathmark | enemies must target this unit; loses 50% max health when damaged by an attack; cannot be revived |
| `defense_down` | Defense Down | -50% defense |
| `defense_pen_down` | Defense Penetration Down | -150 armor and resistance penetration |
| `distracted` | Distracted | cannot gain bonus turn meter |
| `doubt` | Doubt | cannot gain bonus turn meter or buffs, or recover protection |
| `evasion_down` | Evasion Down | -25% dodge chance |
| `expose` | Expose | loses 20% max health when damaged by an attack |
| `fear` | Fear | misses its next turn, cannot evade, and its cooldowns rise by 1; ends when damaged by an attack *(cannot be dispelled)* |
| `healing_immunity` | Healing Immunity | cannot regain health or protection |
| `health_down` | Health Down | -20% max health |
| `health_steal_down` | Health Steal Down | -50% health steal |
| `marked` | Marked | enemies must target this unit |
| `off_balance` | Off Balance | loses 10% health and 10% turn meter when damaged by an attack |
| `offense_down` | Offense Down | -50% offense |
| `plague` | Plague | loses 5% max health per stack at the start of each turn, ignoring protection (the on-infliction tick and the heal-to-full removal are not modelled) |
| `potency_down` | Potency Down | -50% potency |
| `shock` | Shock | cannot heal, gain buffs, or gain bonus turn meter |
| `speed_down` | Speed Down | -25% speed |
| `stagger` | Stagger | loses 100% turn meter when damaged by an attack |
| `stun` | Stun | misses a turn and cannot avoid attacks |
| `tenacity_down` | Tenacity Down | minimizes the chance to resist negative effects |
| `undermine` | Undermine | +25 speed, -25% critical avoidance and critical chance, cannot gain bonus turn meter |
| `vulnerable` | Vulnerable | will be critically hit if possible |
| `ambushed` | Ambushed | -50 speed; protection is disabled and cannot be regained *(cannot be dispelled)* |
| `anguish` | Anguish | loses 2% max health per stack at the start of its turn (cannot be defeated by it); the offense gain is not modelled |
| `bleed` | Bleed | -5% speed and tenacity per stack and loses 5% max health per stack each turn; one stack is removed whenever health is recovered *(cannot be dispelled)* |
| `captive` | Captive | speed set to 0; cannot gain bonus turn meter *(cannot be dispelled)* |
| `decay` | Decay | -10% max health per stack; cannot health steal *(cannot be dispelled)* |
| `demoralized` | Demoralized | -50% offense, -25% critical chance and critical damage *(cannot be dispelled)* |
| `disarm` | Disarm | -50% critical damage and offense (does not stack with those debuffs); gains Damage Over Time (unresistable) whenever it uses a basic ability *(cannot be dispelled)* |
| `doomed` | Doomed | protection is disabled; loses 20% max health at the end of each of its turns; cannot be revived *(cannot be dispelled)* |
| `edge_of_madness` | Edge of Madness | -2% defense per stack *(cannot be dispelled)* |
| `exile` | Exile | cannot attack out of turn or gain buffs during another unit's turn *(cannot be dispelled)* |
| `ferocity` | Ferocity | per stack: -15% defense and tenacity, +8% offense and potency |
| `force_influence` | Force Influence | cannot assist, counterattack or gain buffs |
| `fracture` | Fracture | speed set to 0; cannot gain buffs or turn meter *(cannot be dispelled)* |
| `low_morale` | Low Morale | -25% defense, health, offense and speed *(cannot be dispelled)* |
| `order_66` | Order 66 | immune to buffs and cannot be revived if defeated *(cannot be dispelled)* |
| `overconfident` | Overconfident | -20% defense, +50% offense, +10% speed, +15% evasion |
| `pinned` | Pinned | -25% speed and cannot gain bonus turn meter; does not stack with Breach or Speed Down *(cannot be dispelled)* |
| `plasma_residue` | Plasma Residue | loses all evasion |
| `protection_disruption` | Protection Disruption | protection is disabled; cannot gain protection *(cannot be dispelled)* |
| `provoked` | Provoked | +100% counter chance; deals 90% less damage out of turn and takes 20% max health (cannot defeat) whenever it attacks out of turn |
| `shaken` | Shaken | -10% accuracy per stack *(cannot be dispelled)* |
| `torture` | Torture | takes 10% max health when damaged by an attack and loses 10% defense (stacking to 50%) for the rest of the battle; cannot gain bonus turn meter *(cannot be dispelled)* |
| `torture_scar` | Torture Scar | -10% defense per stack (max 5), permanent (internal effect of Torture) *(cannot be dispelled)* |
| `useful_pawn` | Useful Pawn | -5% critical chance, critical damage and offense *(cannot be dispelled)* |

## Not implemented, and why

**Ship effects** (ships aren't modelled): Afterburner, Breach Immunity, Buzz Droids, Chaff, Concussion Mine, Deflector Shield,
Formation, Hunted, Master Plan, Pilot's Resolve, Plan 99, Reinforced Determination, Reinforced Hull, Ruthless,
Shield Disruption, Stable Bulwark, Stranded, Suprosa Supercomputer, Suspense, Target Lock, The Bad Batch,
Thrust Reversal, Unending Loyalty, Valor.

**Needs a mechanic the engine doesn't have yet** (stack-tier bonuses, summons, shields, per-turn bookkeeping):
Ambush, Beskar Armor, Blight, Confuse, Dark Infusion, Frustration, Heat, Overheat, Pain, Purge, Shatterpoint, Shield Up,
Translation / Corrupted Translation, Tenuous Agreement, Revealed, Jedi Knight / Jedi Legacy / Jedi Lessons.

**Bound to one specific unit's kit** (they only make sense together with that unit's other abilities; add them in your
own `user/` file when you add that unit): Baktoid Shield Generator, BlasTech Weapon Mod, Deceived, Explosive Trap,
Favorable Terms, Fearless, Inevitable Failure, Legendary / Corrupted Battle Meditation, Madness Manifested, Mand'alor,
Mechanic's Savvy, Secret Intel, Shien, Spark of Rebellion, Tactical Genius.

**Partly modelled** (the main effect works; a secondary clause is skipped, as the description says): Alert, Anguish,
Blessed, Dramatic Entrance, Hatred (no extra clauses), High Ground, Plague, Spare Parts.

Any of these can be built in a `user/` file from the same pieces (stat modifiers, flags, triggers) -- see
`docs/ADDING_UNITS.md` -- or ask for the missing engine feature.
