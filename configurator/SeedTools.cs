using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;

namespace DS2008RandomizerConfig
{
    // A share code: the seed plus every setting that differs from the frozen defaults, so another player gets the
    // same run. Each version only appends keys; the lists and defaults below never change.
    internal static class SeedCode
    {
        private const string Prefix = "DS08";
        private const string Alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        private static readonly string[] KeysV1 = {
            "EnemyRandomizer=0", "EnemyRandomizerSeed=875025", "EnemyRandomizerChancePercent=100", "MenuVersionLabel=1",
            "EnemyRandomizerSource_Zombie=1", "EnemyRandomizerSource_Lurker=1", "EnemyRandomizerSource_Leaper=1",
            "EnemyRandomizerSource_Exploder=1", "EnemyRandomizerSource_Infector=1", "EnemyRandomizerSource_Pregnant=1",
            "EnemyRandomizerKeepChance_Zombie=0", "EnemyRandomizerKeepChance_Lurker=0", "EnemyRandomizerKeepChance_Leaper=0",
            "EnemyRandomizerKeepChance_Exploder=0", "EnemyRandomizerKeepChance_Infector=0",
            "EnemyRandomizerKeepChance_Pregnant=0", "EnemyRandomizerFamilyWeight_Zombie=10",
            "EnemyRandomizerFamilyWeight_Lurker=5", "EnemyRandomizerFamilyWeight_Leaper=10",
            "EnemyRandomizerFamilyWeight_Exploder=6", "EnemyRandomizerFamilyWeight_Infector=5",
            "EnemyRandomizerFamilyWeight_Pregnant=5", "EnemyRandomizerFamilyWeight_DividerCore=3",
            "EnemyRandomizerFamilyWeight_Brute=2", "EnemyRandomizerWeight_zombie=10", "EnemyRandomizerWeight_zombievar1=10",
            "EnemyRandomizerWeight_zombievar2=10", "EnemyRandomizerWeight_zombiewoman=10", "EnemyRandomizerWeight_twitcher=10",
            "EnemyRandomizerWeight_super_slasher=10", "EnemyRandomizerWeight_lurker=10",
            "EnemyRandomizerWeight_super_lurker=10", "EnemyRandomizerWeight_leaper=10",
            "EnemyRandomizerWeight_super_leaper=10", "EnemyRandomizerWeight_sup_expl=10", "EnemyRandomizerWeight_infector=10",
            "EnemyRandomizerWeight_pregnant=10", "EnemyRandomizerWeight_super_brute=10",
            "EnemyRandomizerCrossFamilyContexts=1", "EnemyRandomizerScriptedVariants=1", "EnemyRandomizerZeroGVariants=0",
            "EnemyRandomizerDifficultyRamp=1", "EnemyRandomizerHunterChance=15", "EnemyRandomizerGlobalTemplates=1",
            "EnemyRandomizerGlobalStreaming=1", "EnemyRandomizerKeepCharacterArchives=1", "EnemyRandomizerDirectSpawns=1",
            "EnemyRandomizerHeavyFamilies=1", "EnemyRandomizerIgnoreVentSurface=1", "SpawnsPerTrigger=1",
            "EnemyType_Zombie=1", "EnemyType_Lurker=1", "EnemyType_Leaper=1", "EnemyType_Exploder=1", "EnemyType_Infector=1",
            "EnemyType_SwarmEnemy=1", "EnemyType_Brute=1", "EnemyType_Pregnant=1", "EnemyType_DividerCore=1",
            "PlayingDeadCorpses=0", "PlayingDeadPercent=20", "LootRandomizer=0", "LootWeightRandomizer=0", "LootScarcity=0",
            "LootAutoScale=0", "LootAmmoPercent=100", "LootCreditsPercent=100", "LooseItemRandomizer=0", "CrateRandomizer=0",
            "FirstWeaponRandomizer=0", "ProgressionRandomizer=0", "ProgressionSeeded=0", "ProgressionStoreGate=0",
            "ProgressionStartingWeapons=0", "StartingSuitRandomizer=0", "StartingSuitCreditShare=50", "ConsoleDlc=1",
            "ConsoleDlcMaterials=0", "SuitSlots=64", "SuitPaths=0", "SuitPathWeaponPacks=1", "SuitPath_Normal=1",
            "SuitPath_Scorpion=1", "SuitPath_Obsidian=1", "SuitPath_Elite=1", "SuitPath_Unitology=1", "SuitPath_Astronaut=1",
            "SuitPath_Military=1", "LevelOrderRandomizer=0", "OxygenRandomizer=0", "SkipTutorialTips=0", "GodMode=0",
            "CrashDumps=1", "EnemyRandomizerDiagnostics=0"
        };
        // Version 2: run-affecting settings the catalog gained later. A version 1 code means their defaults.
        private static readonly string[] KeysV2 = {
            "EnemyRandomizerFamilyWeight_Twitcher=10", "EnemyRandomizerFamilyWeight_SwarmEnemy=10",
            "EnemyRandomizerFamilyWeight_Guardian=10", "EnemyRandomizerLateCapPercent=40", "LimbWeakSpotRandomizer=0",
            "LimbProfilePool=0", "LimbSeverCostRandomizer=0", "CratePowerNodePercent=100", "LootGlobalPool=0",
            "LootScarcityRandom=0", "LogRandomizer=0", "LogRandomizerSameChapter=0", "StartingSuitIncludeDefault=0",
            "UpgradeBenchRandomizer=0", "VacuumZones=0", "VacuumRoomRandomizer=0",
            "VacuumRoomsPerChapter=1"
        };
        private const uint Version = 2;
        private static readonly string[] Keys = Concat(KeysV1, KeysV2);

        internal static IEnumerable<string> KeyNames()
        {
            foreach (string entry in Keys) yield return entry.Split('=')[0];
        }

        private static string[] Concat(string[] first, string[] second)
        {
            var all = new string[first.Length + second.Length];
            first.CopyTo(all, 0);
            second.CopyTo(all, first.Length);
            return all;
        }

        private sealed class Bits
        {
            private readonly List<byte> bytes = new List<byte>();
            private int used;
            private readonly byte[] input;
            private int read;

            internal Bits() { }
            internal Bits(byte[] data) { input = data; }

            internal void Write(uint value, int count)
            {
                for (int i = count - 1; i >= 0; --i)
                {
                    if (used % 8 == 0) bytes.Add(0);
                    if (((value >> i) & 1) != 0) bytes[bytes.Count - 1] |= (byte)(0x80 >> (used % 8));
                    ++used;
                }
            }

            internal uint Read(int count)
            {
                uint value = 0;
                for (int i = 0; i < count; ++i)
                {
                    if (read / 8 >= input.Length) throw new FormatException("The code is too short.");
                    value = (value << 1) | (uint)((input[read / 8] >> (7 - read % 8)) & 1);
                    ++read;
                }
                return value;
            }

            internal void WriteNumber(int value)
            {
                uint zigzag = (uint)((value << 1) ^ (value >> 31));
                do
                {
                    uint chunk = zigzag & 0x3F;
                    zigzag >>= 6;
                    Write(chunk | (zigzag != 0 ? 0x40u : 0u), 7);
                } while (zigzag != 0);
            }

            internal int ReadNumber()
            {
                uint zigzag = 0;
                for (int shift = 0; shift < 36; shift += 6)
                {
                    uint chunk = Read(7);
                    zigzag |= (chunk & 0x3F) << shift;
                    if ((chunk & 0x40) == 0) return (int)(zigzag >> 1) ^ -(int)(zigzag & 1);
                }
                throw new FormatException("A number in the code is damaged.");
            }

            internal byte[] ToArray() { return bytes.ToArray(); }
        }

        private static byte Check(byte[] data, int count)
        {
            uint crc = 0x5A;
            for (int i = 0; i < count; ++i)
            {
                crc ^= data[i];
                for (int b = 0; b < 8; ++b) crc = (crc & 0x80) != 0 ? ((crc << 1) ^ 0x07) & 0xFF : (crc << 1) & 0xFF;
            }
            return (byte)crc;
        }

        internal static string Encode(Dictionary<string, string> values)
        {
            var bits = new Bits();
            bits.Write(Version, 4);
            bits.Write(SuitPaths.SeedOf(values), 32);
            var changes = new List<KeyValuePair<int, int>>();
            for (int i = 0; i < Keys.Length; ++i)
            {
                string[] pair = Keys[i].Split('=');
                if (pair[0] == "EnemyRandomizerSeed") continue;
                string text;
                int value;
                if (!values.TryGetValue(pair[0], out text) || !int.TryParse(text, NumberStyles.Integer,
                    CultureInfo.InvariantCulture, out value)) continue;
                if (value.ToString(CultureInfo.InvariantCulture) != pair[1]) changes.Add(new KeyValuePair<int, int>(i, value));
            }
            bits.Write((uint)changes.Count, 7);
            foreach (KeyValuePair<int, int> change in changes)
            {
                bits.Write((uint)change.Key, 7);
                bits.WriteNumber(change.Value);
            }
            byte[] body = bits.ToArray();
            byte[] data = new byte[body.Length + 1];
            Array.Copy(body, data, body.Length);
            data[body.Length] = Check(body, body.Length);
            var text32 = new StringBuilder();
            int buffer = 0, held = 0;
            foreach (byte b in data)
            {
                buffer = (buffer << 8) | b;
                held += 8;
                while (held >= 5)
                {
                    text32.Append(Alphabet[(buffer >> (held - 5)) & 31]);
                    held -= 5;
                }
            }
            if (held > 0) text32.Append(Alphabet[(buffer << (5 - held)) & 31]);
            var grouped = new StringBuilder(Prefix);
            for (int i = 0; i < text32.Length; i += 4)
                grouped.Append('-').Append(text32.ToString(i, Math.Min(4, text32.Length - i)));
            return grouped.ToString();
        }

        // The values a code stands for: every known key, the defaults where the code says nothing.
        internal static Dictionary<string, string> Decode(string code)
        {
            string text = (code ?? "").Trim().ToUpperInvariant().Replace(" ", "");
            if (!text.StartsWith(Prefix + "-")) throw new FormatException("A seed code starts with " + Prefix + "-.");
            text = text.Substring(Prefix.Length).Replace("-", "").Replace('O', '0').Replace('I', '1').Replace('L', '1');
            var bytes = new List<byte>();
            int buffer = 0, held = 0;
            foreach (char c in text)
            {
                int digit = Alphabet.IndexOf(c);
                if (digit < 0) throw new FormatException("The code has a character that does not belong in it: " + c);
                buffer = (buffer << 5) | digit;
                held += 5;
                if (held >= 8)
                {
                    bytes.Add((byte)(buffer >> (held - 8)));
                    held -= 8;
                    buffer &= (1 << held) - 1;
                }
            }
            if (bytes.Count < 6) throw new FormatException("The code is too short.");
            byte[] data = bytes.ToArray();
            int length = data.Length - 1;
            if (Check(data, length) != data[length])
                throw new FormatException("The code does not check out. Copy it again, a character is wrong or missing.");
            var bits = new Bits(data);
            uint version = bits.Read(4);
            if (version < 1 || version > Version) throw new FormatException("This code comes from a newer configurator.");
            int known = version == 1 ? KeysV1.Length : Keys.Length;
            var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (string entry in Keys)
            {
                string[] pair = entry.Split('=');
                result[pair[0]] = pair[1];
            }
            result["EnemyRandomizerSeed"] = bits.Read(32).ToString(CultureInfo.InvariantCulture);
            int count = (int)bits.Read(7);
            for (int i = 0; i < count; ++i)
            {
                int index = (int)bits.Read(7);
                int value = bits.ReadNumber();
                if (index >= known) throw new FormatException("The code names a setting this configurator lacks.");
                result[Keys[index].Split('=')[0]] = value.ToString(CultureInfo.InvariantCulture);
            }
            return result;
        }
    }

    // What a seed decides outside the enemies, computed the way the plugin does.
    internal static class SeedPreview
    {
        private static readonly string[] FirstWeapons = {
            "Plasma Cutter", "Line Gun", "Pulse Rifle", "Flamethrower", "Force Gun", "Ripper", "Contact Beam"
        };
        private static readonly string[] KioskWeapons = {
            "Flamethrower", "Force Gun", "Line Gun", "Pulse Rifle", "Ripper", "Contact Beam"
        };

        private static uint Avalanche(uint value, uint salt)
        {
            unchecked
            {
                value ^= salt;
                value ^= value >> 16;
                value *= 0x7FEB352Du;
                value ^= value >> 15;
                value *= 0x846CA68Bu;
                value ^= value >> 16;
            }
            return value;
        }

        private static bool On(Dictionary<string, string> values, string key)
        {
            string text;
            return values.TryGetValue(key, out text) && text == "1";
        }

        // progression_randomizer.inc MixStartingSuitSeed ("SUIT") and MixStartingStoreSeed ("SHOP").
        internal static uint SuitMix(uint value) { return Avalanche(value, 0x53554954u); }
        internal static uint ShopMix(uint value) { return Avalanche(value, 0x504F4853u); }

        internal static int FirstWeapon(uint seed)
        {
            return (int)(SuitMix(seed ^ 0x46525354u) % (uint)FirstWeapons.Length);
        }

        internal static int[] ChapterOrder(uint seed)
        {
            var order = new int[12];
            for (int i = 0; i < 12; ++i) order[i] = i + 1;
            for (int i = 10; i > 1; --i)
            {
                uint roll = Spoiler.Hash(seed, new uint[] { seed, (uint)i, 0, 0 }, new uint[4], 0x4C564C4Fu);
                int j = 1 + (int)(roll % (uint)i);
                int keep = order[i];
                order[i] = order[j];
                order[j] = keep;
            }
            return order;
        }

        internal static string Describe(Dictionary<string, string> values, bool dlc)
        {
            uint seed = SuitPaths.SeedOf(values);
            var parts = new List<string>();
            int first = FirstWeapon(seed);
            bool firstWeapon = On(values, "FirstWeaponRandomizer") && first != 0;
            if (On(values, "FirstWeaponRandomizer"))
                parts.Add("first weapon " + FirstWeapons[first]);
            if (On(values, "StartingSuitRandomizer"))
                parts.Add("starting suit level " + (2 + SuitMix(seed) % 5));
            if (On(values, "ProgressionStartingWeapons"))
            {
                var pool = new List<string>(KioskWeapons);
                if (firstWeapon) pool.Remove(FirstWeapons[first]);
                int a = (int)(ShopMix(seed) % (uint)pool.Count);
                int b = (int)(ShopMix(seed ^ 0x9E3779B9u) % (uint)(pool.Count - 1));
                if (b >= a) ++b;
                parts.Add("first kiosk " + pool[a] + " and " + pool[b]);
            }
            if (On(values, "LevelOrderRandomizer"))
                parts.Add("chapters " + string.Join(" > ", Array.ConvertAll(ChapterOrder(seed), c => c.ToString())));
            string text = parts.Count > 0 ? "Seed preview: " + string.Join(", ", parts.ToArray()) + "." : "";
            if (dlc)
                text = (text + " " + SuitPaths.Describe(values)).Trim();
            return text;
        }
    }

    // Which enemy each spawner gets for a seed: CandidateFor and FamilyFirstPick of the plugin over the
    // spawners in SpoilerData.txt (trainer/build_ds1_spoiler_data.py). The one random Hunter per chapter and a
    // donor that is not loaded in time are left out.
    internal static class Spoiler
    {
        private sealed class Source
        {
            internal int Chapter;
            internal string Class, Variant;
            internal uint[] Entity, Target;
            internal bool Scripted, Fixed;
            internal int[] Candidates;
            internal string[] Needs;
        }

        private static readonly List<string[]> targets = new List<string[]>();
        private static readonly List<Source> sources = new List<Source>();
        private static readonly string[] Late = {
            "super_slasher", "super_leaper", "super_lurker", "super_brute", "twitcher", "sup_expl", "hunter"
        };

        private static uint Word(uint hash, uint value)
        {
            unchecked
            {
                hash ^= value;
                hash *= 0x85EBCA6Bu;
                hash ^= hash >> 13;
            }
            return hash;
        }

        // dllmain.cpp StableRandomizerHash.
        internal static uint Hash(uint seed, uint[] entity, uint[] target, uint salt)
        {
            uint hash = Word(0x9E3779B9u, seed ^ salt);
            for (int i = 0; i < 4; ++i) hash = Word(hash, entity[i]);
            for (int i = 0; i < 4; ++i) hash = Word(hash, target[i]);
            unchecked
            {
                hash ^= hash >> 16;
                hash *= 0x7FEB352Du;
                hash ^= hash >> 15;
            }
            return hash;
        }

        private static uint[] Guid(string text)
        {
            string[] words = text.Split('-');
            var value = new uint[4];
            for (int i = 0; i < 4; ++i) value[i] = uint.Parse(words[i], NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            return value;
        }

        private static void Load()
        {
            if (sources.Count > 0) return;
            using (Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("DS2008Spoiler"))
            using (var reader = new StreamReader(stream, Encoding.ASCII))
            {
                string line;
                while ((line = reader.ReadLine()) != null)
                {
                    string[] f = line.Split(' ');
                    if (f[0] == "T") targets.Add(new[] { f[1], f[2] });
                    if (f[0] != "S") continue;
                    var s = new Source {
                        Chapter = int.Parse(f[1], CultureInfo.InvariantCulture), Class = f[2], Variant = f[3],
                        Entity = Guid(f[4]), Target = Guid(f[5]), Scripted = f[6].Contains("s"), Fixed = f[6].Contains("f")
                    };
                    var cands = new List<int>();
                    var needs = new List<string>();
                    if (f[7] != "-")
                        foreach (string c in f[7].Split(','))
                        {
                            string[] p = c.Split(':');
                            cands.Add(int.Parse(p[0], CultureInfo.InvariantCulture));
                            needs.Add(p[1]);
                        }
                    s.Candidates = cands.ToArray();
                    s.Needs = needs.ToArray();
                    sources.Add(s);
                }
            }
        }

        private static int Number(Dictionary<string, string> values, string key, int fallback)
        {
            string text;
            int value;
            return values.TryGetValue(key, out text) && int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture,
                out value) ? value : fallback;
        }

        private static int FamilyDefault(string family)
        {
            switch (family)
            {
                case "Hunter": return 1;
                case "Lurker": return 5;
                case "DividerCore": return 3;
                case "Exploder": return 6;
                case "Brute": return 2;
                case "Pregnant": return 5;
                case "Infector": return 5;
                default: return 10;
            }
        }

        internal static string Name(string family, string variant)
        {
            switch (variant)
            {
                case "zombie": return "Slasher";
                case "zombievar1": return "Slasher (variant 1)";
                case "zombievar2": return "Slasher (variant 2)";
                case "zombiewoman": return "Female Slasher";
                case "twitcher": return "Twitcher";
                case "super_slasher": return "Enhanced Slasher";
                case "lurker": return "Lurker";
                case "super_lurker": return "Enhanced Lurker";
                case "leaper": return "Leaper";
                case "super_leaper": return "Enhanced Leaper";
                case "exploder": return "Exploder";
                case "sup_expl": return "Enhanced Exploder";
                case "infector": return "Infector";
                case "pregnant": return "Pregnant";
                case "divider": return "Divider";
                case "super_brute": return "Super Brute";
                default: return family == "SwarmEnemy" ? "Swarm" : family + " " + variant;
            }
        }

        // Per chapter (in the order played), each replacement and how often.
        internal static string Report(Dictionary<string, string> values)
        {
            Load();
            if (Number(values, "EnemyRandomizer", 0) != 1)
                return "The enemy randomizer is off, so every enemy stays as authored.";
            uint seed = SuitPaths.SeedOf(values);
            bool cross = Number(values, "EnemyRandomizerCrossFamilyContexts", 1) == 1;
            bool direct = Number(values, "EnemyRandomizerDirectSpawns", 1) == 1;
            bool heavy = Number(values, "EnemyRandomizerHeavyFamilies", 1) == 1;
            bool scripted = Number(values, "EnemyRandomizerScriptedVariants", 1) == 1;
            bool ramp = Number(values, "EnemyRandomizerDifficultyRamp", 1) == 1;
            int chance = Number(values, "EnemyRandomizerChancePercent", 100);
            int[] order = Number(values, "LevelOrderRandomizer", 0) == 1 ? SeedPreview.ChapterOrder(seed)
                                                                             : new[] { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
            var perChapter = new SortedDictionary<int, Dictionary<string, int>>();
            var kept = new Dictionary<int, int>();
            var totals = new Dictionary<string, int>();
            foreach (Source s in sources)
            {
                int position = Array.IndexOf(order, s.Chapter) + 1;
                if (position <= 0) position = s.Chapter;
                string pick = Pick(seed, s, values, cross, direct, heavy, scripted, ramp, chance, position);
                if (!perChapter.ContainsKey(position)) perChapter[position] = new Dictionary<string, int>();
                if (pick == null)
                {
                    kept[position] = (kept.ContainsKey(position) ? kept[position] : 0) + 1;
                    continue;
                }
                Dictionary<string, int> counts = perChapter[position];
                counts[pick] = (counts.ContainsKey(pick) ? counts[pick] : 0) + 1;
                totals[pick] = (totals.ContainsKey(pick) ? totals[pick] : 0) + 1;
            }
            var text = new StringBuilder();
            text.AppendLine("Seed " + seed + ": the enemies the randomizer puts at the authored spawners.");
            text.AppendLine("Not shown: the random Hunter (one per chapter at most) and More enemies extras.");
            text.AppendLine();
            foreach (KeyValuePair<int, Dictionary<string, int>> chapter in perChapter)
            {
                var list = new List<KeyValuePair<string, int>>(chapter.Value);
                list.Sort((a, b) => b.Value != a.Value ? b.Value.CompareTo(a.Value) : string.CompareOrdinal(a.Key, b.Key));
                var parts = new List<string>();
                foreach (KeyValuePair<string, int> entry in list) parts.Add(entry.Key + " " + entry.Value);
                int stays = kept.ContainsKey(chapter.Key) ? kept[chapter.Key] : 0;
                text.AppendLine("Chapter " + order[chapter.Key - 1] + (order[chapter.Key - 1] != chapter.Key
                    ? " (played " + chapter.Key + ".)" : "") + ": " + (parts.Count > 0 ? string.Join(", ", parts.ToArray()) : "nothing replaced") +
                    (stays > 0 ? "; " + stays + " stay as authored" : ""));
            }
            var all = new List<KeyValuePair<string, int>>(totals);
            all.Sort((a, b) => b.Value.CompareTo(a.Value));
            var summary = new List<string>();
            foreach (KeyValuePair<string, int> entry in all) summary.Add(entry.Key + " " + entry.Value);
            text.AppendLine();
            text.AppendLine("Whole run: " + string.Join(", ", summary.ToArray()));
            return text.ToString();
        }

        private static string Pick(uint seed, Source s, Dictionary<string, string> values, bool cross, bool direct, bool heavy,
                                   bool scripted, bool ramp, int chance, int chapter)
        {
            if (s.Fixed || (s.Scripted && !scripted)) return null;
            if (Number(values, "EnemyRandomizerSource_" + s.Class, 1) != 1) return null;
            if (chance < 100 && Hash(seed, s.Entity, s.Target, 0x43484E43u) % 100u >= (uint)chance) return null;
            int keep = Number(values, "EnemyRandomizerKeepChance_" + s.Class, 0);
            if (keep > 0 && Hash(seed, s.Entity, s.Target, 0x4B454550u) % 100u < (uint)keep) return null;
            var groups = new List<string[]>();   // family, variant
            for (int i = 0; i < s.Candidates.Length; ++i)
            {
                string need = s.Needs[i];
                if ((need.Contains("c") && !cross) || (need.Contains("d") && !direct) || (need.Contains("h") && !heavy)) continue;
                string[] t = targets[s.Candidates[i]];
                bool known = false;
                foreach (string[] g in groups) known = known || (g[0] == t[0] && g[1] == t[1]);
                if (!known) groups.Add(t);
            }
            if (groups.Count == 0) return null;
            int rampChapter = ramp ? chapter : 0;
            bool lateOnly = rampChapter > 0;
            foreach (string[] g in groups) lateOnly = lateOnly && Array.IndexOf(Late, g[1]) >= 0;
            if (lateOnly && Hash(seed, s.Entity, s.Target, 0x52414D50u) % 12u >= (uint)rampChapter) return null;
            var families = new List<string>();
            foreach (string[] g in groups) if (!families.Contains(g[0])) families.Add(g[0]);
            var scores = new ulong[families.Count];
            ulong total = 0;
            for (int f = 0; f < families.Count; ++f)
            {
                ulong weighted = 0, based = 0;
                foreach (string[] g in groups)
                {
                    if (g[0] != families[f]) continue;
                    ulong weight = (ulong)Math.Max(0, Number(values, "EnemyRandomizerWeight_" + g[1], 10));
                    weighted += rampChapter > 0 ? weight * (Array.IndexOf(Late, g[1]) >= 0 ? (ulong)rampChapter : 12UL) : weight;
                    based += rampChapter > 0 ? weight * 12UL : weight;
                }
                ulong familyWeight = (ulong)Math.Max(0, Number(values, "EnemyRandomizerFamilyWeight_" + families[f],
                    FamilyDefault(families[f])));
                scores[f] = based > 0 ? familyWeight * 1024UL * weighted / based : 0;
                total += scores[f];
            }
            if (total == 0) return null;
            ulong roll = Hash(seed, s.Entity, s.Target, 0x46414D49u) % total;
            int family = 0;
            while (family + 1 < families.Count && roll >= scores[family])
            {
                roll -= scores[family];
                ++family;
            }
            var members = new List<string>();
            var weights = new List<ulong>();
            foreach (string[] g in groups)
            {
                if (g[0] != families[family]) continue;
                ulong weight = (ulong)Math.Max(0, Number(values, "EnemyRandomizerWeight_" + g[1], 10));
                members.Add(g[1]);
                weights.Add(rampChapter > 0 ? weight * (Array.IndexOf(Late, g[1]) >= 0 ? (ulong)rampChapter : 12UL) : weight);
            }
            ulong variantTotal = 0;
            foreach (ulong w in weights) variantTotal += w;
            ulong variantRoll = variantTotal > 0 ? Hash(seed, s.Entity, s.Target, 0x56415249u) % variantTotal : 0;
            string chosen = members[members.Count - 1];
            for (int i = 0; i < members.Count; ++i)
            {
                chosen = members[i];
                if (variantTotal > 0 && variantRoll < weights[i]) break;
                if (variantTotal > 0) variantRoll -= weights[i];
            }
            return Name(families[family], chosen);
        }
    }

    // Settings that do nothing, or something else than they look like, together.
    internal static class SettingsCheck
    {
        private static bool On(Dictionary<string, string> v, string key)
        {
            string text;
            return v.TryGetValue(key, out text) && text == "1";
        }

        private static string Get(Dictionary<string, string> v, string key)
        {
            string text;
            return v.TryGetValue(key, out text) ? text : "";
        }

        internal static List<string> Warnings(Dictionary<string, string> v, Dictionary<string, string> saved, bool dlc,
                                              bool fileExisted)
        {
            var list = new List<string>();
            if (!On(v, "ProgressionRandomizer") &&
                (On(v, "ProgressionSeeded") || On(v, "ProgressionStoreGate") || On(v, "ProgressionStartingWeapons")))
                list.Add("Seed schematic rewards, Move store unlocks and First store weapons only work with Schematic " +
                         "progression on.");
            if (On(v, "EnemyRandomizer") && Get(v, "EnemyRandomizerChancePercent") == "0")
                list.Add("Replacement chance is 0 %, so the enemy randomizer changes nothing.");
            if (On(v, "EnemyRandomizer") && !On(v, "EnemyRandomizerGlobalTemplates") && !On(v, "EnemyRandomizerGlobalStreaming"))
                list.Add("Without the global enemy pool only enemies of the current level swap places, and not the same " +
                         "way on every run.");
            else if (On(v, "EnemyRandomizerGlobalTemplates") != On(v, "EnemyRandomizerGlobalStreaming"))
                list.Add("Global enemy pool and Keep enemy assets loaded work as one switch; the plugin turns both on.");
            if (On(v, "EnemyRandomizerZeroGVariants") && (!On(v, "EnemyRandomizer") || !On(v, "EnemyRandomizerGlobalTemplates")))
                list.Add("Zero-G normal / enhanced needs the enemy randomizer and the global enemy pool.");
            if (On(v, "LootAutoScale") && (Get(v, "LootAmmoPercent") != "100" || Get(v, "LootCreditsPercent") != "100"))
                list.Add("Loot scales with More enemies, so the manual Ammo and Credit amounts are not used.");
            if (!On(v, "PlayingDeadCorpses") && Get(v, "PlayingDeadPercent") != Get(saved, "PlayingDeadPercent"))
                list.Add("Playing-dead share only matters with Playing-dead enemies on.");
            if (!On(v, "StartingSuitRandomizer") && Get(v, "StartingSuitCreditShare") != Get(saved, "StartingSuitCreditShare"))
                list.Add("Suit debt share only matters with Starting suit on.");
            int path = dlc ? SuitPaths.For(v) : -1;
            if (path >= 0 && On(v, "FirstWeaponRandomizer") && SeedPreview.FirstWeapon(SuitPaths.SeedOf(v)) != 0)
                list.Add("First weapon gives its own weapon, so the suit path's Plasma Cutter does not replace the one " +
                         "in chapter 1.");
            if (path > 0 && On(v, "StartingSuitRandomizer"))
                list.Add("The " + SuitPaths.Names[path] + " suit path starts in its own level 1 suit; Starting suit " +
                         "and its debt are not used in this run.");
            bool anyFamily = false;
            foreach (KeyValuePair<string, string> entry in v)
                if (entry.Key.StartsWith("EnemyRandomizerFamilyWeight_", StringComparison.OrdinalIgnoreCase) && entry.Value != "0")
                    anyFamily = true;
            if (On(v, "EnemyRandomizer") && !anyFamily)
                list.Add("Every family weight is 0, so nothing can be replaced.");
            foreach (KeyValuePair<string, string> entry in v)
            {
                int count;
                if (entry.Key.StartsWith("EnemyType_", StringComparison.OrdinalIgnoreCase) &&
                    int.TryParse(entry.Value, out count) && count > 4)
                {
                    list.Add("More than 4 enemies per trigger (" + entry.Key.Substring(10) + " " + count + ") can overload " +
                             "rooms and the sound banks; expect slowdowns.");
                    break;
                }
            }
            if (fileExisted && Get(v, "EnemyRandomizerSeed") != Get(saved, "EnemyRandomizerSeed"))
                list.Add("The seed changed: start a new game. A save from the old seed keeps what it already has.");
            return list;
        }
    }

    // One zip on the desktop with what a bug report needs.
    internal static class BugReport
    {
        private static void Add(ZipArchive zip, string path, string name, List<string> added)
        {
            if (!File.Exists(path)) return;
            try
            {
                ZipArchiveEntry entry = zip.CreateEntry(name, CompressionLevel.Optimal);
                using (var source = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (Stream target = entry.Open())
                    source.CopyTo(target);
                added.Add(name);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        private static string Describe(string path)
        {
            if (!File.Exists(path)) return "missing";
            var info = new FileInfo(path);
            return info.Length + " bytes, " + info.LastWriteTime.ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture);
        }

        internal static string Create(string iniPath, string seedCode)
        {
            string folder = Path.GetDirectoryName(iniPath) ?? ".";
            string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            string zipPath = Path.Combine(desktop, "DeadSpace2008Randomizer_bugreport_" +
                DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture) + ".zip");
            var added = new List<string>();
            using (var file = new FileStream(zipPath, FileMode.CreateNew))
            using (var zip = new ZipArchive(file, ZipArchiveMode.Create))
            {
                foreach (string name in new[] { "DeadSpace2008Randomizer.log", "DeadSpace2008Randomizer.prev.log",
                                                "DeadSpace2008Randomizer.ini", "DeadSpace2008Randomizer_RunState.ini",
                                                "DeadSpace2008Randomizer_SuitPaths.ini" })
                    Add(zip, Path.Combine(folder, name), name, added);
                // The newest three crashes with their log copies; the .dmp files are too large to mail.
                var crashes = new List<FileInfo>();
                foreach (string place in new[] { folder, @"C:\DS1CrashDumps" })
                    if (Directory.Exists(place))
                        foreach (string path in Directory.GetFiles(place, "DeadSpace2008Randomizer.crash.*.txt"))
                            crashes.Add(new FileInfo(path));
                crashes.Sort((a, b) => b.LastWriteTimeUtc.CompareTo(a.LastWriteTimeUtc));
                for (int i = 0; i < crashes.Count && i < 3; ++i)
                {
                    Add(zip, crashes[i].FullName, "crashes/" + crashes[i].Name, added);
                    string log = Path.ChangeExtension(crashes[i].FullName, ".log");
                    Add(zip, log, "crashes/" + Path.GetFileName(log), added);
                }
                var info = new StringBuilder();
                info.AppendLine("Dead Space 2008 Randomizer bug report, " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss",
                    CultureInfo.InvariantCulture));
                info.AppendLine("Configurator 1.0.0, game folder " + folder);
                info.AppendLine("Seed code: " + seedCode);
                info.AppendLine("Dead Space.exe: " + Describe(Path.Combine(folder, "Dead Space.exe")));
                info.AppendLine("winmm.dll (ASI loader): " + Describe(Path.Combine(folder, "winmm.dll")));
                info.AppendLine("plugins\\DeadSpace2008Randomizer.asi: " +
                    Describe(Path.Combine(folder, "plugins", "DeadSpace2008Randomizer.asi")));
                info.AppendLine("plugins\\DeadSpace2008Randomizer_ConsoleDLC.asi: " +
                    Describe(Path.Combine(folder, "plugins", "DeadSpace2008Randomizer_ConsoleDLC.asi")));
                if (Directory.Exists(Path.Combine(folder, "plugins")))
                    foreach (string asi in Directory.GetFiles(Path.Combine(folder, "plugins"), "*.asi"))
                        info.AppendLine("  plugin present: " + Path.GetFileName(asi));
                info.AppendLine("Files: " + string.Join(", ", added.ToArray()));
                ZipArchiveEntry readme = zip.CreateEntry("report.txt", CompressionLevel.Optimal);
                using (var writer = new StreamWriter(readme.Open(), new UTF8Encoding(false)))
                    writer.Write(info.ToString());
            }
            return zipPath;
        }
    }
}
