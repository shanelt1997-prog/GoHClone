# SWGOH Comlink data sync

The Windows desktop application can download the current SWGOH game-data segment through a running SWGOH Comlink service.

## Start Comlink

Comlink is a separate service; GoHClone does not bundle or host it. Run your own Comlink instance using the official instructions:

- https://github.com/swgoh-utils/swgoh-comlink
- https://github-wiki-see.page/m/swgoh-utils/swgoh-comlink/wiki/Getting-Started

The default address expected by GoHClone is `http://localhost:3000`.

If your Comlink instance uses another address, set the Windows environment variable `GOHCLONE_COMLINK_URL` to its base URL (for example, `http://127.0.0.1:3000` or your own hosted endpoint), then restart GoHClone.

## Download the game data

Choose **DATA → Sync SWGOH game data (Comlink)**. GoHClone asks for confirmation, reads the current game-data version from `/metadata`, and requests `/data` with `requestSegment: 2` (ability definitions) and `requestSegment: 3` (unit definitions), both with `includePveUnits: false`. It also requests the English (US) localization bundle. The three raw responses are saved as:

- `data/comlink/game-data-segment2.json`
- `data/comlink/game-data-segment3.json`
- `data/comlink/localization-eng-us.json`

The download is deliberately cached separately from `data/content.json`; it does not overwrite your existing demo roster or change the battle engine's content.

## Import limitations

Comlink exposes raw game definitions and localization data, not ready-made GoHClone units. It does not provide final calculated character stats in the game-data response. A proper roster entry also needs the character's progression (level, stars, gear, relics and mods) to calculate final stats, and the character's ability definitions must be translated into GoHClone's effect/trigger schema. This first integration stage downloads and preserves the official unit/ability source data; it does not claim to automatically convert every character into a battle-ready unit.

For stat calculation, the SWGOH community maintains a separate calculator and data tooling. See https://github.com/swgoh-utils/swgoh-comlink and https://github.com/swgoh-utils/comlink-python for current ecosystem details.
