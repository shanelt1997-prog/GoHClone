# Checking the simulator against the real game

The simulator can only be as accurate as the numbers you give it. A practical way to test it:

1. **Pick a battle you know well** (same two squads, same gear, ideally against the AI so nothing is human-random).
2. **Enter every unit** in `data/user/` from the stat screens (see `docs/ADDING_UNITS.md`). Use the `_pct` keys so you can type exactly what the game shows.
3. **Measure the damage multipliers** you don't know with the calibration helper (main menu 7). Take 3-5 plain hits of each ability
   at unchanged stats; the helper gives a range that narrows with every hit.
4. **Compare the first turns**, which are the most deterministic part: the turn order, who acts first, who gets which buff.
   If the order differs, check Speed values and turn-meter effects first.
5. **Compare outcomes statistically.** One battle is luck (crits, dodges, resists). Run `sim` with a few thousand battles and
   compare the win rate, the average number of turns, and who tends to die first with your experience of the real fights.
6. **If something is consistently off**, suspect one of the switches in `data/rules.json`
   (`cooldown_counts_use_turn`, `statuses_tick_on_apply_turn`, `health_steal_includes_protection`) and flip it. Each is a
   yes/no question the game can answer with a single observation; see the README "Calibration" list.
7. Use `--seed` to replay a battle exactly while you investigate.

What the simulator cannot know: hidden multipliers (you measure them), per-unit special cases that aren't in the status library
(`docs/STATUSES.md` lists them), and anything that depends on a server (rewards, matchmaking).
