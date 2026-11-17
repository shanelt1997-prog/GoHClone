YOUR OWN UNITS GO HERE
======================

Any file ending in .json in this folder is loaded automatically, after the shipped data, every time the
program starts. You can add units, teams and even new status effects here without touching the shipped
files. Each id must be unique across ALL files; a clash is reported with the file name.

To try it:
  1. Copy example_unit.json.txt to my_units.json   (the .txt ending keeps the example switched off)
  2. Edit the stats and abilities, then run the program: your unit appears in the team builder.
  3. Run `GoHClone selftest`; it also checks that everything in this folder loads and can fight.

How to fill in a unit from the game:
  * Stats: type them as the game's stat screen shows them. A key ending in _pct means "percent":
        "potency_pct": 105.2     -> 105.2%
        "crit_damage_pct": 187.5 -> a crit deals 187.5% damage
        "armor_pct": 41.27       -> the damage-reduction % shown for Armor (converted to the flat rating)
    Plain keys use raw numbers (health, protection, speed, physical_damage, special_damage).
  * Abilities: describe each ability as a list of effects. docs/ADDING_UNITS.md has a recipe for every
    common pattern (damage, debuff chances, heals, turn meter, assists, bonus turns, follow-up attacks...).
  * Damage multipliers are NOT shown in the game. Measure them with the calibration helper
    (main menu option 7, or `GoHClone calibrate`), then paste the result into "multiplier".
  * Every buff and debuff already exists: run `GoHClone statuses` to see the ids to use.

Keep these files private if they describe a commercial game's characters.
