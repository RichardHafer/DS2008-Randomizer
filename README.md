<p align="center">
  <img src="assets/DS2008Randomizer_Banner.png" alt="Dead Space 2008 Randomizer" width="70%">
</p>

# Dead Space 2008 Randomizer

A randomizer for the PC version of the original Dead Space (2008). Any enemy
spawn in the campaign can become a different necromorph, built by the game
from its own data, in the spot and through the vent the level authored, with
the room's scripting still working. On top of that: shuffled loot, crates,
loose items, audio logs, schematics, the first weapon, the starting suit, the
chapter order and more.

Every choice comes from the seed. The same seed with the same settings gives
the same run, also after a death, a checkpoint restart or loading a save.

Nothing in the game's files is changed. The plugin is loaded by the Ultimate
ASI Loader and everything happens in memory while you play.

## What it does

**On by default**

| | |
|---|---|
| Enemy replacement | Slashers, Lurkers, Leapers, Exploders, Infectors, Pregnants, Swarmers, Brutes, Dividers and their enhanced forms take part. Any chapter can throw any of them at you. Seeded: the same `EnemyRandomizerSeed` gives the same run. |
| Cross-family swaps | A spawn may become a different species, not just another look of the same one. Vent spawns keep their vent and arrival, direct spawns stay direct. |
| Difficulty ramp | Enhanced variants, Twitchers and the Hunter become more common as the campaign goes on, capped at `EnemyRandomizerLateCapPercent` of a family. |
| Scripted rooms | Rooms that wait for their enemies to die still open: the replacement carries the death message the room counts. |
| Watchdog | A replacement that never arrives or never dies is reported in the log and released after a leash, so a room cannot lock you in. |
| Leg length correction | Enemies on another species' rig keep their feet on the floor. |
| Store fast scroll | PageUp/PageDown (or LT/RT) jump a page in the store, Ctrl+PageUp/PageDown to the first or last item. |
| Engine limits | More sound bank slots for the extra enemy voices (`SoundBankSlots`), plus two crash guards for engine paths the extra enemies reach. |
| Loot | Drop tables give other items with other weights, may pick from the whole game's item set, and the seed sets how scarce drops are for the run. Ammo and credit pickups scale with the enemy counts (`LootAutoScale`). |
| Crates | Fixed crate rewards move between compatible crates of the same chapter; `CratePowerNodePercent` sets how many Power Nodes they hold. |
| Loose items | Items lying in the levels become other items. |
| Logs | Every collectable audio, text and video log grants another log. |
| Schematics | Which schematic gives what. `ProgressionStoreGate` additionally keeps store items in their chapter. |
| First weapon | The Plasma Cutter at the start becomes another weapon, with matching ammo. |
| Chapter order | Chapters 2 to 11 in a seeded order; chapter 1 stays first and chapter 12 last. |
| Oxygen | One air budget for vacuum, picked by the seed. |

**Off by default, turn on in the INI or in the settings window**

| | |
|---|---|
| `LootAmmoPercent`, `LootCreditsPercent` | Ammo and credit pickups in fixed percent of what the game authored (with `LootAutoScale=0`). |
| `StartingSuitRandomizer` | Start in a random suit and pay for it from future credit income (`StartingSuitCreditShare`). |
| `UpgradeBenchRandomizer` | Which upgrade nodes each bench offers (experimental). |
| `LimbWeakSpotRandomizer`, `LimbProfilePool`, `LimbSeverCostRandomizer` | Move where an enemy's weak limb is and what cutting each limb is worth. |
| `PlayingDeadCorpses` | Some corpses are necromorphs playing dead. |
| `GodMode`, `DamageMultiplier` | Practice helpers, also as toggle keys. |

Every patch site is checked against the Steam bytes before it is placed, and
the log says which ones went in. An unsupported build is left alone.

## Install

### Windows

**[Download the latest release](https://github.com/RichardHafer/DS2008-Randomizer/releases/latest)**

Made for the Steam release of Dead Space (version 1.0.0.222).

Close the game, open the game folder (Steam: right-click Dead Space > Manage >
Browse local files, the folder with `Dead Space.exe`) and extract the archive
into it. The installed files should look like this:

```text
Dead Space/
|-- winmm.dll                               (Ultimate ASI Loader)
|-- DeadSpace2008Randomizer.ini
|-- DeadSpace2008RandomizerConfig.exe
|-- DeadSpace1Progression.tsv
|-- DeadSpace1Progression.store.tsv
|-- plugins/
|   `-- DeadSpace2008Randomizer.asi
`-- ds1_randomizer_assets/
    `-- *.str                               (enemy pool archives)
```

If the folder already has a `winmm.dll` from another mod, keep that one; it is
most likely the same loader.

Start the game. The main menu shows "Dead Space 2008 Randomizer v1.0.0" next
to the game version when the plugin is running.

Settings live in `DeadSpace2008Randomizer.ini`, and
`DeadSpace2008RandomizerConfig.exe` edits it for you. Pick a seed and the
modules you want, save, then start a **new** game. Changing settings in the
middle of a playthrough mixes two runs.

Loading screens are a little longer than usual. The plugin keeps every enemy
template resident so any enemy can appear anywhere, and it holds the loading
screen until that pool is ready. That is on purpose.

Works next to DeadSpaceFixes. To uninstall, delete
`plugins\DeadSpace2008Randomizer.asi` (and `winmm.dll` if no other mod uses
it). Saves stay playable.

The plugin writes `DeadSpace2008Randomizer.log` into the game folder while the
game runs. If something goes wrong, send that file (and
`DeadSpace2008Randomizer.prev.log` after a crash) with the seed and your
settings file. After a crash the plugin also writes a `.dmp` file; it is
large, so only send it when asked.

---

## How the bigger pieces work

Not required reading. This is here so the next person does not have to redo
the research. The comments in the source carry the evidence: addresses,
dumps, and the runs that proved or disproved an idea.

### The enemy pool

The original game only has the enemies of the current chapter in memory. The
plugin ships its own archives, built offline from the game's data, with every
regular enemy record, its character package and its voice banks. They are
registered as streams of their own, loaded once and pinned, which is why any
enemy can appear in any chapter and why the loading screen waits for them.

### Replacing an enemy

A spawner hands the engine a template to build. The plugin swaps that template
for one from the pool, so the factory builds the replacement the way it builds
the original: same transform, same group, same stream ownership. How an enemy
arrives is part of the spawning component (vent, ceiling, direct), so a donor
is only accepted when its arrival fits the spot; pairs that cannot be matched
are refused rather than guessed.

### Keeping the scripting alive

A scripted room does not count corpses, it counts death messages, and every
authored enemy carries the name of the message its room waits for. A
replacement from another chapter carries its own. The plugin writes the
source's message into the replacement, and the watchdog says it for the
enemy when that is impossible or the enemy never arrives.

### Loot, crates and the store

Drop lists are sixteen weighted entries plus a weight for "nothing", read and
rewritten in place, so scarcity and the global pool use the game's own
selection. The store reads a single kiosk list, and the chapter gate decides
per item when it unlocks, so randomized schematics cannot hand out late-game
items in chapter one.

### Upgrade benches

The bench tree is decoded by one function for all twelve node kinds and the
22 benches of the game. The shuffle rotates which nodes a bench offers while
the grid and the save data stay as authored.

---

## Planned

- More enemy variants
- Vacuum rooms picked by the seed
- A console DLC add-on (suits and weapons) as a separate plugin

## Building

Visual Studio 2022 with the x86 toolchain, then `build.bat` for the plugin and
`build_configurator.cmd` for the settings window (.NET Framework 4 C#
compiler, part of Windows). safetyhook needs `std::expected`, hence
`/std:c++latest`.

`tools\make_release.py --version 1.0.0` builds the release zip. It needs the
enemy pool archives and the Ultimate ASI Loader's `winmm.dll`, which are not
part of the repository: the pool is game-derived and ships with the release,
not with the code.

The `src/*_generated.*` tables are built from extracted game data that is not
in the repository; they are committed, so the plugin builds without it.

Addresses are specific to the Steam build. Signatures are matched at runtime,
so an unsupported build fails safely and reports what it could not find.

## Credits

- [safetyhook](https://github.com/cursey/safetyhook) and [Zydis](https://github.com/zyantific/zydis) for hooking
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG for loading the plugin
- Everyone who played test runs and sent logs

## Licence

MIT, see `LICENSE`. Not affiliated with EA or Visceral Games.
