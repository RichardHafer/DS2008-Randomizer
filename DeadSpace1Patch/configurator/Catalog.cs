namespace DS2008RandomizerConfig
{
    internal enum ValueKind { Toggle, Number, Seed }

    internal sealed class Option
    {
        internal readonly string Group;
        internal readonly string Key;
        internal readonly string Title;
        internal readonly string Description;
        internal readonly string Default;
        internal readonly string Recommended;
        internal readonly ValueKind Kind;
        internal readonly int Minimum;
        internal readonly int Maximum;
        internal readonly string Note;

        internal Option(string group, string key, string title, string description,
                        string fallback, string recommended, ValueKind kind,
                        int minimum, int maximum, string note)
        {
            Group = group;
            Key = key;
            Title = title;
            Description = description;
            Default = fallback;
            Recommended = recommended;
            Kind = kind;
            Minimum = minimum;
            Maximum = maximum;
            Note = note;
        }
    }

    internal static class Catalog
    {
        internal static readonly string[] Groups = {
            "General", "Enemy sources", "Replacements", "Enemy behavior", "More enemies", "Loot & items",
            "Progression", "World", "Extras"
        };

        private static Option Toggle(string group, string key, string title, string description,
                                     string fallback, string recommended, string note = "")
        {
            return new Option(group, key, title, description, fallback, recommended,
                              ValueKind.Toggle, 0, 1, note);
        }

        private static Option Number(string group, string key, string title, string description,
                                     string fallback, string recommended, int minimum, int maximum,
                                     string note = "")
        {
            return new Option(group, key, title, description, fallback, recommended,
                              ValueKind.Number, minimum, maximum, note);
        }

        internal static readonly Option[] Options = {
            Toggle("General", "EnemyRandomizer", "Randomizer on",
                "Master switch for enemy replacements. The other features can also be used separately.", "0", "1"),
            new Option("General", "EnemyRandomizerSeed", "Seed",
                "The same seed repeats the same choices. Start a new game after changing it.",
                "875025", "875025", ValueKind.Seed, 0, 0, "Do not change the seed during a playthrough."),
            Number("General", "EnemyRandomizerChancePercent", "Replacement chance (%)",
                "How often an eligible enemy is replaced. Zero keeps every original enemy.",
                "100", "100", 0, 100),
            Toggle("General", "MenuVersionLabel", "Main menu version label",
                "Show DS2008 Randomizer v1.0 beside the game version and DeadSpaceFixes when loaded.", "1", "1"),

            Toggle("Enemy sources", "EnemyRandomizerSource_Zombie", "Randomize Slashers",
                "Include Zombie-class spawns, including Twitchers and Wheezers, as sources.", "1", "1"),
            Toggle("Enemy sources", "EnemyRandomizerSource_Lurker", "Randomize Lurkers",
                "Include reviewed Lurker spawns as sources.", "1", "1"),
            Toggle("Enemy sources", "EnemyRandomizerSource_Leaper", "Randomize Leapers",
                "Include reviewed Leaper spawns as sources.", "1", "1"),
            Toggle("Enemy sources", "EnemyRandomizerSource_Exploder", "Randomize Exploders",
                "Include reviewed Exploder spawns as sources.", "1", "1"),
            Toggle("Enemy sources", "EnemyRandomizerSource_Infector", "Randomize Infectors",
                "Include only catalogued, compatible Infector spawns as sources.", "1", "1"),
            Toggle("Enemy sources", "EnemyRandomizerSource_Pregnant", "Randomize Pregnants",
                "Include only catalogued, compatible Pregnant spawns as sources.", "1", "1"),
            Number("Enemy sources", "EnemyRandomizerKeepChance_Zombie", "Keep original Slashers (%)",
                "Seeded chance to leave an eligible Zombie-class source unchanged.", "0", "0", 0, 100),
            Number("Enemy sources", "EnemyRandomizerKeepChance_Lurker", "Keep original Lurkers (%)",
                "Seeded chance to leave an eligible Lurker source unchanged.", "0", "0", 0, 100),
            Number("Enemy sources", "EnemyRandomizerKeepChance_Leaper", "Keep original Leapers (%)",
                "Seeded chance to leave an eligible Leaper source unchanged.", "0", "0", 0, 100),
            Number("Enemy sources", "EnemyRandomizerKeepChance_Exploder", "Keep original Exploders (%)",
                "Seeded chance to leave an eligible Exploder source unchanged.", "0", "0", 0, 100),
            Number("Enemy sources", "EnemyRandomizerKeepChance_Infector", "Keep original Infectors (%)",
                "Seeded chance to leave an eligible Infector source unchanged.", "0", "0", 0, 100),
            Number("Enemy sources", "EnemyRandomizerKeepChance_Pregnant", "Keep original Pregnants (%)",
                "Seeded chance to leave an eligible Pregnant source unchanged.", "0", "0", 0, 100),

            Number("Replacements", "EnemyRandomizerFamilyWeight_Zombie", "Slasher family weight",
                "Zero excludes this family as a replacement. Higher values make it more common.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Lurker", "Lurker family weight",
                "Zero excludes this family as a replacement. Compatibility still applies.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Leaper", "Leaper family weight",
                "Zero excludes this family as a replacement. Compatibility still applies.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Exploder", "Exploder family weight",
                "Zero excludes this family as a replacement. Compatibility still applies.", "5", "5", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Infector", "Infector family weight",
                "Zero excludes this family as a replacement. Requires compatible direct placement.", "5", "5", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Pregnant", "Pregnant family weight",
                "Zero excludes this family as a replacement. Requires compatible direct placement.", "5", "5", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Brute", "Brute family weight",
                "Zero excludes this family as a replacement. Brutes need compatible placement.", "2", "2", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_zombie", "Standard Slasher weight",
                "Zero excludes this exact replacement variant; 10 is neutral within its family.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_zombievar1", "Slasher variant 1 weight",
                "Zero excludes this exact replacement variant; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_zombievar2", "Slasher variant 2 weight",
                "Zero excludes this exact replacement variant; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_zombiewoman", "Female Slasher weight",
                "Zero excludes this exact replacement variant; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_twitcher", "Twitcher weight",
                "Zero excludes Twitchers as replacements; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_super_slasher", "Enhanced Slasher weight",
                "Zero excludes Enhanced Slashers as replacements; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_lurker", "Standard Lurker weight",
                "Zero excludes standard Lurkers as replacements; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_super_lurker", "Enhanced Lurker weight",
                "Zero excludes Enhanced Lurkers as replacements; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_leaper", "Standard Leaper weight",
                "Zero excludes standard Leapers as replacements; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_super_leaper", "Enhanced Leaper weight",
                "Zero excludes Enhanced Leapers as replacements; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_sup_expl", "Enhanced Exploder weight",
                "Zero excludes the stronger Exploder replacement; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_infector", "Infector variant weight",
                "Zero excludes Infectors as replacements; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_pregnant", "Pregnant variant weight",
                "Zero excludes Pregnants as replacements; 10 is neutral.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerWeight_super_brute", "Super Brute weight",
                "Zero excludes Super Brutes as replacements; 10 is neutral.", "10", "10", 0, 1000),

            Toggle("Enemy behavior", "EnemyRandomizerCrossFamilyContexts", "Mix enemy families",
                "Allow compatible spawners to draw an enemy from another family.", "1", "1"),
            Toggle("Enemy behavior", "EnemyRandomizerScriptedVariants", "Scripted scene variants",
                "Use a same-class variant where a scene directly controls its enemy.", "1", "1"),
            Toggle("Enemy behavior", "EnemyRandomizerZeroGVariants", "Zero-G normal / enhanced",
                "Each catalogued Leaper or Lurker has a seeded 50/50 chance to switch form.",
                "0", "0", "Requires the enemy randomizer and global pool. Awaiting an in-game test."),
            Toggle("Enemy behavior", "EnemyRandomizerDifficultyRamp", "Difficulty ramp",
                "Let enhanced variants become more common as the campaign progresses.", "1", "1"),
            Number("Enemy behavior", "EnemyRandomizerHunterChance", "Hunter chance (%)",
                "Chance for one designated, safe Hunter encounter per chapter.", "15", "15", 0, 100),
            Toggle("Enemy behavior", "EnemyRandomizerGlobalTemplates", "Global enemy pool",
                "Use the installed sidecar templates for cross-chapter enemies.", "0", "1",
                "Requires the matching ds1_randomizer_assets files."),
            Toggle("Enemy behavior", "EnemyRandomizerGlobalStreaming", "Keep enemy assets loaded",
                "Stream and retain models and audio needed by the global pool.", "0", "1"),
            Toggle("Enemy behavior", "EnemyRandomizerDirectSpawns", "Directly placed enemies",
                "Include reviewed spawns that stand in the world rather than exit a vent.", "1", "1"),
            Toggle("Enemy behavior", "EnemyRandomizerHeavyFamilies", "Heavy replacements",
                "Allow compatible Pregnant, Infector and Super Brute replacements with minimal sidecars.", "1", "1"),
            Toggle("Enemy behavior", "EnemyRandomizerIgnoreVentSurface", "Mix vent surfaces",
                "Match the vent arrival mode without requiring the original floor, wall or ceiling surface.", "1", "1"),

            Number("More enemies", "SpawnsPerTrigger", "Enemies per trigger",
                "Fallback count for enemy classes without an EnemyType override. One is original.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_Zombie", "Slashers per trigger",
                "Total Zombie-class enemies, including Twitchers and Wheezers. One is original.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_Lurker", "Lurkers per trigger",
                "Total Lurkers at an eligible trigger. One is original.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_Leaper", "Leapers per trigger",
                "Total Leapers at an eligible trigger. One is original.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_Exploder", "Exploders per trigger",
                "Total Exploders at an eligible trigger. Extra blasts can be dangerous.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_Infector", "Infectors per trigger",
                "Total Infectors at an eligible trigger; each can create more Slashers.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_SwarmEnemy", "Swarmers per trigger",
                "Total Swarm enemies at an eligible trigger. One is original.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_Brute", "Brutes per trigger",
                "Total Brutes at an eligible trigger. One is strongly recommended.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_Pregnant", "Pregnants per trigger",
                "Total Pregnants at an eligible trigger; each can release Swarmers.", "1", "1", 1, 16),
            Number("More enemies", "EnemyType_DividerCore", "Dividers per trigger",
                "Total Divider cores at an eligible trigger. One is original.", "1", "1", 1, 16),
            Toggle("More enemies", "EnhancedExploders", "Enhanced Exploders",
                "Turn some Exploders into the stronger authored variant.", "0", "0"),
            Number("More enemies", "EnhancedExploderPercent", "Enhanced Exploder share (%)",
                "How many eligible Exploders get the stronger variant.", "50", "50", 0, 100),
            Toggle("More enemies", "PlayingDeadCorpses", "Playing-dead enemies",
                "Some eligible enemies wait as bodies instead of emerging from vents.", "0", "0"),
            Number("More enemies", "PlayingDeadPercent", "Playing-dead share (%)",
                "Chance for an eligible encounter when playing-dead enemies are enabled.", "20", "20", 0, 100),

            Toggle("Loot & items", "LootRandomizer", "Shuffle drops",
                "Exchange the items in enemy and crate drop tables.", "0", "0"),
            Toggle("Loot & items", "LootWeightRandomizer", "Shuffle drop weights",
                "Change which items in a drop table are common or rare.", "0", "0"),
            Number("Loot & items", "LootScarcity", "Loot scarcity",
                "Positive values mean more empty drops; negative values mean more loot.",
                "0", "0", -100, 100),
            Number("Loot & items", "LootAmmoPercent", "Ammo amount (%)",
                "Scale rounds from pickups without changing shop purchases.", "100", "100", 10, 500),
            Number("Loot & items", "LootCreditsPercent", "Credit amount (%)",
                "Scale credit pickups without changing money already carried.", "100", "100", 10, 500),
            Toggle("Loot & items", "LooseItemRandomizer", "Shuffle loose items",
                "Trade loose pickups within their chapter and compatible item class.", "0", "0",
                "The floor model is changed when its resource is available."),
            Toggle("Loot & items", "CrateRandomizer", "Shuffle fixed crate contents",
                "Move fixed rewards between compatible crates in the same chapter.", "0", "0"),

            Toggle("Progression", "FirstWeaponRandomizer", "First weapon",
                "Replace the first Plasma Cutter with a seeded weapon and matching ammo.", "0", "0"),
            Toggle("Progression", "ProgressionRandomizer", "Schematic progression",
                "Randomize schematic rewards and campaign store progression.", "0", "0"),
            Toggle("Progression", "ProgressionSeeded", "Seed schematic rewards",
                "Use the enemy seed to permute rewards within each reward class.", "0", "0"),
            Toggle("Progression", "ProgressionStoreGate", "Move store unlocks",
                "Unlock a moved reward when its new schematic position permits it.", "0", "0"),
            Toggle("Progression", "ProgressionStartingWeapons", "First store weapons",
                "Replace the Line Gun and Pulse Rifle in the first kiosk with a seeded pair.", "0", "0"),
            Toggle("Progression", "StartingSuitRandomizer", "Starting suit",
                "Give a seeded suit on a new game; existing saves are not migrated.", "0", "0",
                "Experimental. Test with a new save first."),
            Number("Progression", "StartingSuitCreditShare", "Suit debt share (%)",
                "Share of future credit income used to pay for a randomized starting suit.",
                "50", "50", 0, 100),

            Toggle("World", "LevelOrderRandomizer", "Chapter order",
                "Shuffle chapters 2 through 11. Chapter 1 remains first, chapter 12 last.", "0", "0"),
            Toggle("World", "OxygenRandomizer", "Random oxygen budget",
                "Choose one air budget from the seed for the whole run.", "0", "0"),

            Toggle("Extras", "SkipTutorialTips", "Hide tutorial tips",
                "Suppress ordinary tutorial popups and their log-menu entries.", "0", "0"),
            Toggle("Extras", "GodMode", "Start with god mode",
                "Prevent health loss. F8 toggles it while a level is loaded.", "0", "0"),
            Toggle("Extras", "CrashDumps", "Capture crash dumps",
                "Save a diagnostic dump when the game crashes.", "1", "1"),
            Toggle("Extras", "EnemyRandomizerDiagnostics", "Detailed enemy log",
                "Log extra selection details for troubleshooting.", "0", "0")
        };
    }
}
