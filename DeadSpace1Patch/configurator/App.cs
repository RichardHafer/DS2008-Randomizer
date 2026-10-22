using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
using DS2CoopConfig;
using ConfigSwitch = DS2CoopConfig.Switch;

[assembly: AssemblyTitle("DS2008 Randomizer Configurator")]
[assembly: AssemblyVersion("1.0.0.0")]

namespace DS2008RandomizerConfig
{
    internal sealed class ConfigForm : Form
    {
        private readonly Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, string> baseline = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, Surface> cards = new Dictionary<string, Surface>(StringComparer.OrdinalIgnoreCase);
        private NavList navigation;
        private readonly ToolTip tips = new ToolTip();
        private readonly TextBox search = new TextBox();
        private readonly Label pathBox = new Label();
        private readonly FlowLayoutPanel page = new FlowLayoutPanel();
        private readonly Panel sidebar = new Panel();
        private readonly Panel banner = new Panel();
        private readonly Panel footer = new Panel();
        private readonly StatusLine status = new StatusLine();
        private readonly Button saveButton = new FlatButton(true);
        private readonly Bitmap bannerArtwork;
        private string selectedGroup = "General";
        private string filePath;
        private bool loading;

        internal ConfigForm(string initialPath)
        {
            Text = "DS2008 Randomizer Configurator v1.0.9";
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            ShowIcon = true;
            FormBorderStyle = FormBorderStyle.Sizable;
            AutoScaleMode = AutoScaleMode.Dpi;
            BackColor = Theme.Back;
            ForeColor = Theme.Text;
            Font = Theme.Body;
            Size = new Size(1280, 820);
            MinimumSize = new Size(900, 600);
            StartPosition = FormStartPosition.CenterScreen;
            KeyPreview = true;
            using (Stream source = Assembly.GetExecutingAssembly().GetManifestResourceStream("DS2008Banner"))
                bannerArtwork = new Bitmap(source);

            page.Dock = DockStyle.Fill;
            page.AutoScroll = true;
            page.FlowDirection = FlowDirection.TopDown;
            page.WrapContents = false;
            page.BackColor = Theme.Back;
            page.Padding = new Padding(28, 18, 16, 22);
            page.Resize += delegate { ResizeCards(); };
            page.HandleCreated += delegate { NativeTheme.Scroll(page); };

            sidebar.Dock = DockStyle.Left;
            sidebar.Width = 244;
            sidebar.BackColor = Theme.Panel;
            sidebar.Paint += delegate(object sender, PaintEventArgs e) {
                using (var pen = new Pen(Theme.Border))
                    e.Graphics.DrawLine(pen, sidebar.Width - 1, 0, sidebar.Width - 1, sidebar.Height);
            };
            BuildSidebar();

            banner.Dock = DockStyle.Top;
            banner.Height = 104;
            banner.BackColor = Theme.Panel;
            BuildBanner();

            footer.Dock = DockStyle.Bottom;
            footer.Height = 62;
            footer.BackColor = Theme.Panel;
            BuildFooter();

            Controls.Add(page);
            Controls.Add(sidebar);
            Controls.Add(footer);
            Controls.Add(banner);

            foreach (Option option in Catalog.Options) values[option.Key] = option.Default;
            foreach (Option option in Catalog.Options) baseline[option.Key] = option.Default;
            ShowPage();
            if (!string.IsNullOrEmpty(initialPath) && File.Exists(initialPath))
                LoadIni(initialPath);
            else
                SetStatus("Select the DeadSpace1Patch.ini beside Dead Space.exe.");
        }

        private static Label Label(string text, Font font, Color color)
        {
            return new Label { Text = text, Font = font, ForeColor = color,
                BackColor = Color.Transparent, AutoEllipsis = true,
                UseMnemonic = false, TextAlign = ContentAlignment.MiddleLeft };
        }

        private static Button Button(string text, bool primary)
        {
            var button = Look.MakeButton(text, primary, 32);
            button.UseMnemonic = false;
            return button;
        }

        private void BuildBanner()
        {
            var stripe = new Panel { Dock = DockStyle.Top, Height = 3, BackColor = Theme.Accent };
            banner.Controls.Add(stripe);
            var divider = new Panel { BackColor = Theme.Border };
            divider.SetBounds(411, 23, 1, 62);
            banner.Controls.Add(divider);
            var kicker = Label("DEAD SPACE 2008", Theme.Small, Theme.Accent);
            kicker.SetBounds(435, 16, 270, 20);
            banner.Controls.Add(kicker);
            var title = Label("Randomizer settings", new Font("Segoe UI Semibold", 14f), Theme.Text);
            title.SetBounds(435, 34, 400, 31);
            banner.Controls.Add(title);
            pathBox.BackColor = Color.Transparent;
            pathBox.ForeColor = Theme.Muted;
            pathBox.Font = Theme.Small;
            pathBox.TextAlign = ContentAlignment.MiddleLeft;
            pathBox.AutoEllipsis = true;
            pathBox.Text = "No INI selected";
            pathBox.SetBounds(436, 70, 432, 20);
            banner.Controls.Add(pathBox);
            var choose = Button("Choose INI...", false);
            choose.SetBounds(0, 34, 120, 31);
            choose.Click += delegate { Browse(); };
            banner.Controls.Add(choose);
            banner.Resize += delegate {
                choose.Left = banner.Width - 141;
                title.Width = Math.Max(180, choose.Left - title.Left - 20);
                pathBox.Width = Math.Max(160, choose.Left - pathBox.Left - 18);
            };
            banner.Paint += delegate(object sender, PaintEventArgs e) {
                using (var background = new System.Drawing.Drawing2D.LinearGradientBrush(
                    banner.ClientRectangle, Theme.Hex(0x191b1f), Theme.Hex(0x111215),
                    System.Drawing.Drawing2D.LinearGradientMode.Vertical))
                    e.Graphics.FillRectangle(background, banner.ClientRectangle);
                e.Graphics.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                e.Graphics.DrawImage(bannerArtwork, new Rectangle(11, 9, 390, 87),
                    new Rectangle(185, 365, 1537, 343), GraphicsUnit.Pixel);
                using (var pen = new Pen(Theme.Border))
                    e.Graphics.DrawLine(pen, 0, banner.Height - 1, banner.Width, banner.Height - 1);
            };
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && bannerArtwork != null) bannerArtwork.Dispose();
            base.Dispose(disposing);
        }

        private void BuildSidebar()
        {
            search.BorderStyle = BorderStyle.None;
            search.BackColor = Theme.Input;
            search.ForeColor = Theme.Text;
            search.SetBounds(48, 22, 169, 20);
            search.HandleCreated += delegate { NativeTheme.CueBanner(search, "Search options"); };
            search.TextChanged += delegate { ShowPage(); };
            sidebar.Controls.Add(search);
            sidebar.Paint += delegate(object sender, PaintEventArgs e) {
                using (var fill = new SolidBrush(Theme.Input))
                    e.Graphics.FillRectangle(fill, 17, 16, sidebar.Width - 34, 31);
                using (var border = new Pen(Theme.InputBorder))
                    e.Graphics.DrawRectangle(border, 17, 16, sidebar.Width - 35, 30);
                e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                using (var pen = new Pen(Theme.Muted, 1.6f))
                {
                    e.Graphics.DrawEllipse(pen, 28, 25, 10, 10);
                    e.Graphics.DrawLine(pen, 37, 34, 42, 39);
                }
            };
            navigation = new NavList(Catalog.Groups, new[] { 4, 7 });
            navigation.SelectedChanged += delegate {
                selectedGroup = Catalog.Groups[navigation.SelectedIndex];
                search.Text = "";
                ShowPage();
            };
            sidebar.Controls.Add(navigation);
            var note = Label("Changes take effect on the next launch.\nOptions not shown here stay untouched.",
                             Theme.Small, Theme.Muted);
            note.SetBounds(18, 0, 210, 62);
            sidebar.Controls.Add(note);
            sidebar.Resize += delegate {
                note.Top = sidebar.Height - 83;
                navigation.SetBounds(0, 64, sidebar.Width - 1, Math.Max(0, note.Top - 70));
            };
        }

        private void BuildFooter()
        {
            var openFolder = Button("Open INI folder", false);
            openFolder.SetBounds(16, 14, 134, 34);
            openFolder.Click += delegate {
                if (filePath != null) Process.Start("explorer.exe", "/select,\"" + filePath + "\"");
            };
            footer.Controls.Add(openFolder);

            var openLog = Button("Open log", false);
            openLog.SetBounds(158, 14, 86, 34);
            openLog.Click += delegate {
                if (filePath == null) return;
                string path = Path.Combine(Path.GetDirectoryName(filePath), "DeadSpace1Patch.log");
                if (File.Exists(path)) Process.Start(path);
                else MessageBox.Show(this, "No DeadSpace1Patch.log found beside the INI.",
                    "Open log", MessageBoxButtons.OK, MessageBoxIcon.Information);
            };
            footer.Controls.Add(openLog);

            status.SetBounds(270, 14, 450, 34);
            status.Font = Look.Base;
            status.BackColor = Theme.Panel;
            footer.Controls.Add(status);

            var reset = Button("Reset all to recommended", false);
            reset.SetBounds(0, 14, 190, 34);
            reset.Click += delegate { ResetAll(); };
            footer.Controls.Add(reset);
            var reload = Button("Reload", false);
            reload.SetBounds(0, 14, 83, 34);
            reload.Click += delegate { if (filePath != null && ConfirmDiscard()) LoadIni(filePath); };
            footer.Controls.Add(reload);
            saveButton.Text = "Save";
            saveButton.SetBounds(0, 14, 80, 34);
            saveButton.Click += delegate { Save(); };
            footer.Controls.Add(saveButton);
            footer.Resize += delegate {
                saveButton.Left = footer.Width - 96;
                reload.Left = saveButton.Left - 91;
                reset.Left = reload.Left - 198;
                status.Width = Math.Max(100, reset.Left - status.Left - 24);
            };
            footer.Paint += delegate(object sender, PaintEventArgs e) {
                using (var pen = new Pen(Theme.Border))
                    e.Graphics.DrawLine(pen, 0, 0, footer.Width, 0);
            };
        }

        private void ShowPage()
        {
            if (page == null) return;
            page.SuspendLayout();
            var oldControls = new List<Control>();
            foreach (Control control in page.Controls) oldControls.Add(control);
            page.Controls.Clear();
            foreach (Control control in oldControls) control.Dispose();
            cards.Clear();
            string query = search.Text.Trim();
            if (navigation != null)
            {
                navigation.Matches = query.Length == 0 ? null : new int[Catalog.Groups.Length];
                navigation.Changed = new bool[Catalog.Groups.Length];
                foreach (Option option in Catalog.Options)
                {
                    int index = Array.IndexOf(Catalog.Groups, option.Group);
                    if (index < 0) continue;
                    if (values[option.Key] != baseline[option.Key]) navigation.Changed[index] = true;
                    if (navigation.Matches != null &&
                        (option.Title + " " + option.Description + " " + option.Key + " " + option.Group)
                        .IndexOf(query, StringComparison.OrdinalIgnoreCase) >= 0)
                        ++navigation.Matches[index];
                }
                navigation.Invalidate();
            }
            var heading = new Panel { Height = 66, BackColor = Theme.Back, Margin = new Padding(0, 0, 0, 8) };
            var title = Label(query.Length > 0 ? "Search results" : selectedGroup, Theme.Heading, Theme.Text);
            title.SetBounds(5, 2, 600, 34);
            heading.Controls.Add(title);
            var intro = Label(query.Length > 0 ? "Matching settings across the configurator." : GroupIntro(selectedGroup),
                              Theme.Body, Theme.Secondary);
            intro.SetBounds(5, 38, 780, 24);
            heading.Controls.Add(intro);
            page.Controls.Add(heading);
            int count = 0;
            foreach (Option option in Catalog.Options)
            {
                if (query.Length == 0 && option.Group != selectedGroup) continue;
                if (query.Length > 0 &&
                    (option.Title + " " + option.Description + " " + option.Key + " " + option.Group)
                    .IndexOf(query, StringComparison.OrdinalIgnoreCase) < 0) continue;
                page.Controls.Add(BuildCard(option));
                ++count;
            }
            if (count == 0)
                page.Controls.Add(Label("No settings match your search.", Theme.Body, Theme.Muted));
            page.ResumeLayout(true);
            ResizeCards();
            UpdateStatus();
        }

        private static string GroupIntro(string group)
        {
            switch (group)
            {
                case "General": return "The settings most players change. Restart the game after saving.";
                case "Enemy sources": return "Choose which authored enemies may change. Only reviewed spawn contracts can be randomized.";
                case "Replacements": return "Set replacement odds. Zero removes a family or variant; compatibility still limits every choice.";
                case "Enemy behavior": return "Control encounter rules and which resources the replacement pool loads.";
                case "More enemies": return "Increase encounter pressure and tune special variants.";
                case "Loot & items": return "Adjust drops and pickup locations without changing existing inventory.";
                case "Progression": return "Weapons, schematics, suits and store unlocks for a new run.";
                case "World": return "Campaign-wide rules that change the order and survival rhythm.";
                default: return "Quality-of-life and diagnostic controls.";
            }
        }

        private Surface BuildCard(Option option)
        {
            var card = new Surface { Rounded = true, BackColor = Look.Card,
                Height = option.Note.Length > 0 ? 153 : 116,
                Margin = new Padding(0, 0, 0, 10) };
            card.Changed = values[option.Key] != baseline[option.Key];
            cards[option.Key] = card;
            var title = Label(option.Title, Theme.Bold, Theme.Text);
            title.SetBounds(18, 15, 270, 23);
            card.Controls.Add(title);
            var key = Label("[DeadSpace1Patch] " + option.Key, Theme.Small, Theme.Muted);
            key.SetBounds(18, 38, 310, 18);
            card.Controls.Add(key);
            var description = Label(option.Description, Theme.Body, Theme.Secondary);
            description.AutoEllipsis = false;
            description.SetBounds(18, 67, 750, 36);
            card.Controls.Add(description);
            Control editor;
            if (option.Kind == ValueKind.Toggle)
            {
                var toggle = new ConfigSwitch { Checked = values[option.Key] == "1",
                    BackColor = Look.Card, ForeColor = Look.Text };
                toggle.Text = toggle.Checked ? "On" : "Off";
                toggle.FitText("Off");
                toggle.CheckedChanged += delegate {
                    toggle.Text = toggle.Checked ? "On" : "Off";
                    SetValue(option, toggle.Checked ? "1" : "0", card);
                };
                editor = toggle;
            }
            else
            {
                var box = new TextBox { Text = values[option.Key], BorderStyle = BorderStyle.FixedSingle,
                    BackColor = Theme.Input, ForeColor = Theme.Text, Font = Theme.Body,
                    TextAlign = HorizontalAlignment.Right };
                box.TextChanged += delegate { SetValue(option, box.Text.Trim(), card); };
                editor = box;
            }
            card.Controls.Add(editor);
            var recommended = Label("Recommended: " + FriendlyValue(option, option.Recommended),
                                    Theme.Small, Theme.Muted);
            recommended.SetBounds(0, 19, 145, 21);
            card.Controls.Add(recommended);
            var reset = Button("Reset", false);
            reset.Size = new Size(71, 28);
            reset.Top = 15;
            reset.Click += delegate {
                if (editor is ConfigSwitch) ((ConfigSwitch)editor).Checked = option.Recommended == "1";
                else editor.Text = option.Recommended;
            };
            card.Controls.Add(reset);
            if (option.Kind == ValueKind.Seed)
            {
                var generate = Button("New", false);
                generate.Size = new Size(52, 28);
                generate.Top = 15;
                generate.Click += delegate { editor.Text = BitConverter.ToUInt32(Guid.NewGuid().ToByteArray(), 0).ToString(); };
                card.Controls.Add(generate);
                generate.Tag = "generate";
            }
            MessageLine note = null;
            if (option.Note.Length > 0)
            {
                note = new MessageLine(card, Tone.Note);
                note.Set(option.Note);
            }
            card.Resize += delegate {
                int right = card.Width;
                int pad = 18;
                bool stacked = right < 710;
                int column = Math.Max(220, Math.Min(300, right * 28 / 100));
                title.Width = stacked ? right - 130 : column;
                int inputX = stacked ? pad : pad + column + 20;
                key.SetBounds(pad, stacked ? 38 : 44, right - 2 * pad, 18);
                int inputY = stacked ? 66 : 15;
                int inputWidth = option.Kind == ValueKind.Toggle ? editor.Width :
                    option.Kind == ValueKind.Seed ? 112 : 125;
                editor.SetBounds(inputX, inputY + (option.Kind == ValueKind.Toggle ? 2 : 3),
                    inputWidth, option.Kind == ValueKind.Toggle ? 24 : 28);
                int afterInput = inputX + inputWidth;
                foreach (Control child in card.Controls)
                    if (child.Tag is string && (string)child.Tag == "generate")
                    {
                        child.SetBounds(afterInput + 10, inputY, 52, 28);
                        afterInput = child.Right;
                    }
                reset.Left = right - pad - reset.Width;
                recommended.Left = afterInput + 20;
                recommended.Top = inputY + 4;
                recommended.Width = Math.Max(100, reset.Left - recommended.Left - 10);
                description.Top = stacked ? 107 : 67;
                description.Width = right - 2 * pad;
                if (note != null)
                {
                    note.Place(pad, stacked ? 150 : 110, right - 2 * pad);
                }
                int height = stacked ? (note == null ? 156 : Math.Max(193, note.Rect.Bottom + 14)) :
                    (note == null ? 116 : Math.Max(153, note.Rect.Bottom + 14));
                if (card.Height != height) card.Height = height;
                card.Invalidate();
            };
            tips.SetToolTip(key, option.Key);
            return card;
        }

        private static string FriendlyValue(Option option, string value)
        {
            if (option.Kind == ValueKind.Toggle) return value == "1" ? "On" : "Off";
            return option.Kind == ValueKind.Seed ? DisplaySeed(value) : value;
        }

        private static string DisplaySeed(string value)
        {
            if (!value.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) return value;
            uint number;
            return uint.TryParse(value.Substring(2), System.Globalization.NumberStyles.AllowHexSpecifier,
                System.Globalization.CultureInfo.InvariantCulture, out number) ? number.ToString() : value;
        }

        private void ResizeCards()
        {
            int width = Math.Max(520, page.ClientSize.Width - page.Padding.Left - page.Padding.Right - 28);
            foreach (Control control in page.Controls)
                if (control.Width != width) control.Width = width;
        }

        private void SetValue(Option option, string value, Surface card)
        {
            if (loading) return;
            values[option.Key] = value;
            card.Changed = value != baseline[option.Key];
            card.Invalidate();
            UpdateStatus();
        }

        private int ChangeCount()
        {
            int count = 0;
            foreach (Option option in Catalog.Options)
                if (values[option.Key] != baseline[option.Key]) ++count;
            return count;
        }

        private void UpdateStatus()
        {
            int count = ChangeCount();
            string message = filePath == null ? "Choose an INI to save changes" :
                count == 0 ? "No unsaved changes" : count + " unsaved change" + (count == 1 ? "" : "s");
            status.Set(message, count > 0 ? Tone.Pending : Tone.Neutral);
            if (navigation != null)
            {
                Array.Clear(navigation.Changed, 0, navigation.Changed.Length);
                foreach (Option option in Catalog.Options)
                {
                    if (values[option.Key] == baseline[option.Key]) continue;
                    int index = Array.IndexOf(Catalog.Groups, option.Group);
                    if (index >= 0) navigation.Changed[index] = true;
                }
                navigation.Invalidate();
            }
            saveButton.Enabled = filePath != null && count > 0;
            saveButton.BackColor = saveButton.Enabled ? Look.Accent : Look.Card;
            saveButton.ForeColor = saveButton.Enabled ? Color.White : Look.Muted;
            saveButton.FlatAppearance.BorderColor = saveButton.Enabled ? Look.Accent : Look.Border;
            footer.Invalidate();
        }

        private void SetStatus(string message)
        {
            status.Set(message, Tone.Neutral);
        }

        private bool ConfirmDiscard()
        {
            return ChangeCount() == 0 || MessageBox.Show(this,
                "Discard your unsaved changes?", "Reload settings",
                MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;
        }

        private void Browse()
        {
            if (!ConfirmDiscard()) return;
            using (var dialog = new OpenFileDialog())
            {
                dialog.Title = "Select DeadSpace1Patch.ini beside Dead Space.exe";
                dialog.Filter = "DeadSpace1Patch.ini|DeadSpace1Patch.ini|INI files|*.ini";
                dialog.FileName = "DeadSpace1Patch.ini";
                if (dialog.ShowDialog(this) == DialogResult.OK) LoadIni(dialog.FileName);
            }
        }

        private void LoadIni(string path)
        {
            try
            {
                string full = Path.GetFullPath(path);
                Dictionary<string, string> loaded = IniDocument.ReadValues(IniDocument.Load(full));
                if (loaded.Count == 0) throw new InvalidDataException("[DeadSpace1Patch] is missing or empty.");
                loading = true;
                foreach (Option option in Catalog.Options)
                {
                    string value;
                    value = loaded.TryGetValue(option.Key, out value) ? value : option.Default;
                    values[option.Key] = option.Kind == ValueKind.Seed ? DisplaySeed(value) : value;
                    baseline[option.Key] = values[option.Key];
                }
                loading = false;
                filePath = full;
                pathBox.Text = full;
                tips.SetToolTip(pathBox, full);
                ShowPage();
                SetStatus("Loaded " + Path.GetFileName(full) + ". Restart after saving.");
            }
            catch (Exception error)
            {
                loading = false;
                MessageBox.Show(this, error.Message, "Could not load INI", MessageBoxButtons.OK, MessageBoxIcon.Error);
                SetStatus("Load failed: " + error.Message);
            }
        }

        private void ResetAll()
        {
            foreach (Option option in Catalog.Options) values[option.Key] = option.Recommended;
            ShowPage();
            SetStatus("Recommended values selected. Review them before saving.");
        }

        private static string Normalized(Option option, string raw)
        {
            if (option.Kind == ValueKind.Toggle) return raw == "1" ? "1" : "0";
            if (option.Kind == ValueKind.Seed)
            {
                string seed = raw.Trim();
                uint number;
                if (seed.Length == 0 || !uint.TryParse(seed,
                    System.Globalization.NumberStyles.None,
                    System.Globalization.CultureInfo.InvariantCulture, out number))
                    throw new InvalidDataException("Seed must be a whole number from 0 to 4294967295.");
                return number.ToString(System.Globalization.CultureInfo.InvariantCulture);
            }
            int value;
            if (!int.TryParse(raw, out value) || value < option.Minimum || value > option.Maximum)
                throw new InvalidDataException(option.Title + " must be between " +
                                               option.Minimum + " and " + option.Maximum + ".");
            return value.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }

        internal static bool SeedSelfTest()
        {
            Option seed = Catalog.Options[1];
            if (DisplaySeed("0x00D5A11") != "875025") return false;
            if (Normalized(seed, "4294967295") != "4294967295") return false;
            try { Normalized(seed, "4294967296"); return false; }
            catch (InvalidDataException) { }
            try { Normalized(seed, "0x00D5A11"); return false; }
            catch (InvalidDataException) { }
            return true;
        }

        private void Save()
        {
            if (filePath == null) return;
            try
            {
                var changed = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                foreach (Option option in Catalog.Options)
                {
                    if (values[option.Key] == baseline[option.Key]) continue;
                    changed.Add(option.Key, Normalized(option, values[option.Key]));
                }
                string backup = IniDocument.Save(filePath, changed);
                LoadIni(filePath);
                if (backup != null) SetStatus("Saved. Backup: " + Path.GetFileName(backup) + ". Restart to apply.");
            }
            catch (Exception error)
            {
                MessageBox.Show(this, error.Message, "Could not save INI", MessageBoxButtons.OK, MessageBoxIcon.Error);
                SetStatus("Save failed: " + error.Message);
            }
        }

        internal void Render(string path, int width, int height)
        {
            StartPosition = FormStartPosition.Manual;
            Location = new Point(-10000, -10000);
            if (width > 0 && height > 0) ClientSize = new Size(width, height);
            Show();
            Application.DoEvents();
            using (var bitmap = new Bitmap(Width, Height))
            {
                DrawToBitmap(bitmap, new Rectangle(0, 0, Width, Height));
                bitmap.Save(path);
            }
            Close();
        }

        internal void SelectGroup(string group)
        {
            int index = Array.IndexOf(Catalog.Groups, group);
            if (index < 0) return;
            navigation.SelectedIndex = index;
        }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main(string[] args)
        {
            if (args.Length == 1 && args[0] == "--self-test")
            {
                var changed = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                changed.Add("EnemyRandomizer", "1");
                changed.Add("EnemyRandomizerZeroGVariants", "1");
                string joined = string.Join("\n", IniDocument.UpdateLines(new[] {
                    "[DeadSpace1Patch]", "; user comment", "EnemyRandomizer=0",
                    "CustomSetting=kept", "[SpawnerRules]", "some_spawner=keep"
                }, changed));
                if (!joined.Contains("EnemyRandomizer=1") ||
                    !joined.Contains("EnemyRandomizerZeroGVariants=1\n[SpawnerRules]") ||
                    !joined.Contains("CustomSetting=kept") ||
                    !joined.Contains("some_spawner=keep") ||
                    !joined.Contains("; user comment"))
                    Environment.ExitCode = 1;
                string[] commented = { "[DeadSpace1Patch]", "EnemyType_Zombie=1   ; original note" };
                if (IniDocument.ReadValues(commented)["EnemyType_Zombie"] != "1" ||
                    IniDocument.UpdateLines(commented, new Dictionary<string, string> {
                        { "EnemyType_Zombie", "3" }
                    })[1] != "EnemyType_Zombie=3   ; original note")
                    Environment.ExitCode = 1;
                var keys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (Option option in Catalog.Options)
                    if (!keys.Add(option.Key) || Array.IndexOf(Catalog.Groups, option.Group) < 0)
                        Environment.ExitCode = 1;
                if (!ConfigForm.SeedSelfTest()) Environment.ExitCode = 1;
                return;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Look.Init(0f);
            string initial = args.Length == 1 && !args[0].StartsWith("--") ? args[0] :
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "DeadSpace1Patch.ini");
            using (var form = new ConfigForm(initial))
            {
                if (args.Length >= 2 && args[0] == "--render")
                {
                    if (args.Length >= 3) form.SelectGroup(args[2]);
                    int width = 0, height = 0;
                    bool sized = args.Length >= 5 && int.TryParse(args[3], out width) &&
                        int.TryParse(args[4], out height);
                    form.Render(args[1], sized ? width : 0, sized ? height : 0);
                }
                else
                    Application.Run(form);
            }
        }
    }
}
