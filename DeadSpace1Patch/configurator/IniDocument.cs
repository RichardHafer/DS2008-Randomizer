using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace DS2008RandomizerConfig
{
    internal static class IniDocument
    {
        private const string Section = "[DeadSpace1Patch]";
        private static readonly Encoding Ansi = Encoding.GetEncoding(1252);

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

        internal static string[] Load(string path)
        {
            if (!File.Exists(path)) throw new FileNotFoundException("Select an existing DeadSpace1Patch.ini.", path);
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
                string backup = path + ".bak-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff");
                File.Replace(temp, path, backup);
                temp = null;
                return backup;
            }
            finally
            {
                if (temp != null && File.Exists(temp)) File.Delete(temp);
            }
        }
    }
}
