Dead Space 2008 Randomizer 1.0.0
Dead Space (2008), Steam version

WHAT IT DOES

Enemies are drawn from a pool of every regular necromorph family and variant, so
any chapter can throw Leapers, Lurkers, Enhanced Slashers, Pregnants and more at
you, coming out of the same vents and spots the original enemy used. Optional
modules shuffle loot, crate contents, loose items, audio logs, schematics, the
first weapon, the starting suit and the chapter order.

Every choice comes from the seed. The same seed with the same settings gives the
same run, also after a death, a checkpoint restart or loading a save.

INSTALL

1. Open the game folder: in Steam, right-click Dead Space > Manage > Browse local files.
   It is the folder that holds "Dead Space.exe".
2. Extract everything from this zip into that folder, keeping the folders:
     winmm.dll                            (Ultimate ASI Loader, loads the plugin)
     plugins\DeadSpace2008Randomizer.asi  (the randomizer)
     ds1_randomizer_assets\               (enemy pool files)
     DeadSpace2008Randomizer.ini          (settings)
     DeadSpace2008RandomizerConfig.exe    (settings window)
     DeadSpace1Progression*.tsv           (schematic and store lists)
3. If the folder already has a winmm.dll from another mod, keep that one; it is
   most likely the same loader.
4. Start the game. The main menu shows "Dead Space 2008 Randomizer v1.0.0" next to the
   game version when the plugin is running.

SETTINGS

Run DeadSpace2008RandomizerConfig.exe from the game folder. It opens the
DeadSpace2008Randomizer.ini next to it on its own. Pick a seed and the modules
you want, save, then start the game and begin a NEW game. Changing settings in
the middle of a playthrough mixes two runs.

Defaults: enemy randomizer, loot, crates, loose items, logs, schematics, first weapon,
chapter order and oxygen on; the rest off.

COMPATIBILITY

- Made for the Steam release (version 1.0.0.222). Other builds are allowed: the
  plugin checks every patch site against the Steam bytes and skips the ones that
  differ, so some features may stay off there. DeadSpace2008Randomizer.log lists them.
- Works next to DeadSpaceFixes.
- The console DLC suits and weapons are a separate add-on. This package
  works without it; the settings window greys that page out until it is
  installed.

UNINSTALL

Delete plugins\DeadSpace2008Randomizer.asi (and winmm.dll if no other mod uses it).
Saves stay playable.

REPORTING A PROBLEM

Please attach DeadSpace2008Randomizer.log (and DeadSpace2008Randomizer.prev.log after a crash)
from the game folder, the seed, and the settings file. After a crash the plugin
also writes a .dmp file next to the log; it is large, so only send it when asked.

CREDITS

winmm.dll is the Ultimate ASI Loader by ThirteenAG, MIT licence, see
UltimateASILoader_LICENSE.txt.
