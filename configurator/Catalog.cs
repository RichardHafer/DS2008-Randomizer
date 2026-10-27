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
            "Progression", "World", "Console DLC", "Extras"
        };

        // Shown greyed out unless the console DLC extension plugin sits next to the INI.
        internal const string DlcGroup = "Console DLC";

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
                "Show Dead Space 2008 Randomizer v1.0.0 beside the game version and DeadSpaceFixes when loaded.", "1", "1"),

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
                "Zero excludes this family as a replacement. Compatibility still applies.", "5", "5", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Leaper", "Leaper family weight",
                "Zero excludes this family as a replacement. Compatibility still applies.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Exploder", "Exploder family weight",
                "Zero excludes this family as a replacement. Compatibility still applies.", "6", "6", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Infector", "Infector family weight",
                "Zero excludes this family as a replacement. Requires compatible direct placement.", "5", "5", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Pregnant", "Pregnant family weight",
                "Zero excludes this family as a replacement. Requires compatible direct placement.", "5", "5", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_DividerCore", "Divider family weight",
                "Zero excludes this family as a replacement. The Divider only replaces directly placed enemies.", "3", "3", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Brute", "Brute family weight",
                "Zero excludes this family as a replacement. Brutes need compatible placement.", "2", "2", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Twitcher", "Twitcher family weight",
                "Twitchers are drawn as a family of their own, not among the Slashers. Zero excludes them; with the " +
                "difficulty ramp they stay rare early in a run.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_SwarmEnemy", "Swarm family weight",
                "Zero excludes the swarm as a replacement. Compatibility still applies.", "10", "10", 0, 1000),
            Number("Replacements", "EnemyRandomizerFamilyWeight_Guardian", "Guardian family weight",
                "Zero excludes the wall Guardians as a replacement. Compatibility still applies.", "10", "10", 0, 1000),
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
                "Each catalogued Leaper or Lurker in a weightless room may switch between its normal and enhanced " +
                "form: as often as the difficulty ramp and its cap allow, half and half without the ramp.",
                "0", "0", "Requires the enemy randomizer and global pool. Awaiting an in-game test."),
            Toggle("Enemy behavior", "EnemyRandomizerDifficultyRamp", "Difficulty ramp",
                "Let enhanced variants become more common as the campaign progresses.", "1", "1"),
            Number("Enemy behavior", "EnemyRandomizerLateCapPercent", "Enhanced share cap (%)",
                "With the ramp on, enhanced enemies, Twitchers and the Hunter never take more than this share of a " +
                "family, a zero-G pair or a substitution. 100 removes the cap.", "40", "40", 0, 100),
            Toggle("Enemy behavior", "LimbWeakSpotRandomizer", "Move limb weak spots",
                "Each ordinary necromorph kind gets its weak limb moved for the whole run. Bosses, story characters " +
                "and props keep their authored values.", "0", "0"),
            Toggle("Enemy behavior", "LimbProfilePool", "Limb profiles of other species",
                "Each ordinary necromorph kind takes the limb profile of another species instead. Overrides the weak " +
                "spot setting.", "0", "0"),
            Toggle("Enemy behavior", "LimbSeverCostRandomizer", "Move limb payoffs",
                "Rotate how much overall health an enemy loses when each limb comes off.", "0", "0"),
            Toggle("Enemy behavior", "EnemyRandomizerNeverUnload", "Keep enemies through deaths",
                "The pool's enemy characters stay loaded through deaths and chapter changes; their sound banks " +
                "reload with every world. Needs the pool to be kept loaded.", "0", "1", "Experimental."),
            Number("Enemy behavior", "EnemyRandomizerHunterChance", "Hunter chance (%)",
                "Chance for one designated, safe Hunter encounter per chapter.", "15", "15", 0, 100),
            Toggle("Enemy behavior", "EnemyRandomizerGlobalTemplates", "Global enemy pool",
                "Use the installed sidecar templates for cross-chapter enemies.", "1", "1",
                "Requires the matching ds1_randomizer_assets files."),
            Toggle("Enemy behavior", "EnemyRandomizerGlobalStreaming", "Keep enemy assets loaded",
                "Stream and retain models and audio needed by the global pool. Works together with the global pool; " +
                "the plugin treats the two as one switch.", "1", "1"),
            Toggle("Enemy behavior", "EnemyRandomizerKeepCharacterArchives", "Keep level-owned enemy assets",
                "When a level area unloads an enemy archive the pool also uses, keep it, so every family can appear in every chapter.", "1", "1"),
            Toggle("Enemy behavior", "EnemyRandomizerPersistentPool", "Keep the pool loaded in play",
                "The pool's enemy archives stay loaded while a level plays, so a zone change cannot take them; they reload with each new world.", "1", "1"),
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
            Toggle("More enemies", "PlayingDeadCorpses", "Playing-dead enemies",
                "Some eligible enemies wait as bodies instead of emerging from vents.", "0", "0"),
            Number("More enemies", "PlayingDeadPercent", "Playing-dead share (%)",
                "Chance for an eligible encounter when playing-dead enemies are enabled.", "20", "20", 0, 100),

            Toggle("Loot & items", "LootRandomizer", "Shuffle drops",
                "Exchange the items in enemy and crate drop tables.", "0", "1"),
            Toggle("Loot & items", "LootWeightRandomizer", "Shuffle drop weights",
                "Change which items in a drop table are common or rare.", "0", "1"),
            Number("Loot & items", "LootScarcity", "Loot scarcity",
                "Positive values mean more empty drops; negative values mean more loot.",
                "0", "0", -100, 100),
            Toggle("Loot & items", "LootAutoScale", "Scale loot with More enemies",
                "Ammo and credits from pickups grow with the More enemies counts, weighted by how often each enemy " +
                "appears and how much ammo it costs. Half of the extra is given, since the added enemies drop loot " +
                "themselves. Turn off to set the two amounts below by hand.", "0", "1"),
            Number("Loot & items", "LootAmmoPercent", "Ammo amount (%)",
                "Scale rounds from pickups without changing shop purchases. Not used while loot scales with More enemies.",
                "100", "100", 10, 500),
            Number("Loot & items", "LootCreditsPercent", "Credit amount (%)",
                "Scale credit pickups without changing money already carried. Not used while loot scales with More " +
                "enemies.", "100", "100", 10, 500),
            Toggle("Loot & items", "LooseItemRandomizer", "Shuffle loose items",
                "Trade loose pickups within their chapter and compatible item class.", "0", "1",
                "The floor model is changed when its resource is available."),
            Toggle("Loot & items", "CrateRandomizer", "Shuffle fixed crate contents",
                "Move fixed rewards between compatible crates in the same chapter.", "0", "1"),
            Number("Loot & items", "CratePowerNodePercent", "Power Nodes in breakable crates (%)",
                "While crates are shuffled: how many Power Nodes per chapter breakable crates hold, as a share of the " +
                "chapter's original nodes.", "100", "100", 0, 200),
            Toggle("Loot & items", "LootGlobalPool", "Drops from the whole game",
                "Drop tables pick their items from the whole game's item set, so an area can drop what it never held. " +
                "Drop weights still apply.", "0", "1"),
            Toggle("Loot & items", "LootScarcityRandom", "Seeded scarcity",
                "The seed picks one loot scarcity for the whole run (-15 to +35) instead of the value above.", "0", "1"),
            Toggle("Loot & items", "LogRandomizer", "Shuffle logs",
                "Every collectable audio, text and video log grants another log.", "0", "1"),
            Toggle("Loot & items", "LogRandomizerSameChapter", "Logs stay in their chapter",
                "Shuffle logs only within their own chapter.", "0", "0"),

            Toggle("Progression", "FirstWeaponRandomizer", "First weapon",
                "Replace the first Plasma Cutter with a seeded weapon and matching ammo.", "0", "1"),
            Toggle("Progression", "ProgressionRandomizer", "Schematic progression",
                "Randomize schematic rewards and campaign store progression.", "0", "1"),
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
            Toggle("Progression", "StartingSuitIncludeDefault", "Starting suit may be the default",
                "Let the random starting suit also be the ordinary level 1 suit.", "0", "0"),
            Toggle("Progression", "UpgradeBenchRandomizer", "Shuffle upgrade bench nodes",
                "Rotate which upgrade nodes each bench offers.", "0", "0", "Experimental."),

            Toggle("Console DLC", "ConsoleDlc", "Console DLC",
                "Put the converted console DLC weapons and suits into every store for 0 credits.", "1", "1"),
            Toggle("Console DLC", "ConsoleDlcMaterials", "Console DLC suit materials",
                "DLC suits use the console material values (shine, lighting) instead of the PC ones.", "0", "0"),
            Number("Console DLC", "SuitSlots", "Suit table size",
                "How many suits the game can hold. 30 is retail; the custom DLC suits need 64.", "64", "64", 30, 100),
            Toggle("Console DLC", "SuitPaths", "Suit paths",
                "A seed may replace the level suits with one DLC style's line, levels 1 to 6, with the same prices and " +
                "unlocks, plus that pack's weapons. The other DLC items unlock after the run.", "0", "1",
                "Experimental. Test with a new game first."),
            Toggle("Console DLC", "SuitPathWeaponPacks", "Weapon pack roll",
                "The seed may also bring one weapon-only pack (Heavy Damage, Hot Rod, Pedestrian, Speed Kills, Big Gun); " +
                "its weapons take the places of the retail weapons the suit path leaves.", "1", "1"),
            Toggle("Console DLC", "SuitPath_Normal", "Normal suits",
                "The seed may keep the original suits. The seed picks among the paths switched on here; leave only " +
                "one on to choose it yourself. The rest of the seed (enemies, items, schematics, chapter order) stays " +
                "the same whichever path you play.", "1", "1"),
            Toggle("Console DLC", "SuitPath_Scorpion", "Scorpion path",
                "Scorpion suits on every level, the Scorpion suit at level 5, Scorpion weapons.", "1", "1"),
            Toggle("Console DLC", "SuitPath_Obsidian", "Obsidian path",
                "Obsidian suits on every level, the Obsidian suit at level 5, Obsidian weapons.", "1", "1"),
            Toggle("Console DLC", "SuitPath_Elite", "Elite path",
                "Elite suits on every level, the Elite suit at level 5, Elite weapons.", "1", "1"),
            Toggle("Console DLC", "SuitPath_Unitology", "Unitology path",
                "Unitology suits on every level, the Unitology suit at level 5, Unitology weapons.", "1", "1"),
            Toggle("Console DLC", "SuitPath_Astronaut", "Astronaut path",
                "Astronaut-coloured suits on every level, the Astronaut suit at level 5, Astronaut weapons.", "1", "1"),
            Toggle("Console DLC", "SuitPath_Military", "Military path",
                "Military suits on levels 1 to 5, the Military suit at level 6, all seven Military weapons.", "1", "1"),

            Toggle("World", "LevelOrderRandomizer", "Chapter order",
                "Shuffle chapters 2 through 11. Chapter 1 remains first, chapter 12 last.", "0", "1"),
            Toggle("World", "OxygenRandomizer", "Random oxygen budget",
                "Choose one air budget from the seed for the whole run.", "0", "1"),
            Toggle("World", "VacuumZones", "Vacuum rooms",
                "Turn the rooms listed in the INI's [VacuumZones] section into vacuum.", "0", "0"),
            Toggle("World", "VacuumRoomRandomizer", "Random vacuum rooms",
                "The seed turns rooms recorded in DeadSpace2008Randomizer_VacuumRooms.txt into vacuum.", "0", "0"),
            Number("World", "VacuumRoomsPerChapter", "Random vacuum rooms per chapter",
                "How many recorded rooms per chapter the seed turns into vacuum.", "1", "1", 0, 16),

            Toggle("Extras", "SkipTutorialTips", "Hide tutorial tips",
                "Suppress ordinary tutorial popups and their log-menu entries.", "0", "0"),
            Toggle("Extras", "GodMode", "Start with god mode",
                "Prevent health loss for the whole session.", "0", "0"),
            Number("Extras", "GodModeHotkey", "God mode key",
                "Windows key code that turns god mode on and off in game (116 is F5). Zero means no key.",
                "0", "0", 0, 255),
            Number("Extras", "DamageMultiplier", "Damage to enemies (x)",
                "Enemies take this many times the damage. 1 is normal.", "1", "1", 1, 100000),
            Number("Extras", "DamageMultiplierHotkey", "Damage key",
                "Windows key code that turns the damage multiplier on and off (117 is F6). With a key it starts off.",
                "0", "0", 0, 255),
            Toggle("Extras", "StoreFastScroll", "Fast store scrolling",
                "PageUp/PageDown or LT/RT jump a page in the store, with Ctrl to the first or last item.", "1", "1"),
            Toggle("Extras", "LegLengthCorrection", "Model suit leg lengths",
                "Model suits built with their own leg lengths keep them in every animation.", "1", "1"),
            Toggle("Extras", "CrashDumps", "Capture crash dumps",
                "Save a diagnostic dump when the game crashes.", "1", "1"),
            Toggle("Extras", "EnemyRandomizerDiagnostics", "Detailed enemy log",
                "Log extra selection details for troubleshooting.", "0", "0")
        };
    }

    // The suit path a seed plays. Must match the console DLC plugin's path choice.
    internal static class SuitPaths
    {
        // True while the console DLC extension plugin sits next to the loaded INI.
        internal static bool Available = false;

        internal static readonly string[] Names = {
            "Normal", "Scorpion", "Obsidian", "Elite", "Unitology", "Astronaut", "Military"
        };

        private static uint Mix(uint value)
        {
            unchecked
            {
                value ^= 0x50415448u;   // "PATH"
                value ^= value >> 16;
                value *= 0x7FEB352Du;
                value ^= value >> 15;
                value *= 0x846CA68Bu;
                value ^= value >> 16;
            }
            return value;
        }

        internal static uint SeedOf(System.Collections.Generic.Dictionary<string, string> values)
        {
            string text;
            uint seed;
            values.TryGetValue("EnemyRandomizerSeed", out text);
            text = (text ?? "").Trim();
            bool parsed = text.StartsWith("0x", System.StringComparison.OrdinalIgnoreCase)
                ? uint.TryParse(text.Substring(2), System.Globalization.NumberStyles.AllowHexSpecifier,
                                System.Globalization.CultureInfo.InvariantCulture, out seed)
                : uint.TryParse(text, System.Globalization.NumberStyles.None,
                                System.Globalization.CultureInfo.InvariantCulture, out seed);
            return parsed ? seed : 0x00D5A11u;
        }

        // -1 when suit paths are off.
        internal static int For(System.Collections.Generic.Dictionary<string, string> values)
        {
            string master;
            if (!values.TryGetValue("SuitPaths", out master) || master != "1") return -1;
            uint seed = SeedOf(values);
            var allowed = new System.Collections.Generic.List<int>();
            for (int i = 0; i < Names.Length; ++i)
            {
                string on;
                if (!values.TryGetValue("SuitPath_" + Names[i], out on) || on == "1") allowed.Add(i);
            }
            if (allowed.Count == 0) return 0;
            return allowed[(int)(Mix(seed) % (uint)allowed.Count)];
        }

        internal static readonly string[] WeaponPacks = {
            "no weapon pack", "Heavy Damage", "Hot Rod", "Pedestrian", "Speed Kills", "Big Gun"
        };

        // 0 when no pack is rolled. Same seed parse and mix as For, its own salt "WPCK".
        internal static int WeaponPackFor(System.Collections.Generic.Dictionary<string, string> values)
        {
            string on;
            if (For(values) < 0 || (values.TryGetValue("SuitPathWeaponPacks", out on) && on == "0")) return 0;
            uint seed = SeedOf(values);
            unchecked
            {
                return (int)(Mix(seed ^ 0x5750434Bu ^ 0x50415448u) % (uint)WeaponPacks.Length);
            }
        }

        internal static string Describe(System.Collections.Generic.Dictionary<string, string> values)
        {
            int path = For(values);
            if (path < 0) return "Suit paths are off: the original suits.";
            int pack = WeaponPackFor(values);
            string weapons = pack == 0 ? "" : ", plus the " + WeaponPacks[pack] + " weapon pack";
            return (path == 0 ? "This seed plays the original suits" : "This seed plays the " + Names[path] + " suit path") +
                   weapons + ".";
        }
    }
}
