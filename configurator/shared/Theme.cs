using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace DS2CoopConfig
{
    enum Tone { Neutral, Info, Note, Warning, Error, Pending, Good }

    // Colors, fonts and the hand-placed layout scale. S() turns 96 dpi pixels into device pixels.
    static class Look
    {
        public static readonly Color Back = Hex(0x0f1012);
        public static readonly Color Panel = Hex(0x16181b);
        public static readonly Color Card = Hex(0x1b1e22);
        public static readonly Color Border = Hex(0x2a2e34);
        public static readonly Color Hover = Hex(0x1f2227);
        public static readonly Color Selected = Hex(0x262a30);
        public static readonly Color InputBack = Hex(0x111316);
        public static readonly Color InputBorder = Hex(0x3a3f47);
        public static readonly Color Text = Hex(0xd9dce0);
        public static readonly Color Secondary = Hex(0xa9aeb6);
        public static readonly Color Muted = Hex(0x7a8089);
        public static readonly Color Accent = Hex(0xe83134);   // median of the logo wordmark
        public static readonly Color AccentHover = Hex(0xf34c4e);
        public static readonly Color AccentDown = Hex(0xc0262a);
        public static readonly Color Warning = Hex(0xe0a030);
        public static readonly Color Error = Hex(0xff5f57);
        public static readonly Color Note = Hex(0x7fa8d4);
        public static readonly Color Changed = Hex(0x4cc3e6);
        public static readonly Color Good = Hex(0x63c174);

        public static float Scale = 1f;
        static float fontScale = 1f;
        public static Font Base, Bold, Small, Heading, PageTitle, Title;
        public static Bitmap InfoIcon, NoteIcon, WarningIcon, ErrorIcon;
        public static ToolTip Tips;

        const TextFormatFlags Wrap = TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl;

        // scaleOverride > 0 renders at another scale than the screen (used by --render).
        public static void Init(float scaleOverride)
        {
            float dpi;
            using (var g = Graphics.FromHwnd(IntPtr.Zero)) dpi = g.DpiX;
            Scale = scaleOverride > 0 ? scaleOverride : dpi / 96f;
            fontScale = Scale * 96f / dpi;
            Base = MakeFont("Segoe UI", 9.75f);
            Bold = MakeFont("Segoe UI Semibold", 9.75f);
            Small = MakeFont("Segoe UI", 8.25f);
            Heading = MakeFont("Segoe UI Semibold", 11.25f);
            PageTitle = MakeFont("Segoe UI Semibold", 16f);
            Title = MakeFont("Segoe UI Semibold", 13f);
            InfoIcon = Glyph(Muted, false, 'i');
            NoteIcon = Glyph(Note, false, 'i');
            WarningIcon = Glyph(Warning, true, '!');
            ErrorIcon = Glyph(Error, false, '!');
            Tips = MakeTips();
            ToolStripManager.Renderer = new DarkMenuRenderer();
        }

        static Font MakeFont(string family, float points) { return new Font(family, points * fontScale); }

        public static int S(int px) { return (int)Math.Round(px * Scale); }

        public static Color Hex(int rgb) { return Color.FromArgb(255, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

        public static Color Blend(Color a, Color b, float t)
        {
            return Color.FromArgb(255, (int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
        }

        public static GraphicsPath Round(Rectangle r, int radius)
        {
            var path = new GraphicsPath();
            int d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
            if (d <= 1) { path.AddRectangle(r); return path; }
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        public static void FillRound(Graphics g, Color color, Rectangle r, int radius)
        {
            using (var path = Round(r, radius))
            using (var brush = new SolidBrush(color)) g.FillPath(brush, path);
        }

        public static void DrawRound(Graphics g, Color color, Rectangle r, int radius)
        {
            using (var path = Round(r, radius))
            using (var pen = new Pen(color)) g.DrawPath(pen, path);
        }

        public static Label MakeLabel(string text, Font font, Color color)
        {
            var l = new Label { AutoSize = false, Text = text ?? "", ForeColor = color, UseMnemonic = false };
            if (font != null) l.Font = font;
            return l;
        }

        // Places a wrapping label and returns the y below it.
        public static int Place(Label label, int x, int y, int width)
        {
            if (string.IsNullOrEmpty(label.Text)) { label.Visible = false; return y; }
            label.Visible = true;
            width = Math.Max(50, width);
            var size = TextRenderer.MeasureText(label.Text, label.Font, new Size(width, int.MaxValue), Wrap);
            label.Bounds = new Rectangle(x, y, width, size.Height + 2);
            return y + size.Height + 2;
        }

        public static int Measure(string text, Font font, int width)
        {
            return TextRenderer.MeasureText(text, font, new Size(Math.Max(50, width), int.MaxValue), Wrap).Height + 2;
        }

        public static int OneLineWidth(string text, Font font)
        {
            return TextRenderer.MeasureText(text, font).Width + 4;
        }

        static readonly Dictionary<Font, int> lineHeights = new Dictionary<Font, int>();

        public static int LineHeight(Font font)
        {
            int h;
            if (!lineHeights.TryGetValue(font, out h))
                lineHeights[font] = h = TextRenderer.MeasureText("Ag", font).Height + 2;
            return h;
        }

        // Round or triangular badge with an "i" or "!" cut out in dark.
        static Bitmap Glyph(Color fill, bool triangle, char mark)
        {
            int n = S(16);
            var bmp = new Bitmap(n, n, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(bmp))
            using (var brush = new SolidBrush(fill))
            using (var dark = new SolidBrush(Hex(0x15171a)))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                float w = n - 1f;
                if (triangle)
                    g.FillPolygon(brush, new[] { new PointF(w / 2, 0.5f), new PointF(w, w - 0.5f), new PointF(0, w - 0.5f) });
                else
                    g.FillEllipse(brush, 0, 0, w, w);
                float bar = Math.Max(1.6f, n * 0.14f), cx = w / 2 - bar / 2 + 0.5f;
                if (mark == 'i')
                {
                    g.FillEllipse(dark, cx, n * 0.20f, bar, bar);
                    g.FillRectangle(dark, cx, n * 0.40f, bar, n * 0.38f);
                }
                else
                {
                    float top = triangle ? n * 0.36f : n * 0.22f, len = triangle ? n * 0.30f : n * 0.36f;
                    g.FillRectangle(dark, cx, top, bar, len);
                    g.FillEllipse(dark, cx, top + len + bar * 0.6f, bar, bar);
                }
            }
            return bmp;
        }

        static ToolTip MakeTips()
        {
            var tips = new ToolTip { OwnerDraw = true, InitialDelay = 400, ReshowDelay = 100, AutoPopDelay = 15000 };
            tips.Popup += (s, e) =>
            {
                var size = TextRenderer.MeasureText(tips.GetToolTip(e.AssociatedControl), Small, new Size(S(420), int.MaxValue), Wrap);
                e.ToolTipSize = new Size(size.Width + S(16), size.Height + S(10));
            };
            tips.Draw += (s, e) =>
            {
                using (var back = new SolidBrush(Hex(0x24272c))) e.Graphics.FillRectangle(back, e.Bounds);
                using (var pen = new Pen(InputBorder)) e.Graphics.DrawRectangle(pen, 0, 0, e.Bounds.Width - 1, e.Bounds.Height - 1);
                var r = new Rectangle(S(8), S(5), e.Bounds.Width - S(16), e.Bounds.Height - S(10));
                TextRenderer.DrawText(e.Graphics, e.ToolTipText, Small, r, Text, Wrap);
            };
            return tips;
        }

        public static Button MakeButton(string text, bool primary, int height)
        {
            var b = new FlatButton(primary) { Text = text };
            b.Size = new Size(OneLineWidth(text, primary ? Bold : Base) + S(28), height);
            return b;
        }

        public static void DrawIcon(Graphics g, Tone tone, int x, int y)
        {
            var icon = tone == Tone.Warning ? WarningIcon : tone == Tone.Error ? ErrorIcon : tone == Tone.Note ? NoteIcon : InfoIcon;
            g.DrawImageUnscaled(icon, x, y);
        }
    }

    static class Native
    {
        [DllImport("dwmapi.dll")]
        static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

        [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
        static extern int SetWindowTheme(IntPtr hwnd, string app, string idList);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr wParam, string lParam);

        // Windows 10 before 20H1 used attribute 19.
        public static void DarkTitleBar(IntPtr hwnd)
        {
            try
            {
                int on = 1;
                if (DwmSetWindowAttribute(hwnd, 20, ref on, 4) != 0) DwmSetWindowAttribute(hwnd, 19, ref on, 4);
            }
            catch (Exception) { }
        }

        public static void DarkScrollBars(Control c)
        {
            try { SetWindowTheme(c.Handle, "DarkMode_Explorer", null); }
            catch (Exception) { }
        }

        public static void CueBanner(TextBox box, string text)
        {
            SendMessage(box.Handle, 0x1501, (IntPtr)1, text);
        }
    }

    // A double-buffered panel that paints its own card, input wells and message callouts,
    // so rows need far fewer child controls.
    class Surface : Panel
    {
        public bool Rounded;
        public bool Changed;
        public AnchorStyles Edges = AnchorStyles.None;
        public readonly List<MessageLine> Messages = new List<MessageLine>();
        public readonly List<Rectangle> Stripes = new List<Rectangle>();
        readonly List<Control> wells = new List<Control>();
        readonly List<Padding> wellPads = new List<Padding>();

        public Surface()
        {
            DoubleBuffered = true;
            SetStyle(ControlStyles.ResizeRedraw, true);
        }

        public static readonly int WellHeight = 30;
        public const float StripeAlpha = 0.025f;

        // An input with a dark field around it; the padding is the field around the control.
        public void AddWell(Control c, int left, int right)
        {
            wells.Add(c);
            wellPads.Add(new Padding(left, 0, right, 0));
            EventHandler focus = delegate { Invalidate(WellRect(wells.IndexOf(c))); };
            c.Enter += focus;
            c.Leave += focus;
        }

        Rectangle WellRect(int i)
        {
            var c = wells[i];
            var pad = wellPads[i];
            int h = Look.S(WellHeight);
            return new Rectangle(c.Left - pad.Left, c.Top + c.Height / 2 - h / 2, c.Width + pad.Left + pad.Right, h);
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            var g = e.Graphics;
            if (Rounded)
            {
                using (var back = new SolidBrush(Parent != null ? Parent.BackColor : Look.Back)) g.FillRectangle(back, ClientRectangle);
                g.SmoothingMode = SmoothingMode.AntiAlias;
                var r = new Rectangle(0, 0, Width - 1, Height - 1);
                int radius = Look.S(6);
                using (var path = Look.Round(r, radius))
                {
                    using (var fill = new SolidBrush(BackColor)) g.FillPath(fill, path);
                    if (Changed)
                    {
                        var clip = g.Clip;
                        g.SetClip(path);
                        using (var bar = new SolidBrush(Look.Changed)) g.FillRectangle(bar, 0, 0, Look.S(3), Height);
                        g.Clip = clip;
                    }
                    using (var pen = new Pen(Look.Border)) g.DrawPath(pen, path);
                }
            }
            else
            {
                using (var back = new SolidBrush(BackColor)) g.FillRectangle(back, ClientRectangle);
            }
            using (var pen = new Pen(Look.Border))
            {
                if ((Edges & AnchorStyles.Top) != 0) g.DrawLine(pen, 0, 0, Width, 0);
                if ((Edges & AnchorStyles.Bottom) != 0) g.DrawLine(pen, 0, Height - 1, Width, Height - 1);
                if ((Edges & AnchorStyles.Right) != 0) g.DrawLine(pen, Width - 1, 0, Width - 1, Height);
            }
            g.SmoothingMode = SmoothingMode.AntiAlias;
            if (Stripes.Count > 0)
                using (var stripe = new SolidBrush(Look.Blend(BackColor, Color.White, StripeAlpha)))
                    foreach (var s in Stripes) g.FillRectangle(stripe, s);
            for (int i = 0; i < wells.Count; i++)
            {
                if (!wells[i].Visible) continue;
                var r = WellRect(i);
                r.Width -= 1;
                r.Height -= 1;
                Look.FillRound(g, Look.InputBack, r, Look.S(3));
                Color border = wells[i].ContainsFocus ? Look.Accent : wells[i].Enabled ? Look.InputBorder : Look.Border;
                Look.DrawRound(g, border, r, Look.S(3));
            }
            foreach (var m in Messages)
            {
                if (m.Rect.IsEmpty) continue;
                Look.FillRound(g, m.Back, m.Rect, Look.S(4));
                var clip = g.Clip;
                using (var path = Look.Round(m.Rect, Look.S(4)))
                {
                    g.SetClip(path);
                    using (var bar = new SolidBrush(m.Color)) g.FillRectangle(bar, m.Rect.X, m.Rect.Y, Look.S(2), m.Rect.Height);
                }
                g.Clip = clip;
                Look.DrawIcon(g, m.Tone, m.Rect.X + Look.S(11), m.Rect.Y + Look.S(8));
            }
        }
    }

    // Icon plus wrapping text inside a tinted callout, hidden while empty.
    sealed class MessageLine
    {
        public readonly Label Label;
        public readonly Tone Tone;
        public readonly Color Color, Back;
        public Rectangle Rect;

        public MessageLine(Surface parent, Tone tone)
        {
            Tone = tone;
            Color text;
            switch (tone)
            {
                case Tone.Warning: Color = Look.Warning; text = Look.Warning; Back = Look.Blend(Look.Card, Look.Warning, 0.09f); break;
                case Tone.Error: Color = Look.Error; text = Look.Error; Back = Look.Blend(Look.Card, Look.Error, 0.11f); break;
                case Tone.Note: Color = Look.Note; text = Look.Blend(Look.Text, Look.Note, 0.35f); Back = Look.Blend(Look.Card, Look.Note, 0.08f); break;
                default: Color = Look.Muted; text = Look.Secondary; Back = Look.Blend(Look.Card, Color.White, 0.035f); break;
            }
            Label = Look.MakeLabel("", null, text);
            Label.BackColor = Back;
            Label.Visible = false;
            parent.Controls.Add(Label);
            parent.Messages.Add(this);
        }

        // True when the shown text changed.
        public bool Set(string text)
        {
            text = text ?? "";
            if (Label.Text == text) return false;
            Label.Text = text;
            return true;
        }

        public int Place(int x, int y, int width)
        {
            if (Label.Text.Length == 0)
            {
                Label.Visible = false;
                Rect = Rectangle.Empty;
                return y;
            }
            int bottom = Look.Place(Label, x + Look.S(36), y + Look.S(7), width - Look.S(36) - Look.S(12));
            Rect = new Rectangle(x, y, width, bottom - y + Look.S(6));
            return Rect.Bottom + Look.S(6);
        }
    }

    // A checkbox drawn as a sliding switch, with its text to the right.
    sealed class Switch : CheckBox
    {
        bool hover;

        public Switch()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            AutoSize = false;
            Font = Look.Base;
            Cursor = Cursors.Hand;
        }

        public static int TrackWidth { get { return Look.S(38); } }

        public void FitText(string longest)
        {
            int w = TrackWidth + 2;
            if (!string.IsNullOrEmpty(longest)) w += Look.S(10) + Look.OneLineWidth(longest, Font);
            Size = new Size(w, Look.S(24));
        }

        protected override void OnCheckedChanged(EventArgs e) { Invalidate(); base.OnCheckedChanged(e); }
        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            using (var back = new SolidBrush(BackColor)) g.FillRectangle(back, ClientRectangle);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            int tw = TrackWidth, th = Look.S(20);
            var track = new Rectangle(1, (Height - th) / 2, tw - 1, th - 1);
            Color fill = Checked ? (hover ? Look.AccentHover : Look.Accent) : (hover ? Look.Hex(0x3a3f47) : Look.Hex(0x2e3238));
            if (!Enabled) fill = Look.Blend(BackColor, fill, 0.45f);
            if (Focused && ShowFocusCues)
                Look.DrawRound(g, Look.Blend(BackColor, Look.Accent, 0.7f), Rectangle.Inflate(track, 2, 2), th / 2 + 2);
            Look.FillRound(g, fill, track, th / 2);
            if (!Checked) Look.DrawRound(g, Look.InputBorder, track, th / 2);
            int knob = th - Look.S(6);
            int kx = Checked ? track.Right - Look.S(3) - knob : track.X + Look.S(3) + 1;
            Color knobColor = Checked ? Color.White : Look.Hex(0xaab0b8);
            if (!Enabled) knobColor = Look.Blend(BackColor, knobColor, 0.5f);
            using (var brush = new SolidBrush(knobColor)) g.FillEllipse(brush, kx, track.Y + (track.Height - knob) / 2f + 0.5f, knob, knob);
            if (Text.Length > 0)
            {
                var r = new Rectangle(tw + Look.S(10), 0, Width - tw - Look.S(10), Height);
                TextRenderer.DrawText(g, Text, Font, r, Enabled ? ForeColor : Look.Muted, BackColor,
                    TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
            }
        }
    }

    // NumericUpDown without a border; the arrows are repainted to match the dark field.
    sealed class SpinBox : NumericUpDown
    {
        readonly Control arrows;
        int hot;    // 0 none, 1 up, 2 down
        bool pressed;

        public SpinBox()
        {
            BorderStyle = BorderStyle.None;
            Font = Look.Base;
            BackColor = Look.InputBack;
            ForeColor = Look.Text;
            foreach (Control c in Controls)
                if (!(c is TextBox)) arrows = c;
            if (arrows == null) return;
            arrows.Paint += PaintArrows;
            arrows.MouseMove += (s, e) => { int h = e.Y < arrows.Height / 2 ? 1 : 2; if (h != hot) { hot = h; arrows.Invalidate(); } };
            arrows.MouseLeave += delegate { hot = 0; pressed = false; arrows.Invalidate(); };
            arrows.MouseDown += delegate { pressed = true; arrows.Invalidate(); };
            arrows.MouseUp += delegate { pressed = false; arrows.Invalidate(); };
        }

        void PaintArrows(object sender, PaintEventArgs e)
        {
            var g = e.Graphics;
            var r = arrows.ClientRectangle;
            using (var back = new SolidBrush(Look.InputBack)) g.FillRectangle(back, r);
            int half = r.Height / 2;
            if (hot != 0 && Enabled)
            {
                var hr = hot == 1 ? new Rectangle(0, 0, r.Width, half) : new Rectangle(0, half, r.Width, r.Height - half);
                using (var b = new SolidBrush(pressed ? Look.Hex(0x30353c) : Look.Hex(0x23272c))) g.FillRectangle(b, hr);
            }
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float w = Math.Max(5f, r.Width * 0.42f), h = w / 2f, cx = r.Width / 2f;
            float upY = half / 2f + h / 2f, downY = half + (r.Height - half) / 2f - h / 2f;
            Color up = !Enabled ? Look.Border : hot == 1 ? Look.Text : Look.Muted;
            Color down = !Enabled ? Look.Border : hot == 2 ? Look.Text : Look.Muted;
            using (var b = new SolidBrush(up))
                g.FillPolygon(b, new[] { new PointF(cx - w / 2, upY), new PointF(cx + w / 2, upY), new PointF(cx, upY - h) });
            using (var b = new SolidBrush(down))
                g.FillPolygon(b, new[] { new PointF(cx - w / 2, downY), new PointF(cx + w / 2, downY), new PointF(cx, downY + h) });
        }
    }

    // Flat button; the primary one is filled with the accent.
    sealed class FlatButton : Button
    {
        readonly bool primary;

        public FlatButton(bool primary)
        {
            this.primary = primary;
            FlatStyle = FlatStyle.Flat;
            UseVisualStyleBackColor = false;
            Cursor = Cursors.Hand;
            FlatAppearance.BorderSize = 1;
            if (primary)
            {
                BackColor = Look.Accent;
                ForeColor = Color.White;
                Font = Look.Bold;
                FlatAppearance.BorderColor = Look.Accent;
                FlatAppearance.MouseOverBackColor = Look.AccentHover;
                FlatAppearance.MouseDownBackColor = Look.AccentDown;
            }
            else
            {
                BackColor = Look.Hex(0x22252a);
                ForeColor = Look.Text;
                FlatAppearance.BorderColor = Look.InputBorder;
                FlatAppearance.MouseOverBackColor = Look.Hex(0x2c3036);
                FlatAppearance.MouseDownBackColor = Look.Hex(0x363b42);
            }
        }

        protected override bool ShowFocusCues { get { return false; } }

        protected override void OnGotFocus(EventArgs e)
        {
            FlatAppearance.BorderColor = primary ? Color.White : Look.Accent;
            base.OnGotFocus(e);
        }

        protected override void OnLostFocus(EventArgs e)
        {
            FlatAppearance.BorderColor = primary ? Look.Accent : Look.InputBorder;
            base.OnLostFocus(e);
        }
    }

    // Section list on the left: accent bar on the selected entry, a dot for unsaved
    // changes and the match count while searching.
    sealed class NavList : Control
    {
        readonly string[] items;
        readonly int[] breaks;    // a divider follows these entries
        int selected, hover = -1, itemHeight = Look.S(38);
        public bool[] Changed;
        public bool[] Disabled;   // greyed out, cannot be selected
        public int[] Matches;     // null while not searching
        public event Action SelectedChanged;

        public NavList(string[] items, int[] breaks)
        {
            this.items = items;
            this.breaks = breaks ?? new int[0];
            Changed = new bool[items.Length];
            Disabled = new bool[items.Length];
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
            TabStop = true;
            BackColor = Look.Panel;
            Cursor = Cursors.Hand;
        }

        public int SelectedIndex
        {
            get { return selected; }
            set
            {
                if (value < 0 || value >= items.Length || value == selected || Disabled[value]) return;
                selected = value;
                Invalidate();
                if (SelectedChanged != null) SelectedChanged();
            }
        }

        public int ItemHeight
        {
            get { return itemHeight; }
            set { if (value != itemHeight) { itemHeight = value; Invalidate(); } }
        }

        public int Count { get { return items.Length; } }
        static int BreakHeight { get { return Look.S(13); } }

        // Height of the whole list at the given entry height.
        public int HeightFor(int entry) { return Look.S(4) + items.Length * entry + breaks.Length * BreakHeight; }

        int BreaksBefore(int i)
        {
            int n = 0;
            foreach (int b in breaks) if (b < i) n++;
            return n;
        }

        Rectangle ItemRect(int i)
        {
            return new Rectangle(Look.S(10), Look.S(2) + i * ItemHeight + BreaksBefore(i) * BreakHeight, Width - Look.S(20), ItemHeight - Look.S(4));
        }

        int HitTest(Point p)
        {
            for (int i = 0; i < items.Length; i++) if (ItemRect(i).Contains(p)) return Disabled[i] ? -1 : i;
            return -1;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(BackColor);
            using (var pen = new Pen(Look.Border))
                foreach (int b in breaks)
                {
                    if (b < 0 || b >= items.Length - 1) continue;
                    int y = ItemRect(b).Bottom + Look.S(2) + BreakHeight / 2;
                    g.DrawLine(pen, Look.S(22), y, Width - Look.S(22), y);
                }
            g.SmoothingMode = SmoothingMode.AntiAlias;
            for (int i = 0; i < items.Length; i++)
            {
                var r = ItemRect(i);
                bool sel = i == selected;
                if (sel || i == hover) Look.FillRound(g, sel ? Look.Selected : Look.Hover, r, Look.S(4));
                if (sel)
                {
                    Look.FillRound(g, Look.Accent, new Rectangle(r.X, r.Y + Look.S(7), Look.S(3), r.Height - Look.S(14)), Look.S(1));
                    if (Focused && ShowFocusCues) Look.DrawRound(g, Look.Blend(Look.Selected, Look.Accent, 0.6f), r, Look.S(4));
                }
                bool none = Matches != null && Matches[i] == 0;
                Color color = Disabled[i] ? Look.Blend(Look.Panel, Look.Muted, 0.45f) : sel ? Look.Text :
                    none ? Look.Blend(Look.Panel, Look.Muted, 0.6f) : Look.Secondary;
                int right = r.Right - Look.S(10);
                if (Matches != null)
                {
                    string count = Matches[i].ToString();
                    int cw = Look.OneLineWidth(count, Look.Small);
                    TextRenderer.DrawText(g, count, Look.Small, new Rectangle(right - cw, r.Y, cw, r.Height),
                        none ? color : Look.Text, TextFormatFlags.VerticalCenter | TextFormatFlags.Right | TextFormatFlags.SingleLine);
                    right -= cw + Look.S(4);
                }
                if (Changed[i])
                {
                    int d = Look.S(7);
                    using (var dot = new SolidBrush(Look.Changed)) g.FillEllipse(dot, right - d, r.Y + (r.Height - d) / 2f, d, d);
                    right -= d + Look.S(8);
                }
                var text = new Rectangle(r.X + Look.S(16), r.Y, right - r.X - Look.S(16), r.Height);
                TextRenderer.DrawText(g, items[i], sel ? Look.Bold : Look.Base, text, color,
                    TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
            }
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            int h = HitTest(e.Location);
            if (h != hover) { hover = h; Invalidate(); }
            base.OnMouseMove(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            hover = -1;
            Invalidate();
            base.OnMouseLeave(e);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            Focus();
            int h = HitTest(e.Location);
            if (h >= 0) SelectedIndex = h;
            base.OnMouseDown(e);
        }

        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Up || keyData == Keys.Down || base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Up) SelectedIndex = Step(-1);
            else if (e.KeyCode == Keys.Down) SelectedIndex = Step(1);
            else if (e.KeyCode == Keys.Home) SelectedIndex = 0;
            else if (e.KeyCode == Keys.End) SelectedIndex = items.Length - 1;
            base.OnKeyDown(e);
        }

        // The next entry in that direction that can be selected.
        int Step(int direction)
        {
            for (int i = selected + direction; i >= 0 && i < items.Length; i += direction)
                if (!Disabled[i]) return i;
            return selected;
        }

        protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
        protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }
    }

    // Header with the logo, the product line and the INI path.
    sealed class Banner : Control
    {
        readonly Bitmap logo;
        readonly string iniPath;

        public Banner(Image source, string path)
        {
            iniPath = path;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            Height = Look.S(104);
            if (source != null)
            {
                int h = Look.S(84);
                int w = (int)Math.Round(source.Width * (double)h / source.Height);
                logo = new Bitmap(w, h, PixelFormat.Format32bppPArgb);
                using (var g = Graphics.FromImage(logo))
                using (var attributes = new ImageAttributes())
                {
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    g.CompositingQuality = CompositingQuality.HighQuality;
                    attributes.SetWrapMode(WrapMode.TileFlipXY);
                    g.DrawImage(source, new Rectangle(0, 0, w, h), 0, 0, source.Width, source.Height, GraphicsUnit.Pixel, attributes);
                }
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            using (var back = new LinearGradientBrush(ClientRectangle, Look.Hex(0x191b1f), Look.Hex(0x111215), LinearGradientMode.Vertical))
                g.FillRectangle(back, ClientRectangle);
            int x = Look.S(20);
            if (logo != null)
            {
                int y = (Height - logo.Height) / 2;
                // faint red glow behind the wordmark
                var glow = new Rectangle(x - Look.S(30), y - Look.S(6), logo.Width + Look.S(60), logo.Height + Look.S(12));
                using (var path = new GraphicsPath())
                {
                    path.AddEllipse(glow);
                    using (var brush = new PathGradientBrush(path))
                    {
                        brush.CenterColor = Color.FromArgb(46, Look.Accent);
                        brush.SurroundColors = new[] { Color.FromArgb(0, Look.Accent) };
                        g.FillPath(brush, path);
                    }
                }
                g.DrawImageUnscaled(logo, x, y);
                x += logo.Width + Look.S(22);
            }
            using (var pen = new Pen(Look.Border))
            {
                g.DrawLine(pen, x, Look.S(26), x, Height - Look.S(26));
                g.DrawLine(pen, 0, Height - 1, Width, Height - 1);
            }
            x += Look.S(22);
            int width = Width - x - Look.S(20);
            if (width < Look.S(80)) return;
            int h1 = Look.LineHeight(Look.Small), h2 = Look.LineHeight(Look.Title), h3 = Look.LineHeight(Look.Small);
            int top = (Height - h1 - h2 - h3 - Look.S(4)) / 2;
            var flags = TextFormatFlags.Left | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix;
            TextRenderer.DrawText(g, "DEAD SPACE 2", Look.Small, new Rectangle(x, top, width, h1), Look.Accent, flags | TextFormatFlags.EndEllipsis);
            TextRenderer.DrawText(g, "Co-op Mod settings", Look.Title, new Rectangle(x, top + h1, width, h2), Look.Text, flags | TextFormatFlags.EndEllipsis);
            TextRenderer.DrawText(g, iniPath, Look.Small, new Rectangle(x, top + h1 + h2 + Look.S(4), width, h3), Look.Muted, flags | TextFormatFlags.PathEllipsis);
        }
    }

    // Status text with a colored dot, vertically centered and wrapped to two lines at most.
    sealed class StatusLine : Control
    {
        Tone tone;

        public StatusLine()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        }

        public void Set(string text, Tone t)
        {
            if (Text == text && tone == t) return;
            tone = t;
            Text = text;
            Look.Tips.SetToolTip(this, text);
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(BackColor);
            if (Text.Length == 0) return;
            Color dot = tone == Tone.Pending ? Look.Changed : tone == Tone.Error ? Look.Error : tone == Tone.Good ? Look.Good : tone == Tone.Warning ? Look.Warning : Look.Muted;
            Color text = tone == Tone.Neutral ? Look.Secondary : tone == Tone.Error ? Look.Error : Look.Text;
            int d = Look.S(8), tx = d + Look.S(10);
            var flags = TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix;
            int w = Width - tx;
            int h = Math.Min(Look.LineHeight(Font) * 2, TextRenderer.MeasureText(Text, Font, new Size(w, int.MaxValue), flags).Height);
            int y = (Height - h) / 2;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (var brush = new SolidBrush(dot)) g.FillEllipse(brush, 0, y + (Look.LineHeight(Font) - d) / 2f - 1, d, d);
            TextRenderer.DrawText(g, Text, Font, new Rectangle(tx, y, w, h), text, flags);
        }
    }

    sealed class DarkMenuRenderer : ToolStripProfessionalRenderer
    {
        public DarkMenuRenderer() : base(new DarkColors()) { RoundedEdges = false; }

        protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
        {
            e.TextColor = e.Item.Enabled ? Look.Text : Look.Muted;
            base.OnRenderItemText(e);
        }

        sealed class DarkColors : ProfessionalColorTable
        {
            public override Color ToolStripDropDownBackground { get { return Look.Hex(0x202328); } }
            public override Color ImageMarginGradientBegin { get { return Look.Hex(0x202328); } }
            public override Color ImageMarginGradientMiddle { get { return Look.Hex(0x202328); } }
            public override Color ImageMarginGradientEnd { get { return Look.Hex(0x202328); } }
            public override Color MenuBorder { get { return Look.InputBorder; } }
            public override Color MenuItemBorder { get { return Look.Hex(0x2f343a); } }
            public override Color MenuItemSelected { get { return Look.Hex(0x2f343a); } }
            public override Color MenuItemSelectedGradientBegin { get { return Look.Hex(0x2f343a); } }
            public override Color MenuItemSelectedGradientEnd { get { return Look.Hex(0x2f343a); } }
            public override Color SeparatorDark { get { return Look.Border; } }
            public override Color SeparatorLight { get { return Look.Border; } }
        }
    }
}
