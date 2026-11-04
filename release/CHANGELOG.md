# Dead Space 2008 Randomizer 1.0.4

Changes since 1.0.3:

- Remove dead spawned enemies that remain as invisible collision after failing to become ragdolls. `DeadBodySweep=1` enables this fix. It waits ten seconds, checks the object's identity and active state, and removes at most four actors per scan. Authored corpses, the Hunter, Dividers, Guardians and Swarmers are excluded. Cleanup stops if the stream teardown hook is unavailable.
- Add the F9 cheats page (`CheatsMenu=1`): give weapons and ammunition, select an installed suit, toggle noclip and god mode, or change the damage multiplier. Use TAB to change pages, arrow keys to select, and ENTER to apply. Research controls remain behind `DebugCheats`.
- List installed Console DLC weapons and suits by name. Weapon and suit requests run on the game thread, with delivery and failure diagnostics in the log. The first-weapon randomizer no longer changes a Plasma Cutter requested through this menu.
- Recognize the prototype Scorpion and Tank Pulse Rifles when choosing ammunition in the F9 menu.
- Keep noclip movement idle while the F9 overlay uses the arrow keys.

God mode, noclip and extra damage remain off in the included settings. The existing seed and randomizer settings are preserved. The Flier, cut-content experiments and camera tuning are not included.

## Update

Close the game and extract the archive into the folder containing `Dead Space.exe`. Back up your INI before replacing it. An existing INI keeps its settings; `CheatsMenu` and `DeadBodySweep` default to 1 if absent. Set either to 0 to disable it. The F9 page also requires `CheckpointChanger=1`.

Made for the Steam build 1.0.0.222. The Console DLC add-on is optional; version 1.0.2 includes the corrected coloured stasis bar.
