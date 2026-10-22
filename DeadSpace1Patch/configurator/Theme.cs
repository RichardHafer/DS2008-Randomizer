using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace DS2008RandomizerConfig
{
    internal static class Theme
    {
        internal static readonly Color Back = Hex(0x0f1012);
        internal static readonly Color Panel = Hex(0x16181b);
        internal static readonly Color Card = Hex(0x1b1e22);
        internal static readonly Color Border = Hex(0x2a2e34);
        internal static readonly Color Hover = Hex(0x1f2227);
        internal static readonly Color Selected = Hex(0x262a30);
        internal static readonly Color Input = Hex(0x111316);
        internal static readonly Color InputBorder = Hex(0x3a3f47);
        internal static readonly Color Text = Hex(0xd9dce0);
        internal static readonly Color Secondary = Hex(0xa9aeb6);
        internal static readonly Color Muted = Hex(0x7a8089);
        internal static readonly Color Accent = Hex(0xe83134);
        internal static readonly Color AccentHover = Hex(0xf34c4e);
        internal static readonly Color Changed = Hex(0x4cc3e6);
        internal static readonly Color Note = Hex(0x7fa8d4);

        internal static readonly Font Body = new Font("Segoe UI", 9.5f);
        internal static readonly Font Bold = new Font("Segoe UI Semibold", 9.5f);
        internal static readonly Font Small = new Font("Segoe UI", 8.2f);
        internal static readonly Font Heading = new Font("Segoe UI Semibold", 15.5f);
        internal static readonly Font Brand = new Font("Segoe UI Semibold", 20f, FontStyle.Bold);

        internal static Color Hex(int rgb)
        {
            return Color.FromArgb((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
        }

        internal static GraphicsPath Round(Rectangle r, int radius)
        {
            int d = radius * 2;
            var path = new GraphicsPath();
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    internal static class NativeTheme
    {
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr handle, int message, IntPtr wParam, string text);

        [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
        private static extern int SetWindowTheme(IntPtr hwnd, string app, string list);

        internal static void CueBanner(TextBox box, string text)
        {
            SendMessage(box.Handle, 0x1501, (IntPtr)1, text);
        }

        internal static void Scroll(Control control)
        {
            try { SetWindowTheme(control.Handle, "DarkMode_Explorer", null); }
            catch (Exception) { }
        }
    }
}
