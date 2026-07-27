using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using System.Reflection;

namespace WowOptimizeLauncher {

    public class SettingItem {
        public string Section;
        public string Key;
        public bool DefaultVal;
        public CheckBox Ctrl;
        public string Tooltip;

        public SettingItem(string section, string key, bool defaultVal, CheckBox ctrl, string tooltip) {
            Section = section;
            Key = key;
            DefaultVal = defaultVal;
            Ctrl = ctrl;
            Tooltip = tooltip;
        }
    }

    // ───────────────────────────────────────────────────────────────
    //  Owner-drawn dark-themed CheckBox
    // ───────────────────────────────────────────────────────────────
    public class DarkCheckBox : CheckBox {
        private static readonly Color CyanAccent = Color.FromArgb(0, 229, 255);
        private static readonly Color BorderIdle = Color.FromArgb(100, 110, 140);
        private static readonly Color BoxBg = Color.FromArgb(18, 18, 28);

        public DarkCheckBox() {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer, true);
            ForeColor = Color.White;
            Font = new Font("Segoe UI", 9.75f, FontStyle.Regular);
            Cursor = Cursors.Hand;
            Margin = new Padding(5, 5, 5, 12);
            AutoSize = true;
        }

        protected override void OnPaint(PaintEventArgs e) {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.HighQuality;
            g.Clear(Parent != null ? Parent.BackColor : Color.FromArgb(15, 15, 22));

            // Checkbox square (16x16)
            int boxSize = 16;
            int boxY = (Height - boxSize) / 2;
            Rectangle boxRect = new Rectangle(0, boxY, boxSize, boxSize);

            using (SolidBrush bgBrush = new SolidBrush(BoxBg)) {
                g.FillRectangle(bgBrush, boxRect);
            }

            Color borderColor = (Checked || ClientRectangle.Contains(PointToClient(MousePosition))) ? CyanAccent : BorderIdle;
            using (Pen borderPen = new Pen(borderColor, 1.5f)) {
                g.DrawRectangle(borderPen, boxRect);
            }

            // Inner cyan square when checked (8x8 centered)
            if (Checked) {
                int innerSize = 8;
                int innerX = (boxSize - innerSize) / 2;
                int innerY = boxY + (boxSize - innerSize) / 2;
                using (SolidBrush cyanBrush = new SolidBrush(CyanAccent)) {
                    g.FillRectangle(cyanBrush, innerX, innerY, innerSize, innerSize);
                }
            }

            // Text
            using (SolidBrush textBrush = new SolidBrush(ForeColor)) {
                g.DrawString(Text, Font, textBrush, boxSize + 8, (Height - Font.Height) / 2f);
            }
        }

        protected override void OnMouseEnter(EventArgs e) {
            base.OnMouseEnter(e);
            Invalidate();
        }

        protected override void OnMouseLeave(EventArgs e) {
            base.OnMouseLeave(e);
            Invalidate();
        }

        public override Size GetPreferredSize(Size proposedSize) {
            using (Graphics g = CreateGraphics()) {
                SizeF textSize = g.MeasureString(Text, Font);
                return new Size(16 + 8 + (int)Math.Ceiling(textSize.Width) + 4, Math.Max(20, (int)Math.Ceiling(textSize.Height) + 4));
            }
        }
    }

    // ───────────────────────────────────────────────────────────────
    //  Owner-drawn dark-themed Button with hover effect
    // ───────────────────────────────────────────────────────────────
    public class DarkButton : Button {
        private bool _hovering;
        private Color _accentColor;
        private bool _highlight;

        public DarkButton(Color accentColor, bool highlight) {
            _accentColor = accentColor;
            _highlight = highlight;

            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer, true);
            FlatStyle = FlatStyle.Flat;
            FlatAppearance.BorderSize = 0;
            Cursor = Cursors.Hand;
            Font = new Font("Segoe UI", 8.5f, FontStyle.Bold);
            Height = 30;
            Margin = new Padding(0, 0, 0, 6);
        }

        protected override void OnPaint(PaintEventArgs e) {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.HighQuality;

            Color bgColor;
            Color fgColor;

            if (_hovering) {
                bgColor = _accentColor;
                fgColor = Color.Black;
            } else if (_highlight) {
                bgColor = _accentColor;
                fgColor = Color.Black;
            } else {
                bgColor = Color.FromArgb(20, 20, 28);
                fgColor = _accentColor;
            }

            using (SolidBrush bgBrush = new SolidBrush(bgColor)) {
                g.FillRectangle(bgBrush, ClientRectangle);
            }

            using (Pen borderPen = new Pen(_accentColor, 1.5f)) {
                g.DrawRectangle(borderPen, 0, 0, Width - 1, Height - 1);
            }

            TextFormatFlags flags = TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter;
            TextRenderer.DrawText(g, Text, Font, ClientRectangle, fgColor, flags);
        }

        protected override void OnMouseEnter(EventArgs e) {
            _hovering = true;
            Invalidate();
            base.OnMouseEnter(e);
        }

        protected override void OnMouseLeave(EventArgs e) {
            _hovering = false;
            Invalidate();
            base.OnMouseLeave(e);
        }
    }

    // ───────────────────────────────────────────────────────────────
    //  Owner-drawn dark-themed TabControl
    // ───────────────────────────────────────────────────────────────
    public class DarkTabControl : TabControl {
        private static readonly Color BgColor = Color.FromArgb(15, 15, 22);
        private static readonly Color CyanAccent = Color.FromArgb(0, 229, 255);
        private static readonly Color TabIdle = Color.FromArgb(90, 90, 110);

        public DarkTabControl() {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer, true);
            DrawMode = TabDrawMode.OwnerDrawFixed;
            SizeMode = TabSizeMode.Fixed;
            ItemSize = new Size(110, 28);
            Padding = new Point(0, 0);
        }

        protected override void OnPaint(PaintEventArgs e) {
            Graphics g = e.Graphics;
            g.Clear(BgColor);

            // Draw tab headers
            for (int i = 0; i < TabCount; i++) {
                Rectangle tabRect = GetTabRect(i);
                bool selected = (SelectedIndex == i);

                Color textColor = selected ? CyanAccent : TabIdle;
                using (Font tabFont = new Font("Segoe UI", 8f, FontStyle.Bold)) {
                    TextFormatFlags flags = TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter;
                    TextRenderer.DrawText(g, TabPages[i].Text, tabFont, tabRect, textColor, flags);
                }

                if (selected) {
                    using (Pen underline = new Pen(CyanAccent, 2f)) {
                        g.DrawLine(underline, tabRect.Left + 4, tabRect.Bottom - 1, tabRect.Right - 4, tabRect.Bottom - 1);
                    }
                }
            }

            // Draw tab page area border
            if (TabCount > 0) {
                Rectangle pageArea = new Rectangle(0, ItemSize.Height, Width - 1, Height - ItemSize.Height - 1);
                using (Pen borderPen = new Pen(Color.FromArgb(30, 30, 45), 1f)) {
                    g.DrawRectangle(borderPen, pageArea);
                }
            }
        }

        protected override void OnDrawItem(DrawItemEventArgs e) {
            // Handled in OnPaint
        }
    }

    // ───────────────────────────────────────────────────────────────
    //  Double-buffered Panel for flicker-free drawing
    // ───────────────────────────────────────────────────────────────
    public class DoubleBufferedPanel : Panel {
        public DoubleBufferedPanel() {
            DoubleBuffered = true;
            SetStyle(ControlStyles.ResizeRedraw, true);
        }
    }

    // ───────────────────────────────────────────────────────────────
    //  Double-buffered FlowLayoutPanel for scroll content
    // ───────────────────────────────────────────────────────────────
    public class DoubleBufferedFlowPanel : FlowLayoutPanel {
        public DoubleBufferedFlowPanel() {
            DoubleBuffered = true;
            SetStyle(ControlStyles.ResizeRedraw, true);
        }
    }

    // ───────────────────────────────────────────────────────────────
    //  Main Form
    // ───────────────────────────────────────────────────────────────
    public class MainForm : Form {
        // Single source of truth for this build's version. Compared against the
        // remote version.txt to decide whether to show the update notification,
        // and shown in the version label. Keep in sync with version.txt and
        // src/core/version.h on every release.
        private const string APP_VERSION = "3.17.0";

        private string iniPath;
        private Dictionary<string, SettingItem> settingsMap;

        // UI references
        private Label versionLabel;
        private Label activeCountLabel;
        private DoubleBufferedPanel progressBarPanel;
        private DarkTabControl tabs;
        private ToolTip toolTip;

        private DarkButton btnEnableGeneral;
        private DarkButton btnEnableUiLua;
        private DarkButton btnEnableCombatNet;
        private DarkButton btnEnableGfx;

        private FlowLayoutPanel generalFlow;
        private FlowLayoutPanel uiLuaFlow;
        private FlowLayoutPanel combatNetFlow;
        private FlowLayoutPanel graphicsSoundFlow;
        private TextBox searchBox;

        // Background image
        private Image backgroundImage;

        // Drag support
        private bool dragging;
        private Point dragStart;

        // Colors
        private static readonly Color DarkBg = Color.FromArgb(15, 15, 22);
        private static readonly Color DarkerBg = Color.FromArgb(12, 12, 18);
        private static readonly Color CyanAccent = Color.FromArgb(0, 229, 255);
        private static readonly Color PanelBg = Color.FromArgb(18, 18, 28);
        private static readonly Color SeparatorColor = Color.FromArgb(30, 30, 45);
        private static readonly Color SubtextColor = Color.FromArgb(150, 150, 180);
        private static readonly Color SubHeaderColor = Color.FromArgb(150, 150, 180);

        public MainForm() {
            // Setup Paths
            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            iniPath = Path.Combine(exeDir, "wow_opt.ini");

            // Tooltip component
            toolTip = new ToolTip();
            toolTip.OwnerDraw = true;
            toolTip.InitialDelay = 250;
            toolTip.AutoPopDelay = 30000;
            toolTip.ReshowDelay = 100;
            toolTip.Draw += ToolTip_Draw;
            toolTip.Popup += ToolTip_Popup;

            // Define settings mapping
            settingsMap = new Dictionary<string, SettingItem>() {
                // General
                { "Precise Sleep Frame Pacing", new SettingItem("General", "SleepPrecision", true, null, "Enforces millisecond-accurate frame-rate sleep pacing to reduce input lag and stabilize frame delivery.") },
                { "Memory Pressure Governor", new SettingItem("General", "MemoryPressure", true, null, "Sheds caches and adjusts texture footprint dynamically under critical 32-bit virtual address (VA) space limits.") },
                { "Heap Compactor", new SettingItem("General", "HeapCompactor", true, null, "Defragments the client heap every 5 seconds to prevent Out-Of-Memory (OOM) crashes during teleports.") },
                { "Lock-Free Heap Defragmenter", new SettingItem("General", "DefragLf", false, null, "Experimental defragmentation on the main thread using lock-free structures. Bypasses standard heap serialization.") },
                { "D3D9Ex Vulkan DXVK Support", new SettingItem("General", "VulkanDXVK", false, null, "Optimizes DLL hook integration to work cleanly with DXVK (requires placing a d3d9.dll Vulkan wrapper in the game folder).") },
                { "High-Precision Timing Fix", new SettingItem("General", "TimingFix", false, null, "Overrides GetTickCount and timeGetTime to use QPC, preventing micro-stutters and timer drift.") },
                { "Null Pointer CVar Safeguard", new SettingItem("General", "CvarNullGuard", true, null, "Critical safety hooks to prevent client crashes caused by uninitialized global variables and CVars.") },
                { "Frame Rate Limiter Override", new SettingItem("General", "FrameLimiter", false, null, "Overrides WoW's built-in frame limiter with a high-precision spin-wait sleep loop.") },
                { "Memory-Mapped MPQ VFS", new SettingItem("General", "MpqMmapVfs", false, null, "Maps all main MPQ files to memory using map views to speed up asset load times and parallelize decompression.") },
                { "Predictive MPQ Prefetcher", new SettingItem("General", "MpqPrefetch", false, null, "Tracks zone transitions and speculatively pre-caches MPQ asset files in background threads before you arrive.") },
                { "Memory-Mapped DBC RAM Cache", new SettingItem("General", "DbcPreload", false, null, "Pre-loads and decompresses all major client database files (.dbc) into RAM at startup for near-instant loading screens.") },
                { "32-bit OOM VRAM Governor", new SettingItem("General", "OomGovernor", false, null, "Dynamically downscales texture mipmaps when the 32-bit client's virtual address space usage approaches critical OOM levels.") },
                { "Hardware Cursor Fix", new SettingItem("General", "HardwareCursor", false, null, "Resets cursor visibility and releases any cursor clip region on startup (no engine byte patches, no hooks). Helps if the cursor is hidden or trapped after alt-tab.") },
                { "Mouse Clip Release on Alt-Tab", new SettingItem("General", "MouseClipRelease", false, null, "Frees the mouse cursor whenever WoW loses window focus, so it is never trapped inside the game window after alt-tab. Polls focus each frame and only ever RELEASES the clip (never applies one), so it cannot cause cursor/camera issues.") },
                { "SavedVariables Backup on Startup", new SettingItem("General", "SavedVarsBackup", false, null, "At startup, copies each WTF\\Account SavedVariables .lua to a .lua.bak so you have the last-good config if a session corrupts it. Runs once on a background thread; only ever copies existing files, never modifies your live SavedVariables.") },
                { "Sampling Profiler (diagnostic)", new SettingItem("General", "SamplingProfiler", false, null, "Developer tool: a background thread samples the main-thread instruction pointer ~1000x/sec and logs the top 50 hot functions on exit. Read-only, no gameplay effect. Leave off for normal play.") },
                { "Compatibility Mode (fixes VM/connection issues)", new SettingItem("General", "CompatMode", false, null, "Turn ON if the game will not connect with the DLL loaded, especially inside a VM or with HyperV/virtual switches enabled. Skips the aggressive CPU-priority, affinity and working-set tweaks that can starve the network in virtualized environments. Small performance cost; leave OFF on normal setups.") },

                // UI & Lua
                { "Fast UI Frame Accessors", new SettingItem("UI_Lua", "UIFrameAccessorFast", false, null, "Bypasses standard Lua stack queries to retrieve UI frame parameters (IsShown, GetAlpha) instantly.") },
                { "Fast FontString Metrics & Glyph Cache", new SettingItem("UI_Lua", "FontMetricsFast", false, null, "Provides high-speed text measurements and caches rasterized font glyph textures to eliminate render-time layout freezes.") },
                { "FrameScript FNV-1a Dispatcher", new SettingItem("UI_Lua", "FrameScriptDispatch", false, null, "Uses an O(1) hash map lookup for script handlers instead of linear string matching.") },
                { "Lua Number Conversion Fast Path", new SettingItem("UI_Lua", "LuaNumConvFast", false, null, "Inlines common Lua stack value queries (tonumber, gettop, settop) to bypass stack checking overhead.") },
                { "Lua GetTime Frame Cache", new SettingItem("UI_Lua", "LuaGetTimeFast", false, null, "Caches the GetTime() Lua API value within a single frame tick to avoid redundant OS-level high-precision timer calls.") },
                { "Lua C-API Inline Cache Suite", new SettingItem("UI_Lua", "LuaOpcache", false, null, "Master switch for the Lua C-API fast paths: luaH_getstr and rawgeti inline caches, table/string/buffer fast paths, VM table indexing and the compiled regex cache.") },
                { "Adaptive Lua GC Governor", new SettingItem("UI_Lua", "LuaGcCoalesce", false, null, "Paces incremental garbage collection per frame from the live game state - relaxed while a loading screen is up, stopped in combat below 256MB, aggressive while idle.") },
                { "Module Handle Cache", new SettingItem("UI_Lua", "ModuleHandleCache", false, null, "Caches GetModuleHandle results, which the client queries repeatedly for already-loaded modules.") },
                
                // Combat & Net
                { "Combat Log Leak Fix (retention 1800s)", new SettingItem("Combat_Net", "CombatLogLeakFix", true, null, "Fixes the 16-year-old WoW combat log memory leak by extending event retention from 300s to 1800s (writes the retention CVar). Proven and stable - on by default.") },
                { "Combat Log Aggregator", new SettingItem("Combat_Net", "CombatLogParser", false, null, "C++ combat log aggregator + buffer governor that intercepts and summarizes events instead of the slow Lua path. More aggressive than the leak fix above; opt-in.") },
                { "Incremental Combat Log parsing", new SettingItem("Combat_Net", "CombatLogIncremental", false, null, "Splits large combat updates into small steps, preventing massive spikes in large-scale combat.") },
                { "SSE2 Network GUID Unpacking", new SettingItem("Combat_Net", "NetworkGuidSse2", false, null, "Vectorizes the unpacking of network entity GUIDs inside network data streams.") },

                // Graphics & Sound
                { "SSE2 Boyer-Moore strstr", new SettingItem("Graphics_Sound", "StrStrSse2", false, null, "Optimizes string sub-searches (such as font names, textures) using vectorized SIMD algorithms.") },
                { "Vectorized String Concatenation", new SettingItem("Graphics_Sound", "StrCatFast", false, null, "Speeds up string appending (such as chat text building) using SSE2 assembly wrappers.") },
                { "FMOD Sound Mixer Optimization", new SettingItem("Graphics_Sound", "SoundMixerOpt", false, null, "Adjusts audio thread schedules and buffer allocations to prevent sound stutters in raids.") },
                { "Parallel Sound Wave Decoding", new SettingItem("Graphics_Sound", "AudioDecodeMt", false, null, "Decodes sound assets in background threads to eliminate latency when playing fresh audio clips.") },
                { "DBC Data Lookup Cache", new SettingItem("Graphics_Sound", "DbcLookupCache", false, null, "Speeds up data reading from internal database files (.dbc) for models, items, and spells.") },
                { "Asynchronous Texture Loader", new SettingItem("Graphics_Sound", "AsyncTexLoader", false, null, "Asynchronously loads and decompresses BLP textures in background worker threads, hot-swapping them on frame boundaries to prevent stutters.") },
                { "Texture Smart Unload Delay", new SettingItem("Graphics_Sound", "TextureUnloadDelay", false, null, "Delays texture unloading during camera turnarounds to prevent immediate load micro-stutters.") },
                { "Mipmap Bias Governor", new SettingItem("Graphics_Sound", "MipBiasGovernor", false, null, "Adjusts mipmap texture bias dynamically based on virtual memory pressure to prevent allocation spikes.") },
                { "SIMD Matrix Vector Transforms", new SettingItem("Graphics_Sound", "SimdMatrixTransform", false, null, "Vectorizes 3D coordinate and matrix-vector calculations using SSE2 SIMD instructions to accelerate particle updates.") },
                { "Dynamic Shadow Quality Auto-Scaler", new SettingItem("Graphics_Sound", "DynamicShadowScaler", false, null, "Dynamically scales shadow quality and resolution depending on the active frame rate.") },
                { "Advanced Sound Channels Coalescer", new SettingItem("Graphics_Sound", "SoundCoalescer", false, null, "Coalesces rapid duplicated sound plays to prevent channel exhaustion under AOE spam.") },
                { "Particle Density Dynamic Scaler", new SettingItem("Graphics_Sound", "ParticleDensityScaler", false, null, "Dynamically scales down particle density in heavy raid environments to keep FPS high.") },
                { "Overlapping Sound Volume Limiter", new SettingItem("Graphics_Sound", "SoundVolumeLimit", false, null, "Limits and clamps volume for overlapping duplicate sound effects to prevent clipping and audio driver lag.") },
                { "Terrain Height Cache", new SettingItem("Graphics_Sound", "TerrainHeightCache", false, null, "Caches terrain elevation queries within the frame to minimize CPU map collisions query time.") },
                { "Spell Visual Effects Culler", new SettingItem("Graphics_Sound", "SpellEffectCulling", false, null, "Dynamically scales down particle density and minor spell impact effects in large raids.") },
                { "Lua String Interning Fast Path", new SettingItem("UI_Lua", "LuaSNewLstrFast", false, null, "Intercepts luaS_newlstr (0x00856C80), which every Lua string in the game passes through, and looks the string up in the VM's string table itself. Experimental: on a miss the engine repeats the same work, and it is under investigation as a possible cause of corrupted addon names. Leave off unless testing.") },
                { "Fast SSE2 Memory Clear (FastMemset)", new SettingItem("Graphics_Sound", "FastMemsetOpt", true, null, "SSE2 non-temporal memset replacement for large memory clears at 0x0040BB80.") },
                { "Fast Case-Insensitive String Compare", new SettingItem("UI_Lua", "FastStrnicmpOpt", true, null, "SSE2 ASCII case-insensitive string comparison replacement at 0x0076E780.") },
                { "Minimap Blip Refresh Governor", new SettingItem("UI_Lua", "MinimapRefreshGovernor", true, null, "Throttles minimap blip refresh calls at 0x00923D50 to eliminate minimap CPU overhead.") },
                { "UI Layout Recalculation Throttle", new SettingItem("UI_Lua", "UILayoutThrottle", true, null, "Governs UI layout recalculations at 0x00865270 during heavy addon updates.") },
            };

            // Window Setup
            Text = "WoW-Optimize Launcher";
            ClientSize = new Size(920, 650);
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.None;
            BackColor = DarkBg;
            ForeColor = Color.White;
            Font = new Font("Segoe UI", 9f);
            MaximizeBox = false;
            DoubleBuffered = true;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);

            // Load background image
            LoadBackgroundImage();

            // Build GUI
            InitializeLayout();

            // Load Settings from INI
            LoadSettings();

            // Check for Updates
            CheckForUpdatesAsync();
        }

        private void LoadBackgroundImage() {
            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            string bgImagePath = Path.Combine(exeDir, "wotlk_background.jpg");

            if (File.Exists(bgImagePath)) {
                try {
                    backgroundImage = Image.FromFile(bgImagePath);
                    return;
                } catch {
                    // fall through to resource
                }
            }

            try {
                Assembly asm = Assembly.GetExecutingAssembly();
                Stream stream = asm.GetManifestResourceStream("wotlk_background.jpg");
                if (stream != null) {
                    backgroundImage = Image.FromStream(stream);
                }
            } catch {
                // fallback to solid color — backgroundImage stays null
            }
        }

        protected override void OnPaintBackground(PaintEventArgs e) {
            Graphics g = e.Graphics;

            if (backgroundImage != null) {
                // Draw background image scaled to fill (UniformToFill equivalent)
                float scaleX = (float)ClientSize.Width / backgroundImage.Width;
                float scaleY = (float)ClientSize.Height / backgroundImage.Height;
                float scale = Math.Max(scaleX, scaleY);
                int drawW = (int)(backgroundImage.Width * scale);
                int drawH = (int)(backgroundImage.Height * scale);
                int drawX = (ClientSize.Width - drawW) / 2;
                int drawY = (ClientSize.Height - drawH) / 2;
                g.DrawImage(backgroundImage, drawX, drawY, drawW, drawH);

                // Dark overlay (alpha ~235/255 of RGB 12,12,18)
                using (SolidBrush overlay = new SolidBrush(Color.FromArgb(235, 12, 12, 18))) {
                    g.FillRectangle(overlay, ClientRectangle);
                }
            } else {
                using (SolidBrush bgBrush = new SolidBrush(DarkerBg)) {
                    g.FillRectangle(bgBrush, ClientRectangle);
                }
            }

            // Outer border
            using (Pen borderPen = new Pen(SeparatorColor, 1f)) {
                g.DrawRectangle(borderPen, 0, 0, ClientSize.Width - 1, ClientSize.Height - 1);
            }
        }

        // ── Drag support ─────────────────────────────────────────
        protected override void OnMouseDown(MouseEventArgs e) {
            if (e.Button == MouseButtons.Left) {
                dragging = true;
                dragStart = new Point(e.X, e.Y);
            }
            base.OnMouseDown(e);
        }

        protected override void OnMouseMove(MouseEventArgs e) {
            if (dragging) {
                Point p = PointToScreen(e.Location);
                Location = new Point(p.X - dragStart.X, p.Y - dragStart.Y);
            }
            base.OnMouseMove(e);
        }

        protected override void OnMouseUp(MouseEventArgs e) {
            dragging = false;
            base.OnMouseUp(e);
        }

        // ── Tooltip owner-draw ───────────────────────────────────
        private void ToolTip_Popup(object sender, PopupEventArgs e) {
            string text = toolTip.GetToolTip(e.AssociatedControl);
            using (Graphics g = e.AssociatedControl.CreateGraphics()) {
                using (Font f = new Font("Segoe UI", 9f)) {
                    SizeF sz = g.MeasureString(text, f, 350);
                    e.ToolTipSize = new Size((int)Math.Ceiling(sz.Width) + 16, (int)Math.Ceiling(sz.Height) + 12);
                }
            }
        }

        private void ToolTip_Draw(object sender, DrawToolTipEventArgs e) {
            using (SolidBrush bgBrush = new SolidBrush(Color.FromArgb(20, 20, 30))) {
                e.Graphics.FillRectangle(bgBrush, e.Bounds);
            }
            using (Pen borderPen = new Pen(CyanAccent, 1f)) {
                e.Graphics.DrawRectangle(borderPen, 0, 0, e.Bounds.Width - 1, e.Bounds.Height - 1);
            }
            using (SolidBrush textBrush = new SolidBrush(Color.White)) {
                using (Font f = new Font("Segoe UI", 9f)) {
                    e.Graphics.DrawString(e.ToolTipText, f, textBrush, new RectangleF(8, 6, e.Bounds.Width - 16, e.Bounds.Height - 12));
                }
            }
        }

        // ── Layout ───────────────────────────────────────────────
        private void InitializeLayout() {
            SuspendLayout();

            // ── LEFT PANEL ──────────────────────────────────────
            DoubleBufferedPanel leftPanel = new DoubleBufferedPanel();
            leftPanel.Location = new Point(10, 10);
            leftPanel.Size = new Size(280, ClientSize.Height - 20);
            leftPanel.BackColor = Color.Transparent;
            leftPanel.AutoScroll = false;

            int y = 10;

            // Title
            Label headerLabel = new Label();
            headerLabel.Text = "WOW OPTIMIZE";
            headerLabel.Font = new Font("Segoe UI", 18f, FontStyle.Bold);
            headerLabel.ForeColor = CyanAccent;
            headerLabel.AutoSize = true;
            headerLabel.Location = new Point(15, y);
            headerLabel.BackColor = Color.Transparent;
            leftPanel.Controls.Add(headerLabel);
            y += headerLabel.PreferredHeight + 0;

            // Dev subtitle
            Label devLabel = new Label();
            devLabel.Text = "by Suprematist";
            devLabel.Font = new Font("Segoe UI", 8.5f, FontStyle.Italic);
            devLabel.ForeColor = CyanAccent;
            devLabel.AutoSize = true;
            devLabel.Location = new Point(17, y);
            devLabel.BackColor = Color.Transparent;
            leftPanel.Controls.Add(devLabel);
            y += devLabel.PreferredHeight + 2;

            // Subheader
            Label subHeaderLabel = new Label();
            subHeaderLabel.Text = "MOD CONFIGURATOR & LAUNCHER";
            subHeaderLabel.Font = new Font("Segoe UI", 7.5f, FontStyle.Regular);
            subHeaderLabel.ForeColor = SubHeaderColor;
            subHeaderLabel.AutoSize = true;
            subHeaderLabel.Location = new Point(17, y);
            subHeaderLabel.BackColor = Color.Transparent;
            leftPanel.Controls.Add(subHeaderLabel);
            y += subHeaderLabel.PreferredHeight + 18;

            // ── Master Buttons ──────────────────────────────────
            int btnWidth = 248;

            DarkButton btnEnableAll = new DarkButton(CyanAccent, false);
            btnEnableAll.Text = "ENABLE ALL FEATURES";
            btnEnableAll.Size = new Size(btnWidth, 30);
            btnEnableAll.Location = new Point(15, y);
            btnEnableAll.Click += delegate { ToggleAll(true); };
            leftPanel.Controls.Add(btnEnableAll);
            y += 36;

            DarkButton btnDisableAll = new DarkButton(Color.FromArgb(255, 23, 68), false);
            btnDisableAll.Text = "DISABLE ALL (VANILLA)";
            btnDisableAll.Size = new Size(btnWidth, 30);
            btnDisableAll.Location = new Point(15, y);
            btnDisableAll.Click += delegate { ToggleAll(false); };
            leftPanel.Controls.Add(btnDisableAll);
            y += 36;

            DarkButton btnDefaults = new DarkButton(Color.FromArgb(100, 110, 140), false);
            btnDefaults.Text = "RESTORE SAFE DEFAULTS";
            btnDefaults.Size = new Size(btnWidth, 30);
            btnDefaults.Location = new Point(15, y);
            btnDefaults.Click += delegate { RestoreDefaults(); };
            leftPanel.Controls.Add(btnDefaults);
            y += 36;

            DarkButton btnSaveProfile = new DarkButton(CyanAccent, false);
            btnSaveProfile.Text = "SAVE PROFILE...";
            btnSaveProfile.Size = new Size(btnWidth, 30);
            btnSaveProfile.Location = new Point(15, y);
            btnSaveProfile.Click += delegate { SaveProfile(); };
            leftPanel.Controls.Add(btnSaveProfile);
            y += 36;

            DarkButton btnLoadProfile = new DarkButton(CyanAccent, false);
            btnLoadProfile.Text = "LOAD PROFILE...";
            btnLoadProfile.Size = new Size(btnWidth, 30);
            btnLoadProfile.Location = new Point(15, y);
            btnLoadProfile.Click += delegate { LoadProfile(); };
            leftPanel.Controls.Add(btnLoadProfile);
            y += 36;

            DarkButton btnShareProfile = new DarkButton(Color.FromArgb(255, 179, 0), false);
            btnShareProfile.Text = "SHARE WITH DEVELOPER";
            btnShareProfile.Size = new Size(btnWidth, 30);
            btnShareProfile.Location = new Point(15, y);
            btnShareProfile.Click += delegate { ShareProfileWithDev(); };
            leftPanel.Controls.Add(btnShareProfile);
            y += 36;

            // ── Separator ───────────────────────────────────────
            DoubleBufferedPanel separator = new DoubleBufferedPanel();
            separator.Size = new Size(btnWidth, 1);
            separator.Location = new Point(15, y + 4);
            separator.BackColor = SeparatorColor;
            leftPanel.Controls.Add(separator);
            y += 18;

            // ── DLL Status Card ─────────────────────────────────
            DoubleBufferedPanel statusCard = new DoubleBufferedPanel();
            statusCard.Size = new Size(btnWidth, 54);
            statusCard.Location = new Point(15, y);
            statusCard.BackColor = PanelBg;
            statusCard.BorderStyle = BorderStyle.None;
            statusCard.Paint += delegate(object sender, PaintEventArgs pe) {
                using (Pen bp = new Pen(Color.FromArgb(35, 35, 50), 1f)) {
                    pe.Graphics.DrawRectangle(bp, 0, 0, statusCard.Width - 1, statusCard.Height - 1);
                }
            };

            Label statusTitle = new Label();
            statusTitle.Text = "MODULE STATUS:";
            statusTitle.Font = new Font("Segoe UI", 7.5f, FontStyle.Bold);
            statusTitle.ForeColor = Color.FromArgb(140, 140, 170);
            statusTitle.AutoSize = true;
            statusTitle.Location = new Point(10, 6);
            statusTitle.BackColor = Color.Transparent;
            statusCard.Controls.Add(statusTitle);

            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            bool dllActive = File.Exists(Path.Combine(exeDir, "version.dll")) &&
                             File.Exists(Path.Combine(exeDir, "wow_optimize.dll"));

            Label statusVal = new Label();
            statusVal.Text = dllActive ? "OPTIMIZER ACTIVE (version.dll)" : "NOT LOADED / MISSING DLLs";
            statusVal.Font = new Font("Segoe UI", 9f, FontStyle.Bold);
            statusVal.ForeColor = dllActive ? Color.FromArgb(0, 230, 118) : Color.FromArgb(255, 145, 0);
            statusVal.AutoSize = true;
            statusVal.Location = new Point(10, 26);
            statusVal.BackColor = Color.Transparent;
            statusCard.Controls.Add(statusVal);

            leftPanel.Controls.Add(statusCard);
            y += 64;

            // ── Active Modules Counter ──────────────────────────
            activeCountLabel = new Label();
            activeCountLabel.Font = new Font("Segoe UI", 8.5f, FontStyle.Regular);
            activeCountLabel.ForeColor = SubtextColor;
            activeCountLabel.AutoSize = true;
            activeCountLabel.Location = new Point(17, y);
            activeCountLabel.BackColor = Color.Transparent;
            activeCountLabel.Text = "Active modules: 0/" + settingsMap.Count.ToString();
            leftPanel.Controls.Add(activeCountLabel);
            y += 20;

            // ── Progress Bar ────────────────────────────────────
            progressBarPanel = new DoubleBufferedPanel();
            progressBarPanel.Size = new Size(btnWidth, 4);
            progressBarPanel.Location = new Point(17, y);
            progressBarPanel.BackColor = SeparatorColor;
            progressBarPanel.Paint += ProgressBar_Paint;
            leftPanel.Controls.Add(progressBarPanel);
            y += 16;

            // ── LAUNCH WOW Button ───────────────────────────────
            DarkButton btnLaunch = new DarkButton(CyanAccent, true);
            btnLaunch.Text = "LAUNCH WOW";
            btnLaunch.Size = new Size(btnWidth, 45);
            btnLaunch.Font = new Font("Segoe UI", 11f, FontStyle.Bold);
            btnLaunch.Location = new Point(15, y);
            btnLaunch.Click += delegate { LaunchWow(); };
            leftPanel.Controls.Add(btnLaunch);
            y += 51;

            // ── EXIT Button ─────────────────────────────────────
            DarkButton btnExit = new DarkButton(Color.FromArgb(60, 60, 70), false);
            btnExit.Text = "EXIT LAUNCHER";
            btnExit.Size = new Size(btnWidth, 30);
            btnExit.Location = new Point(15, y);
            btnExit.Click += delegate { Close(); };
            leftPanel.Controls.Add(btnExit);
            y += 36;

            // ── Version Label ───────────────────────────────────
            versionLabel = new Label();
            versionLabel.Text = "v" + APP_VERSION + "-Release";
            versionLabel.Font = new Font("Segoe UI", 7f, FontStyle.Regular);
            versionLabel.ForeColor = Color.FromArgb(90, 90, 110);
            versionLabel.AutoSize = true;
            versionLabel.Location = new Point(17, y);
            versionLabel.BackColor = Color.Transparent;
            leftPanel.Controls.Add(versionLabel);

            Controls.Add(leftPanel);

            // ── RIGHT PANEL ─────────────────────────────────────
            int rightX = 300;
            int rightW = ClientSize.Width - rightX - 10;

            // Tip label
            Label tipLabel = new Label();
            tipLabel.Text = "Tip: Hover over any optimization feature to view a detailed description of its behavior.";
            tipLabel.Font = new Font("Segoe UI", 8.5f, FontStyle.Italic);
            tipLabel.ForeColor = CyanAccent;
            tipLabel.AutoSize = false;
            tipLabel.Size = new Size(rightW - 270, 20);
            tipLabel.Location = new Point(rightX, 15);
            tipLabel.BackColor = Color.Transparent;
            Controls.Add(tipLabel);

            // Search Label
            Label searchLabel = new Label();
            searchLabel.Text = "Search:";
            searchLabel.Font = new Font("Segoe UI", 9f, FontStyle.Bold);
            searchLabel.ForeColor = Color.White;
            searchLabel.AutoSize = true;
            searchLabel.Location = new Point(rightX + rightW - 260, 15);
            searchLabel.BackColor = Color.Transparent;
            Controls.Add(searchLabel);

            // Search TextBox
            searchBox = new TextBox();
            searchBox.Font = new Font("Segoe UI", 9f, FontStyle.Regular);
            searchBox.BackColor = Color.FromArgb(20, 20, 30);
            searchBox.ForeColor = Color.White;
            searchBox.BorderStyle = BorderStyle.FixedSingle;
            searchBox.Location = new Point(rightX + rightW - 200, 12);
            searchBox.Size = new Size(190, 20);
            searchBox.TextChanged += delegate { FilterFeatures(searchBox.Text); };
            Controls.Add(searchBox);

            // TabControl
            tabs = new DarkTabControl();
            tabs.Location = new Point(rightX, 40);
            tabs.Size = new Size(rightW, ClientSize.Height - 55);
            tabs.SelectedIndexChanged += delegate {
                if (searchBox != null) {
                    FilterFeatures(searchBox.Text);
                }
            };

            // Create tab pages
            TabPage tpGeneral = CreateTabPage("GENERAL");
            TabPage tpUiLua = CreateTabPage("UI & LUA");
            TabPage tpCombatNet = CreateTabPage("COMBAT & NET");
            TabPage tpGraphicsSound = CreateTabPage("GRAPHICS & SOUND");

            tabs.TabPages.Add(tpGeneral);
            tabs.TabPages.Add(tpUiLua);
            tabs.TabPages.Add(tpCombatNet);
            tabs.TabPages.Add(tpGraphicsSound);

            // Get the scroll panels from each tab page
            generalFlow = (FlowLayoutPanel)((Panel)tpGeneral.Controls[0]).Controls[0];
            uiLuaFlow = (FlowLayoutPanel)((Panel)tpUiLua.Controls[0]).Controls[0];
            combatNetFlow = (FlowLayoutPanel)((Panel)tpCombatNet.Controls[0]).Controls[0];
            graphicsSoundFlow = (FlowLayoutPanel)((Panel)tpGraphicsSound.Controls[0]).Controls[0];

            // Add "ENABLE ALL IN ..." buttons at top of each flow
            btnEnableGeneral = CreateCategoryButton("ENABLE ALL IN GENERAL");
            btnEnableGeneral.Click += delegate { ToggleCategoryAction("General", btnEnableGeneral, "GENERAL"); };
            generalFlow.Controls.Add(btnEnableGeneral);

            btnEnableUiLua = CreateCategoryButton("ENABLE ALL IN UI & LUA");
            btnEnableUiLua.Click += delegate { ToggleCategoryAction("UI_Lua", btnEnableUiLua, "UI & LUA"); };
            uiLuaFlow.Controls.Add(btnEnableUiLua);

            btnEnableCombatNet = CreateCategoryButton("ENABLE ALL IN COMBAT & NET");
            btnEnableCombatNet.Click += delegate { ToggleCategoryAction("Combat_Net", btnEnableCombatNet, "COMBAT & NET"); };
            combatNetFlow.Controls.Add(btnEnableCombatNet);

            btnEnableGfx = CreateCategoryButton("ENABLE ALL IN GRAPHICS & SOUND");
            btnEnableGfx.Click += delegate { ToggleCategoryAction("Graphics_Sound", btnEnableGfx, "GRAPHICS & SOUND"); };
            graphicsSoundFlow.Controls.Add(btnEnableGfx);

            // Populate checkboxes
            foreach (KeyValuePair<string, SettingItem> pair in settingsMap) {
                string name = pair.Key;
                SettingItem data = pair.Value;
                DarkCheckBox chk = CreateStyledCheckBox(name, data.Tooltip);

                data.Ctrl = chk;

                chk.CheckedChanged += delegate { UpdateActiveModulesCount(); };

                switch (data.Section) {
                    case "General":
                        generalFlow.Controls.Add(chk);
                        break;
                    case "UI_Lua":
                        uiLuaFlow.Controls.Add(chk);
                        break;
                    case "Combat_Net":
                        combatNetFlow.Controls.Add(chk);
                        break;
                    case "Graphics_Sound":
                        graphicsSoundFlow.Controls.Add(chk);
                        break;
                }
            }

            Controls.Add(tabs);
            ResumeLayout(false);
        }

        private void FilterFeatures(string query) {
            query = (query ?? "").Trim().ToLower();
            bool hasSearch = !string.IsNullOrEmpty(query);

            TabPage activeTab = (tabs != null) ? tabs.SelectedTab : null;
            FlowLayoutPanel activeFlow = null;
            if (activeTab != null && activeTab.Controls.Count > 0) {
                Control scrollPanel = activeTab.Controls[0];
                if (scrollPanel.Controls.Count > 0) {
                    activeFlow = scrollPanel.Controls[0] as FlowLayoutPanel;
                }
            }

            if (generalFlow == null || uiLuaFlow == null || combatNetFlow == null ||
                graphicsSoundFlow == null) {
                return;
            }

            // Temporarily clear all flow panels
            generalFlow.Controls.Clear();
            uiLuaFlow.Controls.Clear();
            combatNetFlow.Controls.Clear();
            graphicsSoundFlow.Controls.Clear();

            // Category buttons visibility
            if (btnEnableGeneral != null) btnEnableGeneral.Visible = !hasSearch;
            if (btnEnableUiLua != null) btnEnableUiLua.Visible = !hasSearch;
            if (btnEnableCombatNet != null) btnEnableCombatNet.Visible = !hasSearch;
            if (btnEnableGfx != null) btnEnableGfx.Visible = !hasSearch;

            // Put category buttons back if not searching
            if (!hasSearch) {
                generalFlow.Controls.Add(btnEnableGeneral);
                uiLuaFlow.Controls.Add(btnEnableUiLua);
                combatNetFlow.Controls.Add(btnEnableCombatNet);
                graphicsSoundFlow.Controls.Add(btnEnableGfx);
            }

            foreach (KeyValuePair<string, SettingItem> pair in settingsMap) {
                string name = pair.Key;
                SettingItem data = pair.Value;

                // Match only by name (case-insensitive)
                bool isMatch = !hasSearch || name.ToLower().Contains(query);

                if (hasSearch) {
                    if (isMatch && data.Ctrl != null && activeFlow != null) {
                        data.Ctrl.Visible = true;
                        activeFlow.Controls.Add(data.Ctrl);
                    } else if (data.Ctrl != null) {
                        data.Ctrl.Visible = false;
                    }
                } else {
                    // Restore to original tab flows
                    if (data.Ctrl != null) {
                        data.Ctrl.Visible = true;
                        switch (data.Section) {
                            case "General": generalFlow.Controls.Add(data.Ctrl); break;
                            case "UI_Lua": uiLuaFlow.Controls.Add(data.Ctrl); break;
                            case "Combat_Net": combatNetFlow.Controls.Add(data.Ctrl); break;
                            case "Graphics_Sound": graphicsSoundFlow.Controls.Add(data.Ctrl); break;
                        }
                    }
                }
            }
        }

        private TabPage CreateTabPage(string title) {
            TabPage tp = new TabPage(title);
            tp.BackColor = DarkBg;
            tp.ForeColor = Color.White;
            tp.Padding = new Padding(0);

            // Scrollable container panel
            Panel scrollPanel = new Panel();
            scrollPanel.Dock = DockStyle.Fill;
            scrollPanel.AutoScroll = true;
            scrollPanel.BackColor = DarkBg;

            scrollPanel.Scroll += delegate(object sender, ScrollEventArgs e) {
                scrollPanel.Invalidate(true);
                scrollPanel.Update();
            };

            scrollPanel.MouseWheel += delegate(object sender, MouseEventArgs e) {
                scrollPanel.Invalidate(true);
                scrollPanel.Update();
            };

            DoubleBufferedFlowPanel flow = new DoubleBufferedFlowPanel();
            flow.FlowDirection = FlowDirection.TopDown;
            flow.WrapContents = false;
            flow.AutoSize = true;
            flow.AutoSizeMode = AutoSizeMode.GrowOnly;
            flow.BackColor = DarkBg;
            flow.Padding = new Padding(5, 10, 5, 10);
            flow.Width = tabs.Width - 40;

            scrollPanel.Controls.Add(flow);
            tp.Controls.Add(scrollPanel);
            return tp;
        }

        private DarkButton CreateCategoryButton(string text) {
            DarkButton btn = new DarkButton(CyanAccent, false);
            btn.Text = text;
            btn.Size = new Size(tabs.Width - 60, 28);
            btn.Font = new Font("Segoe UI", 7.5f, FontStyle.Bold);
            btn.Margin = new Padding(5, 5, 5, 12);
            return btn;
        }

        private DarkCheckBox CreateStyledCheckBox(string name, string tooltipText) {
            DarkCheckBox chk = new DarkCheckBox();
            chk.Text = name;
            toolTip.SetToolTip(chk, tooltipText);
            return chk;
        }

        private void ProgressBar_Paint(object sender, PaintEventArgs e) {
            if (settingsMap == null) return;
            int activeCount = 0;
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Ctrl != null && item.Ctrl.Checked) {
                    activeCount++;
                }
            }
            int totalCount = settingsMap.Count;
            if (totalCount == 0) return;

            int fillWidth = (int)((float)activeCount / totalCount * progressBarPanel.Width);
            using (SolidBrush cyanBrush = new SolidBrush(CyanAccent)) {
                e.Graphics.FillRectangle(cyanBrush, 0, 0, fillWidth, progressBarPanel.Height);
            }
        }

        // ── Settings Logic ───────────────────────────────────────

        private void ToggleAll(bool enabled) {
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Ctrl != null) {
                    item.Ctrl.Checked = enabled;
                }
            }
        }

        private void ToggleTabFeatures(string section, bool enabled) {
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Section == section && item.Ctrl != null) {
                    item.Ctrl.Checked = enabled;
                }
            }
        }

        private void ToggleCategoryAction(string section, DarkButton btn, string labelName) {
            bool allChecked = true;
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Section == section && item.Ctrl != null && !item.Ctrl.Checked) {
                    allChecked = false;
                    break;
                }
            }

            bool nextState = !allChecked;
            ToggleTabFeatures(section, nextState);
            UpdateCategoryButtonTexts();
        }

        private void UpdateCategoryButtonTexts() {
            if (settingsMap == null) return;

            if (btnEnableGeneral != null) {
                bool all = true;
                foreach (SettingItem item in settingsMap.Values) {
                    if (item.Section == "General" && item.Ctrl != null && !item.Ctrl.Checked) {
                        all = false;
                        break;
                    }
                }
                btnEnableGeneral.Text = all ? "DISABLE ALL IN GENERAL" : "ENABLE ALL IN GENERAL";
            }

            if (btnEnableUiLua != null) {
                bool all = true;
                foreach (SettingItem item in settingsMap.Values) {
                    if (item.Section == "UI_Lua" && item.Ctrl != null && !item.Ctrl.Checked) {
                        all = false;
                        break;
                    }
                }
                btnEnableUiLua.Text = all ? "DISABLE ALL IN UI & LUA" : "ENABLE ALL IN UI & LUA";
            }

            if (btnEnableCombatNet != null) {
                bool all = true;
                foreach (SettingItem item in settingsMap.Values) {
                    if (item.Section == "Combat_Net" && item.Ctrl != null && !item.Ctrl.Checked) {
                        all = false;
                        break;
                    }
                }
                btnEnableCombatNet.Text = all ? "DISABLE ALL IN COMBAT & NET" : "ENABLE ALL IN COMBAT & NET";
            }

            if (btnEnableGfx != null) {
                bool all = true;
                foreach (SettingItem item in settingsMap.Values) {
                    if (item.Section == "Graphics_Sound" && item.Ctrl != null && !item.Ctrl.Checked) {
                        all = false;
                        break;
                    }
                }
                btnEnableGfx.Text = all ? "DISABLE ALL IN GRAPHICS & SOUND" : "ENABLE ALL IN GRAPHICS & SOUND";
            }
        }

        private void RestoreDefaults() {
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Ctrl != null) {
                    item.Ctrl.Checked = item.DefaultVal;
                }
            }
        }

        private void LoadSettings() {
            LoadSettingsFromPath(iniPath);
        }

        private void LoadSettingsFromPath(string path) {
            if (!File.Exists(path)) {
                RestoreDefaults();
                return;
            }

            try {
                string[] lines = File.ReadAllLines(path);
                Dictionary<string, string> currentSettings = new Dictionary<string, string>();

                foreach (string line in lines) {
                    string trimmed = line.Trim();
                    if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith(";") || trimmed.StartsWith("["))
                        continue;

                    string[] parts = trimmed.Split('=');
                    if (parts.Length == 2) {
                        currentSettings[parts[0].Trim()] = parts[1].Trim();
                    }
                }

                foreach (string name in new List<string>(settingsMap.Keys)) {
                    SettingItem data = settingsMap[name];
                    string val;
                    if (currentSettings.TryGetValue(data.Key, out val)) {
                        data.Ctrl.Checked = (val == "1" || val.ToLower() == "true");
                    } else {
                        data.Ctrl.Checked = data.DefaultVal;
                    }
                }
                UpdateActiveModulesCount();
            } catch (Exception ex) {
                MessageBox.Show("Error loading config profile: " + ex.Message, "Load Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                RestoreDefaults();
            }
        }

        private void SaveSettings() {
            SaveSettingsToPath(iniPath);
        }

        private void SaveSettingsToPath(string path) {
            try {
                string dir = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir)) {
                    Directory.CreateDirectory(dir);
                }

                Dictionary<string, List<string>> sections = new Dictionary<string, List<string>>() {
                    { "General", new List<string>() },
                    { "UI_Lua", new List<string>() },
                    { "Combat_Net", new List<string>() },
                    { "Graphics_Sound", new List<string>() }
                };

                foreach (SettingItem item in settingsMap.Values) {
                    string val = (item.Ctrl != null && item.Ctrl.Checked) ? "1" : "0";
                    sections[item.Section].Add(item.Key + "=" + val);
                }

                using (StreamWriter sw = new StreamWriter(path, false, Encoding.UTF8)) {
                    sw.WriteLine("; WoW-Optimize Mod Configuration Profile");
                    sw.WriteLine("; Generated by Launcher");
                    sw.WriteLine();

                    foreach (KeyValuePair<string, List<string>> section in sections) {
                        sw.WriteLine("[" + section.Key + "]");
                        foreach (string line in section.Value) {
                            sw.WriteLine(line);
                        }
                        sw.WriteLine();
                    }
                }
            } catch (Exception ex) {
                MessageBox.Show("Error saving config profile: " + ex.Message, "Save Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void SaveProfile() {
            SaveFileDialog sfd = new SaveFileDialog();
            sfd.Filter = "Configuration Profiles (*.ini)|*.ini";
            sfd.FileName = "wow_opt_profile.ini";
            sfd.Title = "Save Configuration Profile";
            if (sfd.ShowDialog() == DialogResult.OK) {
                SaveSettingsToPath(sfd.FileName);
                MessageBox.Show("Profile successfully saved to:\n" + sfd.FileName, "Profile Saved", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
        }

        private void LoadProfile() {
            OpenFileDialog ofd = new OpenFileDialog();
            ofd.Filter = "Configuration Profiles (*.ini)|*.ini";
            ofd.Title = "Load Configuration Profile";
            if (ofd.ShowDialog() == DialogResult.OK) {
                LoadSettingsFromPath(ofd.FileName);
                MessageBox.Show("Profile successfully loaded from:\n" + ofd.FileName, "Profile Loaded", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
        }

        private void ShareProfileWithDev() {
            try {
                StringBuilder sb = new StringBuilder();
                sb.AppendLine("; SUGGESTED SAFE PROFILE PRESET");
                sb.AppendLine("; Submit to Suprematist");
                sb.AppendLine();

                Dictionary<string, List<string>> sections = new Dictionary<string, List<string>>() {
                    { "General", new List<string>() },
                    { "UI_Lua", new List<string>() },
                    { "Combat_Net", new List<string>() },
                    { "Graphics_Sound", new List<string>() }
                };

                foreach (SettingItem item in settingsMap.Values) {
                    string val = (item.Ctrl != null && item.Ctrl.Checked) ? "1" : "0";
                    sections[item.Section].Add(item.Key + "=" + val);
                }

                foreach (KeyValuePair<string, List<string>> section in sections) {
                    sb.AppendLine("[" + section.Key + "]");
                    foreach (string line in section.Value) {
                        sb.AppendLine(line);
                    }
                    sb.AppendLine();
                }

                Clipboard.SetText(sb.ToString());

                MessageBox.Show(
                    "Your current profile settings have been copied to the clipboard!\n\n" +
                    "Please paste and share them with the developer (Suprematist) via Discord or GitHub Issues " +
                    "to suggest making this profile safe by default in future updates.",
                    "Profile Copied to Clipboard",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information
                );
            } catch (Exception ex) {
                MessageBox.Show("Failed to copy profile to clipboard: " + ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void CheckForUpdatesAsync() {
            System.Threading.ThreadPool.QueueUserWorkItem(delegate {
                try {
                    using (System.Net.WebClient wc = new System.Net.WebClient()) {
                        wc.Headers.Add("User-Agent", "WoW-Optimize-Launcher");
                        string rawVer = wc.DownloadString("https://raw.githubusercontent.com/suprepupre/wow-optimize/main/version.txt?t=" + DateTime.UtcNow.Ticks.ToString());
                        if (!string.IsNullOrEmpty(rawVer)) {
                            string cleanVer = rawVer.Trim();
                            Version latest = new Version(cleanVer);
                            Version current = new Version(APP_VERSION);

                            if (latest > current) {
                                BeginInvoke(new Action(delegate { ShowUpdateAlert(cleanVer); }));
                            }
                        }
                    }
                } catch {
                    // Fail silently on network errors
                }
            });
        }

        private void ShowUpdateAlert(string latestVer) {
            if (versionLabel != null) {
                versionLabel.Text = "UPDATE AVAILABLE: v" + latestVer;
                versionLabel.ForeColor = Color.FromArgb(0, 230, 118);
                versionLabel.Font = new Font("Segoe UI", 7f, FontStyle.Bold);
                versionLabel.Cursor = Cursors.Hand;
                toolTip.SetToolTip(versionLabel, "Click to open GitHub releases page for upgrade!");
                versionLabel.Click += delegate {
                    try {
                        Process.Start("https://github.com/suprepupre/wow-optimize/releases");
                    } catch {
                        // ignore
                    }
                };
            }
        }

        private void LaunchWow() {
            // 1. Save Settings
            SaveSettings();

            // 2. Locate target executable
            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            string wowPath = Path.Combine(exeDir, "wow.exe");

            if (!File.Exists(wowPath)) {
                string[] alternateNames = { "Ascension.exe", "run.exe", "WoWCircle.exe", "wow-64.exe", "Sirus.exe" };
                foreach (string altName in alternateNames) {
                    string altPath = Path.Combine(exeDir, altName);
                    if (File.Exists(altPath)) {
                        wowPath = altPath;
                        break;
                    }
                }
            }

            // Fallback: search for any .exe containing "wow" or "ascension" that isn't the launcher/loader itself
            if (!File.Exists(wowPath)) {
                try {
                    string[] files = Directory.GetFiles(exeDir, "*.exe");
                    foreach (string file in files) {
                        string name = Path.GetFileName(file).ToLower();
                        if (name != "wow_optimize_launcher.exe" && name != "wow_loader.exe" && 
                            (name.Contains("wow") || name.Contains("ascension") || name.Contains("circle") || name.Contains("sirus"))) {
                            wowPath = file;
                            break;
                        }
                    }
                } catch {
                    // Ignore directory read errors
                }
            }

            if (!File.Exists(wowPath)) {
                MessageBox.Show("Could not find wow.exe, Ascension.exe, or another valid game executable in the current directory: " + exeDir + "\n\nPlease place the launcher in your World of Warcraft directory.", "Execution Error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            try {
                ProcessStartInfo psi = new ProcessStartInfo();
                psi.FileName = wowPath;
                psi.WorkingDirectory = exeDir;
                Process.Start(psi);

                // Exit launcher on launch
                Close();
            } catch (Exception ex) {
                MessageBox.Show("Failed to launch " + Path.GetFileName(wowPath) + ": " + ex.Message, "Execution Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void UpdateActiveModulesCount() {
            if (settingsMap == null) return;
            int activeCount = 0;
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Ctrl != null && item.Ctrl.Checked) {
                    activeCount++;
                }
            }
            if (activeCountLabel != null) {
                activeCountLabel.Text = "Active modules: " + activeCount.ToString() + "/" + settingsMap.Count.ToString();
            }
            if (progressBarPanel != null) {
                progressBarPanel.Invalidate();
            }
            UpdateCategoryButtonTexts();
        }
    }

    // ───────────────────────────────────────────────────────────────
    //  Application Entry Point
    // ───────────────────────────────────────────────────────────────
    public static class Program {
        [STAThread]
        public static void Main() {
            try {
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new MainForm());
            } catch (Exception ex) {
                try {
                    string crashPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "launcher_crash.txt");
                    File.WriteAllText(crashPath, ex.ToString());
                } catch {
                    // Ignore secondary logging failures
                }
                MessageBox.Show("Fatal launcher error:\n" + ex.Message + "\n\nDetails saved to launcher_crash.txt", "Fatal Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
