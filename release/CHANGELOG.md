# Dead Space 2008 Randomizer 1.0.5

- Scripted Slashers in the opening chase, elevator and first Plasma Cutter encounter can use the standard, variant, armored or Enhanced Slasher look. The same rule covers identified Slasher scenes in chapters 4 and 7. Their original animations, triggers and actor identities stay in place.
- Each look comes from the enemy seed and the authored actor ID. Checkpoint restarts and different spawn orders give the same result with the same settings.
- Limb weak spots and limb sever costs are off in the default INI. Existing custom settings still apply.
- Install both Plugin 1.0.5 and Pool Assets 1.0.1 when updating from 1.0.4. The pool adds the Slasher visuals to the two original character streams.

# Dead Space 2008 Randomizer 1.0.4

Changes since 1.0.3:

- Remove dead spawned enemies that remain as invisible collision after failing to become ragdolls. `DeadBodySweep=1` enables this fix. It waits ten seconds, checks the object's identity and active state, and removes at most four actors per scan. Authored corpses, the Hunter, Dividers, Guardians and Swarmers are excluded. Cleanup stops if the stream teardown hook is unavailable.
- Add the F9 cheats page (`CheatsMenu=1`): give weapons and ammunition, select an installed suit, toggle noclip and god mode, or change the damage multiplier. Use TAB to change pages, arrow keys to select, and ENTER to apply. Research controls remain behind `DebugCheats`.
- List installed Console DLC weapons and suits by name. Weapon and suit requests run on the game thread, with delivery and failure diagnostics in the log. The first-weapon randomizer no longer changes a Plasma Cutter requested through this menu.
- Recognize the prototype Scorpion and Tank Pulse Rifles when choosing ammunition in the F9 menu.
- Keep noclip movement idle while the F9 overlay uses the arrow keys.

God mode, noclip and extra damage remain off in the included settings. The existing seed and randomizer settings are preserved. The Flier, cut-content experiments and camera tuning are not included.

## Downloads

Both ZIPs are attached to this GitHub release:

- **Plugin 1.0.4:** `DeadSpace2008Randomizer_Plugin_v1.0.4.zip`. Contains the ASI, configurator, default INI, progression tables and loader.
- **Pool Assets 1.0.0:** `DeadSpace2008Randomizer_PoolAssets_v1.0.0.zip`. Contains the enemy pool, character and sound archives.

Plugin 1.0.4 requires Pool Assets 1.0.0. First installation needs both ZIPs. Future plugin updates need only the Plugin ZIP while the required pool version stays the same.

The assets are unchanged from the combined Randomizer 1.0.3 and 1.0.4 packages; users with either complete package installed can keep their pool and download only the Plugin ZIP.

## Update

Close the game and extract the archive into the folder containing `Dead Space.exe`. Back up your INI before replacing it. An existing INI keeps its settings; `CheatsMenu` and `DeadBodySweep` default to 1 if absent. Set either to 0 to disable it. The F9 page also requires `CheckpointChanger=1`.

Made for the Steam build 1.0.0.222. The Console DLC add-on is optional; version 1.0.2 includes the corrected coloured stasis bar.
