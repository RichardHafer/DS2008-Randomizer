# DS2008 Randomizer Configurator

Run `build_configurator.cmd` to create `build/DS2008RandomizerConfig-v1.0.9.exe`. The
executable needs only the Windows .NET Framework 4 runtime.

The layout compiles the shared UI controls from
`../../DeadSpace2Randomizer/tools/RandomizerConfig/Theme.cs`.

The executable icon is packaged from `assets/DS2008RandomizerIcon.ico`.
`assets/DS2008RandomizerIcon.png` is the full-size source image.
The original banner is embedded from `assets/DS2008RandomizerBanner.png`.

Select the `DeadSpace1Patch.ini` beside `Dead Space.exe`, choose a whole-number seed and the
main features, then save. The editor retains settings and sections it does not
show and creates a timestamped backup on every save. Changes take effect on the
next game launch. It does not install the ASI or asset archives.

The editor groups the main options into nine pages with search, per-option
reset, and a recommended-values action. It writes only changed settings.
Enemy sources and replacement families are independent. A zero replacement
weight excludes that family or variant, while a source toggle keeps eligible
authored enemies of that class unchanged. Compatibility and scripted-scene
guards still apply. More Enemies has counts for ordinary and heavy classes;
set-piece actors retain their original counts unless edited in the INI.

The zero-G option requires the enemy randomizer and its global target pool.
When a compatible normal or enhanced donor is unavailable, the authored enemy
is used. This feature has been checked offline but still needs a game test.
