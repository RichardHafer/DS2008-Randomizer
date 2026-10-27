using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace DS2008RandomizerConfig
{
    internal static class IniDocument
    {
        private const string Section = "[DeadSpace2008Randomizer]";
        private const string BackupStamp = "yyyyMMdd-HHmmss-fff";
        private static readonly Encoding Ansi = Encoding.GetEncoding(1252);

        // Keeps the newest save backups; backups named any other way are left alone.
        private static void PruneBackups(string path, int keep)
        {
            try
            {
                string folder = Path.GetDirectoryName(Path.GetFullPath(path));
                string prefix = Path.GetFileName(path) + ".bak-";
                var ours = new List<string>();
                foreach (string file in Directory.GetFiles(folder, prefix + "*"))
                {
                    string stamp = Path.GetFileName(file).Substring(prefix.Length);
                    DateTime parsed;
                    if (DateTime.TryParseExact(stamp, BackupStamp, System.Globalization.CultureInfo.InvariantCulture,
                                               System.Globalization.DateTimeStyles.None, out parsed))
                        ours.Add(file);
                }
                ours.Sort(StringComparer.OrdinalIgnoreCase);
                for (int i = 0; i < ours.Count - keep; ++i)
                    File.Delete(ours[i]);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        internal static Dictionary<string, string> ReadValues(string[] source)
        {
            var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            bool section = false;
            foreach (string raw in source)
            {
                string line = raw.Trim();
                if (line.StartsWith("["))
                {
                    section = string.Equals(line, Section, StringComparison.OrdinalIgnoreCase);
                    continue;
                }
                if (!section || line.StartsWith(";") || line.StartsWith("#")) continue;
                int equals = line.IndexOf('=');
                if (equals > 0)
                {
                    string value = line.Substring(equals + 1);
                    int comment = InlineCommentIndex(value);
                    if (comment >= 0) value = value.Substring(0, comment);
                    result[line.Substring(0, equals).Trim()] = value.Trim();
                }
            }
            return result;
        }

        internal static string[] UpdateLines(string[] source, Dictionary<string, string> values)
        {
            var output = new List<string>();
            var pending = new Dictionary<string, string>(values, StringComparer.OrdinalIgnoreCase);
            bool section = false;
            bool found = false;
            foreach (string raw in source)
            {
                string trimmed = raw.Trim();
                if (trimmed.StartsWith("["))
                {
                    if (section)
                    {
                        foreach (var item in pending) output.Add(item.Key + "=" + item.Value);
                        pending.Clear();
                    }
                    section = string.Equals(trimmed, Section, StringComparison.OrdinalIgnoreCase);
                    found |= section;
                }
                if (section && !trimmed.StartsWith(";") && !trimmed.StartsWith("#"))
                {
                    int equals = raw.IndexOf('=');
                    if (equals > 0)
                    {
                        string key = raw.Substring(0, equals).Trim();
                        string value;
                        if (values.TryGetValue(key, out value))
                        {
                            string oldValue = raw.Substring(equals + 1);
                            int comment = InlineCommentIndex(oldValue);
                            string suffix = "";
                            if (comment >= 0)
                            {
                                int start = comment;
                                while (start > 0 && char.IsWhiteSpace(oldValue[start - 1])) --start;
                                suffix = oldValue.Substring(start);
                            }
                            output.Add(raw.Substring(0, equals + 1) + value + suffix);
                            pending.Remove(key);
                            continue;
                        }
                    }
                }
                output.Add(raw);
            }
            if (!found) throw new InvalidDataException(Section + " is missing.");
            if (section) foreach (var item in pending) output.Add(item.Key + "=" + item.Value);
            return output.ToArray();
        }

        private static int InlineCommentIndex(string value)
        {
            for (int i = 0; i < value.Length; ++i)
                if (value[i] == ';' && (i == 0 || char.IsWhiteSpace(value[i - 1])))
                    return i;
            return -1;
        }

        // The INI beside the program. An INI from before the rename (DeadSpace1Patch.ini) is copied once under
        // the new name with its section renamed; the old file stays.
        internal static string FindBeside(string folder)
        {
            string current = Path.Combine(folder, "DeadSpace2008Randomizer.ini");
            string legacy = Path.Combine(folder, "DeadSpace1Patch.ini");
            if (File.Exists(current) || !File.Exists(legacy)) return current;
            try
            {
                string text = File.ReadAllText(legacy, Ansi);
                File.WriteAllText(current, text.Replace("[DeadSpace1Patch]", "[DeadSpace2008Randomizer]"), Ansi);
                return current;
            }
            catch (IOException) { return legacy; }
            catch (UnauthorizedAccessException) { return legacy; }
        }

        internal static string[] Load(string path)
        {
            if (!File.Exists(path)) throw new FileNotFoundException("Select an existing DeadSpace2008Randomizer.ini.", path);
            byte[] bytes = File.ReadAllBytes(path);
            Encoding encoding = EncodingFor(bytes);
            string text = encoding.GetString(bytes);
            if (text.Length > 0 && text[0] == '\uFEFF') text = text.Substring(1);
            return text.Split(new[] { "\r\n", "\n", "\r" }, StringSplitOptions.None);
        }

        private static Encoding EncodingFor(byte[] bytes)
        {
            if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
                return new UTF8Encoding(true);
            if (bytes.Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
                return Encoding.Unicode;
            return Ansi;
        }

        internal static string Save(string path, Dictionary<string, string> changed)
        {
            if (changed.Count == 0) return null;
            string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            try
            {
                byte[] original = File.ReadAllBytes(path);
                Encoding encoding = EncodingFor(original);
                string text = encoding.GetString(original);
                if (text.Length > 0 && text[0] == '\uFEFF') text = text.Substring(1);
                string newline = text.Contains("\r\n") ? "\r\n" : text.Contains("\n") ? "\n" : "\r";
                string[] lines = text.Split(new[] { "\r\n", "\n", "\r" }, StringSplitOptions.None);
                string updated = string.Join(newline, UpdateLines(lines, changed));
                byte[] payload = encoding.GetPreamble();
                byte[] body = encoding.GetBytes(updated);
                using (var stream = new FileStream(temp, FileMode.CreateNew, FileAccess.Write))
                {
                    stream.Write(payload, 0, payload.Length);
                    stream.Write(body, 0, body.Length);
                }
                string backup = path + ".bak-" + DateTime.Now.ToString(BackupStamp);
                File.Replace(temp, path, backup);
                temp = null;
                PruneBackups(path, 5);
                return backup;
            }
            finally
            {
                if (temp != null && File.Exists(temp)) File.Delete(temp);
            }
        }
    }
}
