// ============================================================================
// WatchBar - runtime configuration
// ----------------------------------------------------------------------------
// The panel ships NO ini file of its own. Every setting lives in the game's
// own uimd.ini, in one section, with the WatchBar. prefix:
//
//     [WatchBar]
//     WatchBar.PanelX=0
//     WatchBar.TopPercent=25
//     WatchBar.DoneColor=120,255,140
//
// and the two art decisions that belong to a faction live where that art
// already lives - in rulesmd.ini (see docs/config.md):
//
//     [GDI]                    ; the side's own section
//     WatchBar.CenterPCX=swside01center.pcx
//     WatchBar.OnPCX=swside01on.pcx
//     WatchBar.OffPCX=swside01off.pcx
//
//     [British]                ; the country's own section
//     File.Flag=C4_FLAG.PCX
//
// Why the game's own ini instead of a file beside the exe: a mod already ships
// one set of inis, the engine has read them before the first frame is drawn,
// and a second file is one more thing to install, document and keep in sync.
// The price is that the values are fixed for the process lifetime - uimd.ini is
// read by the engine at startup, so a change needs a game restart.
//
// Design rules (keep them if you extend this):
//
//   1. ARRAY BOUNDS STAY COMPILE-TIME. kHardMaxRows / kHardMaxLinesPerPlayer /
//      kHardMaxIconsPerLine / kHardMaxTypesPerGroup size real arrays in
//      WatchBar.cpp. Ini values are CLAMPED to those caps and the clamp is
//      reported, never silent.
//   2. NO ENGINE DEPENDENCY. This unit is pure Win32 + CRT. The caller hands it
//      a settings source (WatchBarIniSource) and it never touches a game object,
//      so the exact same parser serves the DLL and tools\ini_check.
//   3. PARSING NEVER LOGS. Issues are collected into Messages[] and drained by
//      the caller once the log settings from this very file are in effect.
// ============================================================================
#pragma once

#include <windows.h>

// ---------------------------------------------------------------- version
#define WATCHBAR_VERSION "1.3.3"

// The host this build hooks: YR 1.001 gamemd.exe. The two hook sites are
// absolute addresses in that build, so a different exe is a warning, not a
// setting.
static constexpr DWORD kExpectedExeTimestamp = 0x3BDF544E;

// --------------------------------------------------------------- hard caps
// Array bounds. Changing these changes the DLL's memory layout, nothing else.
enum
{
    kHardMaxRows           = 8,   // YR supports 8 players
    kHardMaxLinesPerPlayer = 8,   // rows in one player's grid
    kHardMaxIconsPerLine   = 16,  // columns in one player's grid
    kHardMaxCells         = kHardMaxLinesPerPlayer * kHardMaxIconsPerLine,
    kHardMaxTypesPerGroup = 64,  // Types tallied per group. INTERNAL: it sizes the
                                 // tally arrays and is neither a parameter nor a
                                 // display limit. A player fielding 64 distinct
                                 // types of one category is not a real scenario,
                                 // so the tally does not drop anything in
                                 // practice - and if it ever did, the log names
                                 // the group instead of hiding the loss.
    kCfgMaxMessages       = 24,  // parse diagnostics kept for the log
    kCfgMaxUnknownKeys    = 16,  // typo'd keys reported per load
};

// ------------------------------------------------------------------ colour
struct WatchBarColor
{
    int R, G, B;
};

// ------------------------------------------------------------------- modes
enum WatchBarSortMode
{
    kSortTech = 0,       // TechLevel high -> low (the original behaviour)
    kSortCount,          // alive count high -> low
    kSortName,           // UIName, A -> Z
};

enum WatchBarLogLevel
{
    kLogOff = 0,
    kLogWarningsOnly,    // only lines starting with WARNING / containing ERROR
    kLogInfo,            // + state changes (rows, cameo, gate, scroll) - default
    kLogDiagnostic,      // + everything else the code emits
};

// ------------------------------------------------------------- line budget
// How many lines of icons one player's block may hold.
//
//   fixed   - WatchBar.MaxLinesPerPlayer, exactly as it always was. This is the
//             default, so an existing uimd.ini keeps behaving as it did.
//   compact - adaptive. The band's whole-line capacity (VisLines in
//             WatchBar.cpp) is shared out among the rows that are actually
//             drawn, so the board is GUARANTEED not to scroll. The uniform
//             share is n = visLines / rows, and every row that wants more than
//             it is guaranteed at least n; a row that wants fewer lines than
//             its share leaves the rest to the rows that want more, so a player
//             fielding many types gets a taller block than one fielding two.
//             The ceiling is LinesCap, not n: a row may pass n when its
//             neighbours want less, which is the point of sharing rather than
//             dividing.
//   loose   - the same, with one line per row beyond what fits: the budget
//             becomes visLines + rows, so with every row wanting more each one
//             gets n+1 - and the board scrolls when any row passes its share.
//   ultra   - the same with two lines per row (n+2 when every row wants more).
//
// Every row keeps at least ONE line in every mode, including a row with nothing
// to show: the flag and the player name live in that line, so n = 0 would
// delete the player rather than shorten them. WatchBar.LinesCap is the per-row
// ceiling in the three adaptive modes; WatchBar.MaxLinesPerPlayer is not read
// then (the log says so).
enum WatchBarLinesMode
{
    kLinesFixed = 0,
    kLinesCompact,
    kLinesLoose,
    kLinesUltra,
};

// ------------------------------------------------------- participant rows
// WatchBar.ParticipantRows: how wide the board is for a PARTICIPANT - the local
// player when WatchBar.SpectatorOnly=0 has put a board in front of someone who
// is actually fighting.
//
// A spectator is not affected: a spectator's board is the whole match, which is
// the point of the panel. This key exists because a participant's board is a
// different question. It is drawn over their own playfield, and every row is
// live information about a house they would otherwise have to scout for, so the
// audience is a decision rather than a given.
//
//   own    - the local player's own house(s), nothing else. The default, since
//            it shows a participant nothing they could not already see.
//   allies - own houses plus every house allied with the local player
//            (HouseClass::IsAlliedWith), which is what a team game wants.
//   all    - every combatant, exactly like a spectator's board. This is the
//            authoring mode: the only way to watch the full board and its line
//            budget while a real match runs. It also reproduces the behaviour
//            SpectatorOnly=0 had before this key existed.
enum WatchBarParticipantRows
{
    kRowsOwn = 0,
    kRowsAllies,
    kRowsAll,
};

// ------------------------------------------------------------- icon groups
// The blocks a row is built from, in the order [WatchBar] GroupOrder lists
// them. The names are the ones the ini keys use, so the mapping is readable
// without a lookup table:
//   production - the two production cells (structure tab, then ordnance tab);
//                they always travel together and keep that internal order
//   building   - counted buildings (WatchBar.CountBuilding), capped by
//                MaxIconsPerGroup like every other block
//   infantry / vehicle / aircraft - the three counted unit groups
enum WatchBarIconGroup
{
    kGroupProduction = 0,
    kGroupBuilding,
    kGroupInfantry,
    kGroupVehicle,
    kGroupAircraft,
    kGroupCount
};

// --------------------------------------------------------- settings source
// Where the [WatchBar] keys come from.
//
// The DLL installs a source over the engine's ALREADY-LOADED uimd.ini
// (CCINIClass::INI_UIMD), so a uimd.ini that lives inside a MIX - EC ships no
// loose one - is read exactly as the game read it, with no second parser to
// disagree with the engine's.
//
// tools\ini_check installs nothing and the parser falls back to reading a loose
// uimd.ini from the exe's folder, which is also the safety net if the engine
// source is ever not installed. Both paths run the same key logic below, so the
// checker can never disagree with the game about what a key means.
struct WatchBarIniSource
{
    const char* Name;   // for the log: "uimd.ini (engine)" / "uimd.ini (file)"

    // >0 and out[] filled when the key is present, 0 when it is not. `def` is
    // returned (and 0 reported) for an absent key; an empty value reads as
    // absent, which is what "leave it blank for the default" means everywhere.
    int (*Read)(const char* section, const char* key, const char* def,
                char* out, int cap);

    // Keys the section carries, 0 when the section does not exist. This is what
    // separates "the author configured nothing" from "the author misspelled a
    // key": the first is silence, the second is reported.
    int (*KeyCount)(const char* section);

    // Name of key <index> in <section>, or nullptr. Only used to report keys
    // the parser does not recognise.
    const char* (*KeyName)(const char* section, int index);
};

// Install the source the parser reads from. Call before the first
// ConfigBeginFrame(); without one, a loose uimd.ini beside the exe is used.
void ConfigSetIniSource(const WatchBarIniSource* pSource);

// ------------------------------------------------------------- key prefix
// Every key in [WatchBar] is spelled WatchBar.<Key>. The prefix is what makes
// the section self-describing in a file full of other people's keys; the bare
// spelling is accepted too, so a value copied out of this documentation cannot
// be wrong.
#define kCfgKeyPrefix "WatchBar."

// ------------------------------------------------------------------ config
struct WatchBarConfig
{
    // ---- geometry ---------------------------------------------------------
    // uimd.ini [WatchBar]: WatchBar.PanelX, WatchBar.TopMargin, ...
    int PanelX;              // left edge of the board (0 = flush to the view)
    int TopMargin;           // fixed part of the top edge
    int TopPercent;          // + this % of the tactical view height
    int BandMargin;          // gap between the band's bottom and the view's
    int LabelWidth;          // flag + player-name column
    int CellWidth;           // one cameo cell, frame included
    int CellHeight;
    int RowGap;              // vertical gap between player rows
    int IconsPerLine;        // columns: icons before the row wraps
    int MaxLinesPerPlayer;   // rows; the row holds MaxLinesPerPlayer * IconsPerLine
                             // (kLinesFixed only - see LinesMode)
    int LinesMode;           // WatchBarLinesMode: fixed / compact / loose / ultra
    int LinesCap;            // adaptive modes: per-row line ceiling (<= kHardMaxLinesPerPlayer)
    int MaxRows;             // <= kHardMaxRows
    int CameoWidth;
    int CameoHeight;
    int CameoOffsetX;        // inset of the cameo inside the cell
    int CameoOffsetY;
    int FlagWidth;
    int FlagHeight;
    int ToggleWidth;         // the on/off strip beside the board
    int ToggleHeight;

    // ---- art --------------------------------------------------------------
    // The art's FILE NAMES are not configured here: they are read from the
    // loaded rules, out of the section that already owns them - the side's own
    // [<Side>] section for the frames and the toggle strip
    // (WatchBar.CenterPCX / WatchBar.OnPCX / WatchBar.OffPCX), and the
    // country's section for the flag (File.Flag). See SidePCX / FlagPCX in
    // WatchBar.cpp.
    //
    // What is left here is the one art decision that is not a filename.
    int ClockEnabled;        // WatchBar.ClockEnabled: draw the gclock2 sweep

    // ---- colours ----------------------------------------------------------
    // WatchBar.DoneColor, WatchBar.ProgressTextColor, ...
    WatchBarColor DoneColor;         // finished, waiting to be placed
    WatchBarColor ProgressTextColor; // fallback digits when gclock2 is missing
    WatchBarColor QueueTextColor;    // "+N" queued depth
    WatchBarColor CountChipColor;    // backing plate of the alive-count badge
    WatchBarColor CountTextColor;
    WatchBarColor IdleTextColor;     // "waiting for match..."
    WatchBarColor ScrollTrackColor;  // the 2px indicator on the board's edge
    WatchBarColor ScrollThumbColor;
    WatchBarColor ScrollGlyphActiveColor;
    WatchBarColor ScrollGlyphIdleColor;
    WatchBarColor ScrollGlyphHeldColor;
    WatchBarColor ToggleGlyphOnColor;
    WatchBarColor ToggleGlyphOffColor;

    // ---- text -------------------------------------------------------------
    // On-screen strings, WatchBar.DoneText / WatchBar.IdleText. Stored wide
    // because the panel draws wide: a CJK value works, and the engine routes it
    // to its CJK font on its own.
    //
    // The value is looked up in the engine's string table first, so it may be a
    // CSF label (the shipped defaults are TXT_READY / TXT_WAITING, which is what
    // makes the board follow the game's own language); a value with no matching
    // label is drawn as written. See TextOrCsf in WatchBar.cpp.
    //
    // The three FONTS are deliberately not here: GAME.FNT has not been
    // reversed, so Point8 is a ceiling rather than a choice, and a font key
    // would only be a way to make the board unreadable. They are fixed in
    // WatchBar.cpp.
    wchar_t DoneText[24];    // structure finished, waiting to be placed
    wchar_t IdleText[48];    // shown while no match data exists yet

    // ---- animation --------------------------------------------------------
    // WatchBar.FadeMs, WatchBar.MoveMs, ...
    int FadeMs;              // dissolve in/out; 0 = no dissolve at all
    int MoveMs;              // slot glide; 0 = snap, else must stay below FadeMs
    int ScrollStep;          // lines per arrow click; 0 = one visible page
    int ScrollButtonWidth;   // the pager's own size, not the toggle strip's
    int ScrollButtonHeight;
    int ScrollButtonOffsetX; // shifts the centred pair left/right; 0 = centred
                             // on the board's occupied width
    int ScrollRepeatDelayMs; // hold this long before auto-repeat starts
    int ScrollRepeatRateMs;  // then one step per this
    int ScanIntervalMs;      // WatchBar.ScanIntervalMs: 0 = rescan every frame

    // ---- content ----------------------------------------------------------
    // WatchBar.ShowStructures, WatchBar.CountBuilding, ...
    int ShowStructures;      // include structures under construction
    int ShowUnits;           // master switch for the fielded UNIT groups
    int CountBuilding;       // + buildings standing on the map (own group)
    int CountInfantry;       // + infantry, when ShowUnits is on
    int CountVehicle;        // + ground/naval vehicles, when ShowUnits is on
    int CountAircraft;       // + aircraft, when ShowUnits is on (own group)
    int MaxIconsPerGroup[kGroupCount];  // Cells each block may hold (1..kHardMaxCells;
                                        // the row's own cell count = no cap).
                                        // Default: building 3, everything else
                                        // uncapped. Cells a cap frees go to the
                                        // groups behind it. This is the ONLY
                                        // limit on what a row shows: every type
                                        // the board tallies is counted exactly,
                                        // caps only decide how many cells get
                                        // drawn.
    int ShowCountChip;       // the alive-count badge
    int SortMode;            // WatchBarSortMode (every counted group)
    int GroupOrder[kGroupCount];  // WatchBarIconGroup values, one each:
                                  // left-to-right order of the row's blocks.
                                  // The LAST group in the list is the first to
                                  // be cut when the row runs out of cells.

    // ---- gate -------------------------------------------------------------
    // WatchBar.SpectatorOnly / WatchBar.ParticipantRows / WatchBar.ShowWhenDefeated
    int SpectatorOnly;       // 1 = the board exists only for spectators
    int ParticipantRows;     // WatchBarParticipantRows: who a PARTICIPANT sees
                             // (only read when SpectatorOnly is 0)
    int ShowWhenDefeated;    // count a defeated house as a spectator

    // ---- log --------------------------------------------------------------
    // WatchBar.LogEnabled / WatchBar.LogLevel / ...
    int  LogEnabled;
    int  LogLevel;           // WatchBarLogLevel
    int  LogMaxKB;
    char LogPath[MAX_PATH];  // empty = <game folder>\WatchBar.log

    // ---- load bookkeeping (not read from the ini) -------------------------
    bool Loaded;             // the settings have been read (see ConfigLoad)
    int  Source;             // WatchBarConfigSource
    const char* SourceName;  // the source's own name, for the log
    int  SectionFound;       // keys [WatchBar] carried; 0 = no such section
    DWORD ExeTimestamp;
    int  MessageCount;
    char Messages[kCfgMaxMessages][160];
    int  UnknownKeyCount;
    char UnknownKeys[kCfgMaxUnknownKeys][64];
};

// Where the values came from.
enum WatchBarConfigSource
{
    kCfgSourceNone = 0,   // nothing readable: every value is the built-in default
    kCfgSourceEngine,     // the engine's own uimd.ini (the in-game path)
    kCfgSourceFile,       // a loose uimd.ini (tools\ini_check)
};

extern WatchBarConfig g_WatchBarCfg;

// Every use site reads the config through this accessor, so no value is ever
// cached: the macros in WatchBar.cpp all read the one loaded struct.
inline const WatchBarConfig& Cfg() { return g_WatchBarCfg; }

// Cells one player's row holds under the FIXED line budget: rows x columns.
// The adaptive modes derive the same number per row from the band instead (see
// ApplyLineBudgets in WatchBar.cpp), so this is the kLinesFixed value - and the
// number the log names when it reports that MaxLinesPerPlayer is not in use.
inline int CfgRowCells(const WatchBarConfig& c)
{
    return c.MaxLinesPerPlayer * c.IconsPerLine;
}

// ------------------------------------------------------- line budget maths
// Whole icon lines a view of this height leaves for the board.
//
// The DLL calls this with the live view; tools\ini_check calls it with a table
// of common resolutions. Sharing the arithmetic is the point: an author tuning
// the line budget offline must get the number the game will compute, and a
// second copy of the formula in the checker could only drift from this one.
//
// viewY is the view rectangle's top in screen coordinates, because the board's
// top edge is a screen coordinate too (PanelY in WatchBar.cpp) - pass 0 for
// "the view starts at the top of the screen", which is every resolution the
// game actually runs at.
inline int CfgVisLines(const WatchBarConfig& c, int viewH, int viewY = 0)
{
    const int pitch = c.CellHeight + c.RowGap;
    if (viewH <= 0 || pitch <= 0)
        return 0;

    const int top    = c.TopMargin + (viewH * c.TopPercent) / 100;
    const int bottom = viewY + viewH - c.BandMargin;
    const int bandH  = bottom - top;
    return bandH > 0 ? bandH / pitch : 0;
}

// Lines each drawn row may show this frame.
//
//   need[i]  the lines row i would use if nothing else competed for the band.
//            need 0 is fine: a row with nothing to show still gets one line,
//            because that line carries the flag and the player name.
//   rows     how many rows are drawn (<= kHardMaxRows)
//   visLines CfgVisLines() for this view
//   out[i]   the lines row i may show, each in [1, LinesCap]
// Returns the lines the whole board uses (the sum of out[]), which is what the
// draw path's height is bounded by.
//
//   kLinesFixed   - every row gets MaxLinesPerPlayer; need[] is ignored, which
//                   is exactly the pre-adaptive behaviour.
//   kLinesCompact - the band is shared out and the total never exceeds
//                   visLines, so the board cannot scroll. A row wanting less
//                   than its share leaves the rest to the rows wanting more.
//   kLinesLoose   - the same, one line per row beyond what fits.
//   kLinesUltra   - the same, two lines per row beyond what fits.
//
// The adaptive split is max-min fair: one line at a time to the row with the
// most unmet need, ties to the earliest row (the order HouseClass::Array lists
// the houses, so the same board always splits the same way). That is what keeps
// a sparse player from hoarding lines they would never draw - the uniform share
// n = visLines / rows is the floor every unsatisfied row still gets, not the
// ceiling everyone is stuck with.
inline int CfgAllocLines(const WatchBarConfig& c, const int* need, int rows,
                         int visLines, int* out)
{
    if (rows <= 0 || !out)
        return 0;
    if (rows > kHardMaxRows)
        rows = kHardMaxRows;

    int cap = c.LinesCap;
    if (cap < 1)
        cap = 1;
    if (cap > kHardMaxLinesPerPlayer)
        cap = kHardMaxLinesPerPlayer;

    if (c.LinesMode == kLinesFixed)
    {
        const int lines = c.MaxLinesPerPlayer >= 1 ? c.MaxLinesPerPlayer : 1;
        for (int i = 0; i < rows; ++i)
            out[i] = lines;
        return lines * rows;
    }

    const int extra = c.LinesMode == kLinesLoose ? 1
                    : c.LinesMode == kLinesUltra ? 2 : 0;
    const int budget = visLines + extra * rows;

    // Rule 1: every drawn row keeps its label line, whatever the band holds.
    // This is not cosmetic - it is what keeps "one row per player" true when the
    // band is shorter than the player list (8 players on a small view), and it
    // is why the board may still scroll in compact mode in that one case.
    int total = 0;
    for (int i = 0; i < rows; ++i)
    {
        out[i] = 1;
        ++total;
    }

    while (total < budget)
    {
        int pick = -1;
        int best = 0;
        for (int i = 0; i < rows; ++i)
        {
            if (out[i] >= cap)
                continue;
            const int unmet = (need ? need[i] : 1) - out[i];
            if (unmet > best)
            {
                best = unmet;
                pick = i;
            }
        }
        if (pick < 0)
            break;   // every row is satisfied or at the ceiling
        ++out[pick];
        ++total;
    }

    return total;
}

// Read the settings. Idempotent and called for you by ConfigBeginFrame(); the
// checker calls it directly. Always leaves a usable config behind: with no
// source and no file, every value is the built-in default.
void ConfigLoad();

// Load on the first frame. The engine reads uimd.ini once at startup, so there
// is nothing to poll afterwards - a change needs a game restart.
void ConfigBeginFrame();

// The loose uimd.ini path, for the log and the checker's banner.
const char* ConfigIniPath();

// Collect a parse/validation message. Called by Config.cpp only.
void ConfigNote(WatchBarConfig& c, const char* fmt, ...);
