// ============================================================================
// WatchBar - in-game spectator board for Yuri's Revenge
// ----------------------------------------------------------------------------
// Horizontal sidebar-style layout: one row per player. Each row has a fixed
// label column (country flag + player id) followed by a DYNAMIC strip of
// cameo icons, four per line, wrapping to a second line when needed:
//
//   [structures being built, gclock2 production clock]
//   [defence structures being built, gclock2 production clock]
//   [buildings standing on the map, alive count]      <- CountBuilding=1
//   [vehicles / ships on the map, alive count in the top-right]
//   [aircraft on the map, alive count in the top-right]
//   [infantry on the map, alive count in the top-right]
//
// Structures only appear while they are actually in production - a player who
// builds nothing shows no structure icon. Counted groups appear by their
// on-map presence: one icon per distinct type, badged with how many are alive.
// Which groups exist is WatchBar.ShowStructures for the production icons,
// WatchBar.ShowUnits (master) plus WatchBar.CountInfantry / CountVehicle /
// CountAircraft for the unit groups, WatchBar.CountBuilding for the building
// group; the order the blocks are laid out in is WatchBar.GroupOrder, and the
// default above is the layout this board has always drawn (vehicles and the
// aircraft that ride with them ahead of the infantry). WatchBar.MaxIconsPerGroup
// caps each block (buildings ship with a cap of 3, the one group that can grow
// without bound).
// Types with no drawable cameo (no CameoPCX=, no real Cameo= SHP) are skipped
// by the collector entirely: an empty recess with a bare percentage or count
// reads as a rendering glitch, not as information.
//
// Two optional rulesmd.ini keys tune the counting per type: CountAs= folds a
// variant into another type's counter, IgnoreCount=yes keeps a type (sub-units,
// slaves, anything) off the board completely.
//
// The board is spectator-only by default: it exists while the local player is
// an observer and is absent - down to the toggle strip and its hit-box - for
// participants and outside matches. WatchBar.SpectatorOnly=0 puts it in front
// of participants too (authoring), and WatchBar.ParticipantRows then decides
// whose rows they get: their own by default, allies or everyone if asked.
// Each counted group sorts by TechLevel, high first, and the blocks keep the
// configured order. This replaced the old fixed four-slot production board:
// what a player is producing is only half the picture, the fielded army is the
// other half, and a dynamic list shows both.
//
// Built as a Syringe-injectable DLL, the same mechanism Ares / Phobos use.
// Loaded via:
//   Syringe.exe gamemd.exe -i=Ares.dll -i=Phobos.dll -i=WatchBar.dll
//
// Reads HouseClass / FactoryClass / the unit arrays directly in-process: no
// ReadProcessMemory and no admin rights.
//
// Every tunable lives in the game's own uimd.ini, in one [WatchBar] section -
// see Config.h for the contract and docs/config.md for the key reference. The
// art file names are not settings at all: they are read from rulesmd.ini, out
// of the section that already owns each picture (a side's frames, a country's
// flag). The defaults in Config.cpp are the reference build, so an absent
// [WatchBar] section changes nothing.
// ============================================================================
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <algorithm>
#include <YRPP.h>
#include <Helpers/Macro.h>
#include <Syringe.h>
#include <HouseClass.h>
#include <BuildingTypeClass.h>
#include <UnitTypeClass.h>
#include <InfantryTypeClass.h>
#include <AircraftTypeClass.h>
#include <UnitClass.h>
#include <InfantryClass.h>
#include <AircraftClass.h>
#include <FactoryClass.h>
#include <SidebarClass.h>
#include <Surface.h>
#include <Drawing.h>
#include <PCX.h>
#include <CommandClass.h>
#include <StringTable.h>
#include <GameStrings.h>
#include <Memory.h>
#include <GScreenClass.h>
#include <GadgetClass.h>
#include <MouseClass.h>
#include <CCINIClass.h>

#include "Config.h"

// ---------------------------------------------------------------- config
// The panel behaves like the game's own super-weapon sidebar: it is shown or
// hidden by a small always-visible toggle strip, not by a hidden hotkey. The
// state is a plain bool because the two clients that could disagree about it -
// the toggle gadget and the draw hook - both live in this process.
static bool g_PanelOpen = true;

// ---------------------------------------------------------------- logging
// Diagnostic log. The events recorded are a row's icon SET changing and the
// resolution outcome of each cameo; counts and percentages change every frame
// and are never logged. Every cameo resolution is logged once per type, so a
// rendering problem can be traced to a named type.
//
// Path, level and the size cap all come from [WatchBar] in uimd.ini; the path
// defaults to the game folder, never to the working directory, because the
// launcher's CWD is not guaranteed.
static bool  g_LogEnabled = true;
static int   g_LogLevel   = kLogInfo;
static long  g_LogCapBytes = 1024 * 1024;
static bool  g_LogFull    = false;   // set once the log passes its size cap

static void LogLine(const char* fmt, ...);

static void ApplyLogSettings()
{
    const WatchBarConfig& c = Cfg();
    g_LogEnabled  = c.LogEnabled != 0;
    g_LogLevel    = c.LogLevel;
    g_LogCapBytes = static_cast<long>(c.LogMaxKB) * 1024;
}

// Emit the config parser's messages now that WatchBar.LogEnabled/LogLevel are
// in effect, then clear them so they are not repeated.
static void DrainConfigMessages()
{
    WatchBarConfig& c = g_WatchBarCfg;

    // Report the source of the settings and whether the section was present at
    // all: a [WatchBar] section missing from uimd.ini is otherwise
    // indistinguishable from a board that ignores its settings.
    if (c.Source == kCfgSourceNone)
        LogLine("config: no uimd.ini to read (%s) - every value is the built-in "
                "default, version %s", ConfigIniPath(), WATCHBAR_VERSION);
    else if (!c.SectionFound)
        LogLine("config: no [WatchBar] section in %s - every value is the built-in "
                "default (see docs\\uimd-sample.ini), version %s",
                c.SourceName, WATCHBAR_VERSION);
    else
        LogLine("config: %d key(s) from [WatchBar] in %s, version %s, exe timestamp 0x%08X",
                c.SectionFound, c.SourceName, WATCHBAR_VERSION, c.ExeTimestamp);

    if (c.ExeTimestamp && c.ExeTimestamp != kExpectedExeTimestamp)
    {
        LogLine("WARNING: host executable timestamp 0x%08X != expected 0x%08X "
                "(YR 1.001). If this mod ships a patched gamemd.exe the hook "
                "addresses may not mean what WatchBar expects.",
                c.ExeTimestamp, kExpectedExeTimestamp);
    }

    for (int i = 0; i < c.MessageCount; ++i)
        LogLine("config: %s", c.Messages[i]);

    if (c.UnknownKeyCount)
    {
        LogLine("WARNING: %d unrecognised key(s) in [WatchBar] (%s) (typo?):",
                c.UnknownKeyCount, c.SourceName);
        for (int i = 0; i < c.UnknownKeyCount; ++i)
            LogLine("WARNING:   %s", c.UnknownKeys[i]);
    }

    c.MessageCount    = 0;
    c.UnknownKeyCount = 0;
}

static void LogLine(const char* fmt, ...)
{
    if (!g_LogEnabled || g_LogLevel <= kLogOff)
        return;

    char msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(msg, sizeof(msg) - 1, fmt, args);
    va_end(args);
    msg[sizeof(msg) - 1] = '\0';

    // Level 1 keeps only what an author needs when something is wrong. The
    // check is on the finished text, so no call site has to carry a level.
    if (g_LogLevel <= kLogWarningsOnly &&
        strncmp(msg, "WARNING", 7) != 0 && strstr(msg, "ERROR") == nullptr)
        return;

    if (g_LogFull)
        return;

    // Open per line: drawing code must never keep a CRT handle across frames,
    // and the events logged here are rare (state changes, first sightings).
    FILE* f = nullptr;
    if (fopen_s(&f, Cfg().LogPath, "ab") != 0 || !f)
        return;   // transient lock (a reader holding the file): skip this line
                  // and retry on the next one. This must not set g_LogFull - a
                  // single blocked open would silence the rest of the session.

    static DWORD firstTick = 0;
    const DWORD now = GetTickCount();
    if (!firstTick)
        firstTick = now;

    fprintf(f, "[%8u] ", now - firstTick);
    fputs(msg, f);
    fputc('\n', f);
    const long bytes = ftell(f);
    fclose(f);

    // Cap: a runaway log must not grow unbounded across a long session.
    static long logged = 0;
    logged += bytes > 0 ? bytes : 0;
    if (logged > g_LogCapBytes)
        g_LogFull = true;
}


// ------------------------------------------------------- runtime parameters
//
// Every number the board is built from comes from [WatchBar] in uimd.ini (see
// Config.h and docs/config.md). They are macros rather than plain constants so
// the ~200 existing use sites keep their names while the value becomes tunable.
// The config is read once per process - uimd.ini is loaded by the engine at
// startup - so a macro is simply a readable alias for one field.
//
// The compile-time bounds stay in Config.h (kHardMaxRows, kHardMaxLinesPerPlayer,
// kHardMaxIconsPerLine, kHardMaxTypesPerGroup): they size real arrays. The ini
// values are clamped to them by the parser, which reports any clamp in the log
// rather than silently dropping the author's number.
#define PANEL_X             (Cfg().PanelX)
#define LABEL_W             (Cfg().LabelWidth)
#define CELL_W              (Cfg().CellWidth)
#define CELL_H              (Cfg().CellHeight)
#define ROW_GAP             (Cfg().RowGap)
#define ICONS_PER_LINE      (Cfg().IconsPerLine)
#define MAX_ROWS            (Cfg().MaxRows)
#define VIEW_H              (Cfg().TopMargin)
#define VIEW_OFFSET_PCT     (Cfg().TopPercent)
#define SCROLL_MARGIN       (Cfg().BandMargin)
#define CAMEO_W             (Cfg().CameoWidth)
#define CAMEO_H             (Cfg().CameoHeight)
#define CAMEO_X             (Cfg().CameoOffsetX)
#define CAMEO_Y             (Cfg().CameoOffsetY)
#define FLAG_W              (Cfg().FlagWidth)
#define FLAG_H              (Cfg().FlagHeight)
#define TOGGLE_W            (Cfg().ToggleWidth)
#define TOGGLE_H            (Cfg().ToggleHeight)
#define FADE_MS             ((DWORD)Cfg().FadeMs)
#define MOVE_MS             ((DWORD)Cfg().MoveMs)
#define SCROLL_BTN_W        (Cfg().ScrollButtonWidth)
#define SCROLL_BTN_H        (Cfg().ScrollButtonHeight)
#define SCROLL_BTN_OFFSET_X (Cfg().ScrollButtonOffsetX)
#define SCROLL_REPEAT_DELAY_MS ((DWORD)Cfg().ScrollRepeatDelayMs)
#define SCROLL_REPEAT_RATE_MS  ((DWORD)Cfg().ScrollRepeatRateMs)

// The three fonts are FIXED, not configurable.
//
// GAME.FNT has not been reversed, so Point8 is the largest slot verified to
// render - a ceiling rather than a choice - and a font key would only be a way
// to make the board unreadable. Point6Grad is the game's own tooltip font and
// is what the readouts were tuned against at the 60x48 cameo size.
#define kFont      TextPrintType::Point8      // the count badge (needs to read)
#define kFontSmall TextPrintType::Point6Grad  // progress text, idle placeholder
#define kFontTiny  TextPrintType::Point6Grad  // "+N", player names

static COLORREF CfgColor(const WatchBarColor& c)
{
    return Drawing::RGB_To_Int(c.R, c.G, c.B);
}

// ---------------------------------------------------------------- geometry
//
// The cell size is not arbitrary: it has to match the sidebar "center" PCX the
// mod ships (WatchBar.CenterPCX, EC's swsideNNcenter.pcx) together with the
// cameo recess cut into it, so the cameo lands inside that recess instead of on
// its border. The defaults (a 62x50 cell holding a 60x48 cameo at 1,1) are the
// proportions the board is tuned against; a mod whose art differs retunes the
// four numbers instead of editing the source.
//
// PANEL_X is 0, not a small margin: the board is meant to sit flush against the
// left edge of the view, with nothing showing through beside it.

// Top edge of the board for this frame's view size.
//
// DSurface::ViewBounds is the tactical view rectangle, which is what "screen"
// means in-game: it excludes the sidebar and the letterboxing, so 25% of it is
// 25% of the playfield rather than 25% of the window. Phobos centres its
// super-weapon sidebar off the same rectangle.
static int PanelY()
{
    const int viewH = DSurface::ViewBounds.Height;
    if (viewH <= 0)
        return VIEW_H;     // before the first view is set up
    return VIEW_H + (viewH * VIEW_OFFSET_PCT) / 100;
}

// Bottom edge of the band the board may draw in. PanelY already reasons in view
// terms (a percentage of ViewBounds.Height), so the band ends at the view's
// bottom edge with a small margin - not at the composite's edge.
static int BandBottom()
{
    const auto& vb = DSurface::ViewBounds;
    if (vb.Height <= 0)
        return 0x40000000;   // before the first view: never clamp, as PanelY
    return vb.Y + vb.Height - SCROLL_MARGIN;
}

// Country flags: read from each country's own File.Flag= line in the loaded
// rules - see FlagPCX below, the only place that resolves one.

// ---------------------------------------------------------------- row model
// One icon in a row's dynamic list. It is either a structure being built
// (Building = true: progress readout, Count is the queue depth behind it) or a
// counted type standing on the map - a unit or, with CountBuilding=1, a
// building (Building = false: alive count readout).
struct RowIcon
{
    TechnoTypeClass* Type;
    int              Count;    // units: alive count; structures: queued depth
    int              Pct;      // structures: 0..99 build progress (100 = done)
    int              Step;     // structures: raw production step 0..54, the
                               // gclock2 frame index driver (see DrawClock)
    bool             Done;     // structures: finished, waiting to be placed
    bool             Building; // true = draw progress, false = draw count
};

struct PlayerRow
{
    HouseClass* House;
    char        Player[32];   // ANSI copy of the display name, for the log
    wchar_t     PlayerW[32];  // display name: can be CJK (nicknames, 电脑)
    RowIcon     Icons[kHardMaxCells];
    int         IconsCollected; // icons in Icons[], before the line budget is
                                // applied - what the budget is computed FROM.
                                // Never trimmed: a budget derived from the
                                // trimmed count would ratchet down every frame.
    int         IconCount;    // icons drawn this frame: <= LineBudget * IconsPerLine
    int         DrawableIcons;// icons this row could draw with no line budget
    int         HiddenIcons;  // DrawableIcons - IconCount (tail first out)
    int         LineNeed;     // lines the icons want, the line budget ignored
    int         LineBudget;   // lines this row may show this frame (see
                              // ApplyLineBudgets)
};

// ------------------------------------------------------------------ helpers
static bool IsPrintableNameW(const wchar_t* s)
{
    for (; *s; ++s)
    {
        if (*s < 0x20)
            return false;
    }
    return true;
}

// ------------------------------------------------------------------- names
//
// Real player identities instead of the engine's placeholders. Engine facts
// this rests on (gamemd disassembly of House.CPP's MP house naming):
//
//   - AI houses: PlainName = "Computer" (0x50A608) AND UIName (wchar_t[21])
//     = StringTable::LoadString("TXT_COMPUTER") (0x50A666). EC's ra2md.csf
//     has TXT_COMPUTER = "电脑", so the engine has already localised the AI
//     label into UIName. Reading it (or asking the same CSF label ourselves)
//     follows whatever language the game's string tables carry - there is no
//     language detection in this code to get wrong. (The CSF header's language
//     field is useless here: EC ships US=0 while its text is Chinese.)
//   - Human houses in Internet games (SessionClass::GameMode == Internet):
//     PlainName = the lobby node's nickname (0x687FCE).
//   - Human houses otherwise (skirmish/LAN): PlainName = "<human player>"
//     (0x68804A), which is a placeholder and is not shown.
//   - spawn.ini [Settings] Name= is the local player's own game id, written
//     by the CnCNet client for every spawner match. The spawner parses that
//     file as UTF-8 (CP_UTF8 conversion at 0x100069F6 in its DLL), so this
//     code decodes it the same way instead of using the ANSI profile APIs.

// StringTable::LoadString returns L"MISSING:'<label>'" when the label is
// absent and a L"***FATAL***..." stub when no table is loaded (0x734E79 /
// 0x734EC2) - neither may ever reach the font.
static bool IsUsableCsfText(const wchar_t* s)
{
    if (!s || s[0] == L'\0' || s[0] == L'*')
        return false;
    if (s[0] == L'M' && s[1] == L'I' && s[2] == L'S' && s[3] == L'S')
        return false;
    return IsPrintableNameW(s);
}

static const wchar_t* ComputerLabel()
{
    // Same label the engine itself resolves for AI houses, so the panel can
    // never disagree with the game's own score screen. LoadString returns a
    // pointer into the engine's string pool, which lives for the process.
    if (IsUsableCsfText(StringTable::LoadString("TXT_COMPUTER")))
        return StringTable::LoadString("TXT_COMPUTER");
    return L"Computer";
}

// The local player's game id from spawn.ini, read once per process (the
// client writes it before spawning gamemd.exe; one game is one process).
// Parsed by hand because the file is UTF-8: GetPrivateProfileString would
// run it through the ANSI codepage first and mojibake CJK nicknames.
static const wchar_t* LocalSpawnName()
{
    static wchar_t name[32];
    static bool    read = false;
    if (read)
        return name;
    read = true;

    // The game directory (exe's folder), not the CWD: the launcher's working
    // directory is not guaranteed either.
    char path[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
        return name;
    char* slash = strrchr(path, '\\');
    if (!slash)
        return name;
    lstrcpynA(slash + 1, "spawn.ini", sizeof(path) - (slash + 1 - path));

    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f)
        return name;

    static char buf[0x8000];   // spawn.ini stays tiny; static keeps the stack flat
    const size_t cb = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[cb] = '\0';

    // Skip a UTF-8 BOM if the client ever writes one - [Settings] is the
    // first line of the file, and a BOM would otherwise hide it.
    char* scan = buf;
    if (cb >= 3 && memcmp(scan, "\xEF\xBB\xBF", 3) == 0)
        scan += 3;

    // Line-scan for [Settings] and the Name= key inside it. Key and section
    // matching is case-insensitive the way the profile APIs are.
    char* line = scan;
    bool  inSettings = false;
    while (line && *line)
    {
        char* next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        // strip \r
        for (char* end = line + strlen(line); end > line; --end)
        {
            if (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')
                end[-1] = '\0';
            else
                break;
        }
        if (line[0] == '[')
        {
            inSettings = _strnicmp(line, "[settings]", 10) == 0;
        }
        else if (inSettings && _strnicmp(line, "name=", 5) == 0)
        {
            const char* value = line + 5;
            if (*value && MultiByteToWideChar(CP_UTF8, 0, value, -1,
                                              name, 31) > 0
                     && IsPrintableNameW(name))
            {
                name[31] = L'\0';
                break;
            }
            name[0] = L'\0';   // unusable value: keep looking / stay empty
        }
        line = next;
    }
    return name;
}

// PlainName is ANSI: the engine converted the wide lobby nickname down to fit
// the 21-byte buffer when it filled it (and wrote ASCII placeholders itself).
// Map it back through the same codepage for display. Returns a pointer into
// `wide` or a static fallback literal.
static const wchar_t* PlainNameWide(HouseClass* pHouse, wchar_t (&wide)[32])
{
    // Defensive copy first: the 21-byte buffer is not guaranteed to be
    // NUL-terminated if the engine filled all 21 bytes.
    char tmp[22];
    lstrcpynA(tmp, pHouse->PlainName, sizeof(tmp));
    if (MultiByteToWideChar(CP_ACP, 0, tmp, -1, wide, 31) > 0)
    {
        wide[31] = L'\0';
        if (IsPrintableNameW(wide))
            return wide;
    }
    return L"human player";
}

// One funnel for both representations of a row's display name. Never writes
// an empty string: a blank label column would look like a missing flag.
static void StoreRowName(PlayerRow& r, const wchar_t* w)
{
    if (!w || w[0] == L'\0' || !IsPrintableNameW(w))
        w = L"player";

    lstrcpynW(r.PlayerW, w, 32);
    if (!WideCharToMultiByte(CP_ACP, 0, w, -1, r.Player, sizeof(r.Player),
                             nullptr, nullptr))
        lstrcpynA(r.Player, "name", sizeof(r.Player));
}

// Decide what the label column shows for this house. See the block comment
// above "names" for where each source was verified in the engine.
static void ResolveRowName(PlayerRow& r, HouseClass* pHouse)
{
    const wchar_t* w = L"";

    if (!pHouse->IsHumanPlayer)
    {
        // AI: the engine already filled UIName from the CSF's TXT_COMPUTER
        // ("电脑" in EC). Fall back to asking the same label ourselves, then
        // to the plain English string.
        wchar_t tmp[22];
        lstrcpynW(tmp, pHouse->UIName, 21);
        tmp[21] = L'\0';
        w = IsUsableCsfText(tmp) ? tmp : ComputerLabel();
        StoreRowName(r, w);
        return;
    }

    wchar_t wide[32];
    if (pHouse == HouseClass::CurrentPlayer)
    {
        // Local player: spawn.ini carries the actual game id in skirmish,
        // where PlainName is only "<human player>".
        const wchar_t* spawn = LocalSpawnName();
        w = spawn[0] ? spawn : PlainNameWide(pHouse, wide);
    }
    else
    {
        // Other humans: Internet games have the lobby nickname in PlainName;
        // skirmish never has another human house.
        w = PlainNameWide(pHouse, wide);
    }
    StoreRowName(r, w);
}

// Decide which houses belong on the board.
//
// This panel is drawn in-process, so it can read every house's production and
// unit counts regardless of fog of war. If a player who is actually fighting
// could see the enemy's live build queue and army, that is a wallhack. So the
// audience decides:
//   - spectator: every combatant (the spectator holds no spawn slot, so
//     IsCombatant already excludes them from their own list)
//   - participant: WatchBar.ParticipantRows - their own house(s) by default,
//     allies or the whole match only when the author asked for it
//
// This answers "am I a spectator". BoardVisibleToMe below answers "is the board
// on screen for me", which is a different question now that
// WatchBar.SpectatorOnly=0 can put the board in front of a participant.
static bool IsSpectating()
{
    // Prefer the game's own notion of "the current player is an observer".
    if (HouseClass::IsCurrentPlayerObserver())
        return true;

    const auto pCurrent = HouseClass::CurrentPlayer;
    if (!pCurrent)
        return false;

    // A defeated combatant is a spectator too. The engine keeps their spawn
    // slot and never reassigns the Observer global on defeat (0xAC1198 is
    // written only at MP house init), so the checks above cannot see a defeated
    // player. The engine's own sidebar stats module does treat Defeated houses
    // exactly like the observer: 0x6A56AD reads byte [house+0x1F5] (the Defeated
    // flag, set by AcceptDefeat at 0x4FC0C2) and selects the same special state
    // as the Observer compare at 0x6A5694. Matching that flag puts the panel in
    // front of the same audience the game already treats as fully informed.
    if (pCurrent->Defeated)
        return Cfg().ShowWhenDefeated != 0;

    // Edge cases may never have called MakeObserver, so also treat
    // "human player with no spawn slot" as spectating - exactly how YRpp
    // defines IsInitiallyObserver().
    return pCurrent->IsHumanPlayer && pCurrent->GetSpawnPosition() == -1;
}

// Is the board on screen for me at all?
//
// WatchBar.SpectatorOnly=0 keeps the board up for participants as well. It
// exists for authoring - tuning the layout against a live match beats watching
// a replay - and it is the only way WatchBar.ParticipantRows ever gets read.
// The shipped default is 1: a participant must never have an invisible
// clickable control sitting on their playfield.
//
// Every gate that decides whether the panel and its gadgets exist asks THIS,
// never IsSpectating(): a participant with the board up is not a spectator, and
// the row filter below must not treat them as one.
static bool BoardVisibleToMe()
{
    return !Cfg().SpectatorOnly || IsSpectating();
}

// A combatant: not neutral/civilian, and actually placed on the map.
// This is what admits AI players and excludes the spectator.
static bool IsCombatant(HouseClass* pHouse)
{
    if (!pHouse)
        return false;

    if (pHouse->IsNeutral() || pHouse->Type->MultiplayPassive)
        return false;
    if (!pHouse->Type->Multiplay)
        return false;
    if (pHouse->IsObserver())
        return false;

    // Occupying a spawn slot separates "playing" from "watching".
    // GetSpawnPosition() returns -1 for houses not placed on the map.
    return pHouse->GetSpawnPosition() >= 0;
}

static bool IsVisibleToMe(HouseClass* pHouse)
{
    if (!IsCombatant(pHouse))
        return false;

    // A defeated house is out of the match and leaves nothing behind, so its
    // row goes with it - not as a setting, because there is no configuration
    // under which a dead player's empty block is worth a share of the band.
    //
    // This is also what makes the adaptive line budget grow as a match thins
    // out: the engine keeps a defeated house's spawn slot (see IsSpectating
    // above, which needs the Defeated flag for the same reason), so without
    // this test an eliminated player would keep drawing a row - and keep
    // taking a share of the band - for the rest of the match.
    if (pHouse->Defeated)
        return false;

    // A spectator sees the whole match. This is the panel's purpose and no
    // setting narrows it: WatchBar.ParticipantRows is about participants only.
    if (IsSpectating())
        return true;

    // A participant with the board up (WatchBar.SpectatorOnly=0). Own houses are
    // in every mode, so "own" needs no branch of its own - and a house the local
    // player controls but does not own (IsInPlayerControl, campaign house
    // switching) counts as theirs.
    const auto pCurrent = HouseClass::CurrentPlayer;
    if (!pCurrent)
        return false;

    if (pHouse->IsCurrentPlayer() || pHouse->IsInPlayerControl)
        return true;

    switch (Cfg().ParticipantRows)
    {
    case kRowsAll:
        return true;
    case kRowsAllies:
        // The engine's own ally list - the same one the minimap and the team
        // colours use, not a rule of the panel's own.
        return pCurrent->IsAlliedWith(pHouse);
    default:   // kRowsOwn
        return false;
    }
}

// WatchBar.ParticipantRows as the log spells it.
static const char* ParticipantRowsName(int mode)
{
    switch (mode)
    {
    case kRowsAllies: return "allies";
    case kRowsAll:    return "all";
    default:          return "own";
    }
}

// Which production group a factory item lands in on this board:
//   0 = structures (the building tab), 1 = ordnance (the defence tab),
//  -1 = not shown. Units and aircraft are NOT shown from production any more -
//      what a player produces shows up on the map a moment later, and the
//      on-map counts are the point of the new board.
static int ProdGroupOf(TechnoTypeClass* pType)
{
    if (pType->WhatAmI() != AbstractType::BuildingType)
        return -1;

    // The game's own lookup is authoritative for which tab a building lands on
    // (defences go to the ordnance tab); the fallback below is the same rule
    // the sidebar itself applies: BuildCat::Combat means defence.
    const int tab = SidebarClass::GetObjectTabIdx(AbstractType::BuildingType,
        static_cast<BuildingTypeClass*>(pType)->BuildCat, false);

    if (tab == 0 || tab == 1)
        return tab;

    return static_cast<BuildingTypeClass*>(pType)->BuildCat == BuildCat::Combat ? 1 : 0;
}

// Defined in the type-rules section below (it needs the rules INI and its
// per-match cache): is this type marked IgnoreCount=yes, i.e. off the board?
static bool TypeIsIgnored(TechnoTypeClass* pType);

// Structures currently in production for one player.
//
// This walks FactoryClass::Array directly instead of going through
// HouseClass::GetPrimaryFactory. The lookup variant requires (AbstractType,
// naval, BuildCat) to match exactly and fails silently on a mismatch, which
// yields no production lines at all. The array is authoritative: it holds every
// factory in the match, so filtering by Owner cannot miss one.
//
// out[0] is the structure group, out[1] the ordnance group. If a group somehow
// reports two factories, the more advanced line wins so the row shows real
// progress rather than whichever came last in the array.
static void CollectStructureProduction(HouseClass* pHouse, RowIcon out[2])
{
    out[0] = RowIcon {};
    out[1] = RowIcon {};

    for (auto pFactory : FactoryClass::Array)
    {
        if (!pFactory || pFactory->Owner != pHouse)
            continue;

        // The item on the line. Object is set while building and stays set
        // after completion until the player places it.
        TechnoTypeClass* pType = nullptr;

        if (pFactory->Object)
            pType = static_cast<TechnoTypeClass*>(pFactory->Object->GetType());

        if (!pType && pFactory->QueuedObjects.Count > 0)
            pType = pFactory->QueuedObjects[0];

        if (!pType)
            continue;

        // IgnoreCount=yes takes the type off the board entirely, production
        // icon included: otherwise a type the author switched off would still
        // show up while it is being built.
        if (TypeIsIgnored(pType))
            continue;

        const int group = ProdGroupOf(pType);
        if (group < 0)
            continue;

        // Progress is measured in 54 steps - hardcoded in the game, and the
        // number Phobos divides by to drive its cameo clock. The raw step
        // also drives the gclock2 clock frame: the engine's own sidebar draws
        // frame = step + 1 (0x6A9E79: inc eax before DrawSHP).
        const bool done = pFactory->IsDone();
        const int progress = pFactory->GetProgress();

        int pct;
        if (done)
        {
            pct = 100;   // finished, waiting to be placed
        }
        else
        {
            pct = progress < 0 ? 0 : (progress * 100) / 54;
            if (pct > 99)
                pct = 99;
        }

        const int step = done ? 54
                              : (progress < 0 ? 0 : (progress > 53 ? 53 : progress));

        RowIcon& slot = out[group];
        if (slot.Type && slot.Pct >= pct)
            continue;

        slot.Type     = pType;
        slot.Pct      = pct;
        slot.Step     = step;
        slot.Done     = done;
        slot.Building = true;
        slot.Count    = pFactory->QueuedObjects.Count;   // items behind this one
    }
}

// ------------------------------------------------------------- unit counting
// On-map units are tallied per type while walking the typed arrays: one entry
// per distinct type, holding how many of them are alive. The array is sized by
// kHardMaxTypesPerGroup (Config.h), which is an internal ceiling, not a
// parameter and not a display limit - every type the board tallies is counted
// exactly, and the only thing that decides how much of that reaches the screen
// is WatchBar.MaxIconsPerGroup and the row's own cell count.
struct UnitBucketEntry
{
    TechnoTypeClass* Type;
    int              Count;
};

// Which counted group a bucket is. Only used to report an overflow once per
// group; the name is what that line prints.
enum BucketGroup
{
    kBucketBuilding = 0,
    kBucketVehicle,
    kBucketAircraft,
    kBucketInfantry,
    kBucketGroupCount
};

static bool BucketUnit(UnitBucketEntry* bucket, int& count, int cap, TechnoTypeClass* pType)
{
    for (int i = 0; i < count; ++i)
    {
        if (bucket[i].Type == pType)
        {
            ++bucket[i].Count;
            return true;
        }
    }
    if (count >= cap)
        return false;   // dropped: caller reports it
    bucket[count].Type  = pType;
    bucket[count].Count = 1;
    ++count;
    return true;
}

// Report a tally overflow exactly once per session: the condition is permanent
// once it appears (a player fielding more distinct types of one group than the
// internal ceiling), so a per-frame warning would just eat the log's size cap.
// With kHardMaxTypesPerGroup at 64 this should never fire in a real match; it
// exists so that if it ever does, the loss is named instead of silent.
static void WarnBucketOverflow(int group, const char* which, int cap)
{
    static bool s_Warned[kBucketGroupCount] = {};
    if (group < 0 || group >= kBucketGroupCount || s_Warned[group])
        return;
    s_Warned[group] = true;
    LogLine("WARNING: %s bucket full (%d types), further types not counted", which, cap);
}

// Alive and on the field. Passengers are InLimbo (they do not logically exist
// while carried), so a transport's contents do not inflate the counts.
static bool AliveOnField(ObjectClass* pObj)
{
    return pObj->IsAlive && !pObj->InLimbo && pObj->Health > 0;
}

// Count every fielded unit type of one house, into one bucket per category.
//
// Ground/naval vehicles, aircraft and infantry each get their own bucket so
// WatchBar.CountVehicle / CountAircraft / CountInfantry can switch a category
// off, and so the three groups sort and truncate independently. Aircraft are
// filtered harder than ground units because the array carries a lot of support
// traffic that is not part of any player's army:
//   - SpawnOwner != null: missiles/drones a spawner fires (Dreadnought rockets,
//     carrier drones) - ordnance in flight, not fielded units;
//   - Airstrike != null: Boris's MiG and A10 support planes;
//   - spy-plane / paradrop approach+overfly missions: transient support traffic.
// A crashing plane still counts until it hits the ground: it is visibly on the
// map, and the window is a couple of seconds at most. Sub-units that carry none
// of those markers are what the rules key IgnoreCount=yes is for (see the type
// rules below).
static void CollectUnits(HouseClass* pHouse,
                         UnitBucketEntry* vehicles, int& nVehicles,
                         UnitBucketEntry* aircraft, int& nAircraft,
                         UnitBucketEntry* infantry, int& nInfantry)
{
    if (Cfg().CountVehicle)
    {
        for (auto pUnit : UnitClass::Array)
        {
            if (!pUnit || pUnit->Owner != pHouse)
                continue;
            if (!AliveOnField(pUnit))
                continue;
            if (pUnit->IsSinking)   // sliding under: dead, just not removed yet
                continue;
            if (!BucketUnit(vehicles, nVehicles, kHardMaxTypesPerGroup,
                    static_cast<TechnoTypeClass*>(pUnit->GetType())))
            {
                WarnBucketOverflow(kBucketVehicle, "vehicle", kHardMaxTypesPerGroup);
            }
        }
    }

    if (Cfg().CountAircraft)
    {
        for (auto pAir : AircraftClass::Array)
        {
            if (!pAir || pAir->Owner != pHouse)
                continue;
            if (!AliveOnField(pAir))
                continue;
            if (pAir->SpawnOwner || pAir->Airstrike)
                continue;
            switch (pAir->CurrentMission)
            {
            case Mission::SpyplaneApproach:
            case Mission::SpyplaneOverfly:
            case Mission::ParadropApproach:
            case Mission::ParadropOverfly:
                continue;
            default:
                break;
            }
            if (!BucketUnit(aircraft, nAircraft, kHardMaxTypesPerGroup,
                    static_cast<TechnoTypeClass*>(pAir->GetType())))
            {
                WarnBucketOverflow(kBucketAircraft, "aircraft", kHardMaxTypesPerGroup);
            }
        }
    }

    if (Cfg().CountInfantry)
    {
        for (auto pInf : InfantryClass::Array)
        {
            if (!pInf || pInf->Owner != pHouse)
                continue;
            if (!AliveOnField(pInf))
                continue;
            if (!BucketUnit(infantry, nInfantry, kHardMaxTypesPerGroup,
                    static_cast<TechnoTypeClass*>(pInf->GetType())))
            {
                WarnBucketOverflow(kBucketInfantry, "infantry", kHardMaxTypesPerGroup);
            }
        }
    }
}

// Count every building of one house that is standing on the map.
//
// BuildingClass::Array is the authoritative list; ActuallyPlacedOnMap is what
// separates "standing there" from "still in a factory", so a structure that is
// only being built never reaches a count. A building that is playing its
// build-up animation IS on the map and counts.
//
// The wall family is skipped on purpose: a wall line is dozens of segments of
// a handful of types, so counting walls would fill the group with walls and
// push the buildings a spectator actually cares about past the icon cap. Laser
// fence posts and the firestorm wall generator belong to the same family.
// Defence structures are counted like any other building.
static void CollectBuildings(HouseClass* pHouse, UnitBucketEntry* bucket, int& count)
{
    for (auto pBld : BuildingClass::Array)
    {
        if (!pBld || pBld->Owner != pHouse)
            continue;
        if (!AliveOnField(pBld))
            continue;
        if (!pBld->ActuallyPlacedOnMap)
            continue;

        BuildingTypeClass* pType = static_cast<BuildingTypeClass*>(pBld->GetType());
        if (!pType)
            continue;
        if (pType->Wall || pType->LaserFence || pType->LaserFencePost || pType->FirestormWall)
            continue;

        if (!BucketUnit(bucket, count, kHardMaxTypesPerGroup,
                static_cast<TechnoTypeClass*>(pType)))
        {
            WarnBucketOverflow(kBucketBuilding, "building", kHardMaxTypesPerGroup);
        }
    }
}

// ------------------------------------------------------------- type rules
//
// Two optional rulesmd.ini keys, read per declared techno type:
//
//     [SCHD]            ; deployed Siege Chopper
//     CountAs=SCHP      ; its fielded units count as the flying form
//
//     [DRONE]           ; sub-units and other types that are not an army
//     IgnoreCount=yes   ; never counted, never shown on the board
//
// CountAs folds variant types into one counter. Motivation: deploy-conversion
// pairs (Ares Convert.Deploy: SCHP/SCHD, PELI/PELID, NETH/NETHD, RETK/RETKD,
// FTTNK/FTTNK2) and pre-crewed variants (BFRTAA/BFRTAG/BFRTAT, Ares
// InitialPayload) are separate type objects, so the board shows them as
// separate icons. With the key, a player fielding 2 flying + 2 deployed Siege
// Choppers reads "SCHP x4".
//
// IgnoreCount is the "do not count me" switch, aimed at sub-units and any
// other type the author does not want on the board. The collectors already
// filter what the engine itself marks (SpawnOwner / Airstrike aircraft, the
// spy-plane and paradrop missions, passengers, sinking hulls), but a mod's own
// sub-units - slaves, deploy leftovers, decorative types - carry no such
// marker. An ignored type is dropped three times over, which is what makes the
// key a full "hide this type" switch:
//   - its own units never enter a count bucket (raw-type pass, BEFORE the
//     merge, so an ignored type cannot feed the type it redirects to);
//   - a CountAs redirect INTO an ignored type is dropped as well (the same
//     pass, run again after the merge): "count A as B" where B is ignored
//     counts as nothing;
//   - its production icon is skipped too (CollectStructureProduction), so the
//     type cannot reappear on the board as a structure under construction.
//
// Both keys are read from the game's own loaded rules (CCINIClass::INI_Rules),
// once per type per match, through the same pattern the cameo code uses:
// pType->ID is a stable per-type pointer, so the engine's caller-pointer
// cache in ReadString (see the ArtSectionOf note) behaves correctly. The value
// is only used for a type lookup, never as a section name.
//
// Safety contract: with neither key anywhere in the rules every type resolves
// to itself, ignores nothing, both fast-path flags stay false and behaviour is
// unchanged. Every failure mode (unknown target, cross-category target,
// self-redirect, cycle, table overflow) degrades to "count as itself" plus a
// one-time log line - never to hidden units or a crash.
//
// Note on the per-group type cap: it still applies to RAW types before the
// merge (the collectors drop the excess first). Merging only shrinks a bucket,
// so a valid CountAs key can never cause a drop by itself.
static constexpr int TYPE_RULE_MAX = 512;

struct TypeRuleInfo
{
    const TechnoTypeClass* Type;    // key: the declared type
    TechnoTypeClass*       Target;  // what its fielded units count as (== Type when no key)
    bool                   Ignore;  // IgnoreCount=yes: never counted, never shown
};

static TypeRuleInfo g_TypeRule[TYPE_RULE_MAX];
static TypeRuleInfo g_TypeRuleOverflow;   // reused slot once the table is full
static int  g_TypeRuleCount  = 0;
static bool g_RedirectActive = false;   // at least one CountAs redirect resolved
static bool g_IgnoreActive   = false;   // at least one IgnoreCount=yes resolved
static int  g_RedirectCount  = 0;       // for the per-match prefill summary
static int  g_IgnoredCount   = 0;
static bool g_TypeRuleFull   = false;

// Look the target up in the same category as the source, so a redirect can
// never pull a vehicle into the infantry group or vice versa - the board's
// groups are per-category, and a cross-category merge would draw an infantry
// icon inside the vehicle group. The per-class arrays come from YRpp's
// ABSTRACTTYPE_ARRAY; Find compares IDs case-insensitively, like the engine.
// A null return means "no such type in this category" - redirect refused.
static TechnoTypeClass* CountAsFindSameCategory(const TechnoTypeClass* pSrc, const char* pID)
{
    switch (pSrc->WhatAmI())
    {
    case AbstractType::UnitType:     return UnitTypeClass::Find(pID);
    case AbstractType::InfantryType: return InfantryTypeClass::Find(pID);
    case AbstractType::AircraftType: return AircraftTypeClass::Find(pID);
    case AbstractType::BuildingType: return BuildingTypeClass::Find(pID);
    default:                         return nullptr;
    }
}

// Resolve one type by following the (rare) chain: [SCHD] CountAs=[SCHP], and
// if SCHP itself declared a CountAs, follow it too. Revisits stop the walk,
// so a cycle like A->B->A stops at the last valid link instead of spinning;
// each cycle member then resolves to its own successor and the log names the
// loop - degenerate data, degraded safely (units still counted, never lost).
// The hop cap is a second belt to the same braces.
//
// IgnoreCount is read off the DECLARED type, never off whatever it redirects
// to: "do not count me" is a statement about this type. A redirect INTO an
// ignored type is caught after the merge instead (DropIgnored), so both
// spellings do the obvious thing.
static TechnoTypeClass* TypeRuleResolveUncached(TechnoTypeClass* pType, bool& ignore)
{
    ignore = false;
    TechnoTypeClass* result = pType;
    const TechnoTypeClass* visited[8] = { pType };
    int nVisited = 1;

    CCINIClass* pRules = CCINIClass::INI_Rules;
    if (!pRules)
        return result;

    // The engine's own INI bool reader: yes/no, true/false, 1/0.
    ignore = pRules->ReadBool(pType->ID, "IgnoreCount", false);

    for (int hop = 1; hop < static_cast<int>(sizeof(visited) / sizeof(visited[0])); ++hop)
    {
        char target[0x20] = { 0 };
        if (pRules->ReadString(result->ID, "CountAs", "", target, sizeof(target)) <= 0
            || target[0] == '\0')
        {
            break;   // no key: count as itself
        }

        TechnoTypeClass* pNext = CountAsFindSameCategory(result, target);
        if (!pNext)
        {
            LogLine("countas %s: target '%s' not found in its category, counting as itself",
                    result->ID, target);
            break;
        }

        if (pNext == result)
            break;   // self-redirect: a no-op, not worth a log line

        bool revisits = false;
        for (int i = 0; i < nVisited && !revisits; ++i)
            revisits = visited[i] == pNext;
        if (revisits)
        {
            LogLine("countas %s: chain loops at '%s', stopping at '%s'",
                    pType->ID, pNext->ID, result->ID);
            break;
        }

        visited[nVisited++] = pNext;
        result = pNext;
    }

    return result;
}

// Per-type resolution with a match-scoped cache. The table is cleared at every
// row-count change (the match boundary, see DrawPanel): a new scenario
// rebuilds the type arrays, so cached TechnoTypeClass pointers from the
// previous match must not survive it.
static const TypeRuleInfo* TypeRuleFor(TechnoTypeClass* pType)
{
    if (!pType)
        return nullptr;

    for (int i = 0; i < g_TypeRuleCount; ++i)
    {
        if (g_TypeRule[i].Type == pType)
            return &g_TypeRule[i];
    }

    bool ignore = false;
    TechnoTypeClass* target = TypeRuleResolveUncached(pType, ignore);

    if (target != pType)
    {
        g_RedirectActive = true;
        ++g_RedirectCount;
        LogLine("countas %s -> %s", pType->ID, target->ID);
    }
    if (ignore)
    {
        g_IgnoreActive = true;
        ++g_IgnoredCount;
        LogLine("ignorecount %s: never counted, never shown", pType->ID);
    }

    if (g_TypeRuleCount >= TYPE_RULE_MAX)
    {
        if (!g_TypeRuleFull)
        {
            g_TypeRuleFull = true;
            LogLine("WARNING: type-rule table full (%d types), further types resolve uncached",
                    TYPE_RULE_MAX);
        }
        g_TypeRuleOverflow.Type   = pType;
        g_TypeRuleOverflow.Target = target;
        g_TypeRuleOverflow.Ignore = ignore;
        return &g_TypeRuleOverflow;
    }

    TypeRuleInfo& slot = g_TypeRule[g_TypeRuleCount];
    slot.Type   = pType;
    slot.Target = target;
    slot.Ignore = ignore;
    ++g_TypeRuleCount;
    return &slot;
}

// What this type's fielded units count as (itself when it declares no key).
static TechnoTypeClass* CountAsTarget(TechnoTypeClass* pType)
{
    const TypeRuleInfo* pRule = TypeRuleFor(pType);
    return pRule ? pRule->Target : pType;
}

// Is this type marked IgnoreCount=yes?
static bool TypeIsIgnored(TechnoTypeClass* pType)
{
    if (!pType)
        return false;
    const TypeRuleInfo* pRule = TypeRuleFor(pType);
    return pRule && pRule->Ignore;
}

// Clear the per-match table and every fast-path flag derived from it.
static void TypeRulesReset()
{
    g_TypeRuleCount  = 0;
    g_RedirectActive = false;
    g_IgnoreActive   = false;
    g_RedirectCount  = 0;
    g_IgnoredCount   = 0;
    g_TypeRuleFull   = false;
}

// Resolve every declared techno type up front, right after TypeRulesReset. One
// pass over the global array is a few thousand cached INI reads once per
// match boundary, and it turns the log into a self-check: with no keys in the
// rules the summary says so and both fast paths stay disabled for the whole
// match.
static void TypeRulesPrefill()
{
    for (auto pType : TechnoTypeClass::Array)
    {
        if (pType)
            TypeRuleFor(pType);
    }

    if (!g_RedirectActive && !g_IgnoreActive)
    {
        LogLine("typerules: prefill %d type(s), no CountAs/IgnoreCount keys in rules",
                g_TypeRuleCount);
    }
    else
    {
        LogLine("typerules: prefill %d type(s), %d redirect(s), %d ignored",
                g_TypeRuleCount, g_RedirectCount, g_IgnoredCount);
    }
}

// Fold bucket entries through CountAs before the icons are built: raw types
// resolving to the same target become one entry with summed counts, so the
// board draws the target's cameo with the combined number (and sorts by the
// target's TechLevel - the deployed forms are TechLevel=-1 and would
// otherwise sink to the bottom as their own icons). Runs per row per frame,
// but only over the collected entries, and not at all until a redirect was
// actually resolved (g_RedirectActive) - the steady no-key case pays one
// branch per bucket.
static void MergeCountAs(UnitBucketEntry* bucket, int& count)
{
    if (!g_RedirectActive)
        return;

    int out = 0;
    for (int i = 0; i < count; ++i)
    {
        TechnoTypeClass* target = CountAsTarget(bucket[i].Type);

        int j = 0;
        for (; j < out; ++j)
        {
            if (bucket[j].Type == target)
            {
                bucket[j].Count += bucket[i].Count;
                break;
            }
        }
        if (j == out)
        {
            bucket[out].Type  = target;
            bucket[out].Count = bucket[i].Count;
            ++out;
        }
    }
    count = out;
}

// Drop bucket entries whose type is marked IgnoreCount=yes.
//
// Runs twice per bucket - on the RAW types before the merge, and again on the
// merged ones after it - because the two passes catch different spellings: the
// first keeps an ignored type from feeding the type it redirects to, the
// second drops a redirect INTO an ignored type. Both cost one bool test per
// bucket until a key was actually resolved (g_IgnoreActive).
static void DropIgnored(UnitBucketEntry* bucket, int& count)
{
    if (!g_IgnoreActive)
        return;

    int out = 0;
    for (int i = 0; i < count; ++i)
    {
        if (TypeIsIgnored(bucket[i].Type))
            continue;
        bucket[out++] = bucket[i];
    }
    count = out;
}

// The whole per-type pipeline for one collected bucket.
static void ApplyTypeRules(UnitBucketEntry* bucket, int& count)
{
    DropIgnored(bucket, count);
    MergeCountAs(bucket, count);
    DropIgnored(bucket, count);
}

// Board order inside one counted group (buildings, vehicles, aircraft,
// infantry - each group sorts on its own). The default is TechLevel from
// rulesmd.ini, high first; WatchBar.SortMode switches to alive count or to
// the type's UI name. Ties always break by rules order so the result is
// deterministic - an unstable order would make icons swap places for no
// visible reason.
//
// It sorts the TALLY entries rather than finished cells: the cells are built
// straight from the sorted tally, so no per-group copy of the whole tally is
// needed (the row can only hold a handful of cells anyway).
static bool BucketBefore(const UnitBucketEntry& a, const UnitBucketEntry& b)
{
    switch (Cfg().SortMode)
    {
    case kSortCount:
        if (a.Count != b.Count)
            return a.Count > b.Count;
        break;

    case kSortName:
    {
        // Name, not UIName: the rules name is a plain always-populated char
        // array, while UIName is a pointer that can be null (or a
        // "MISSING:..." stub) before the string table is up.
        const int cmp = _stricmp(a.Type->Name, b.Type->Name);
        if (cmp != 0)
            return cmp < 0;
        break;
    }

    default:
    {
        const int ta = a.Type->TechLevel;
        const int tb = b.Type->TechLevel;
        if (ta != tb)
            return ta > tb;
        break;
    }
    }

    return a.Type->GetArrayIndex() < b.Type->GetArrayIndex();
}

// Defined in the cameo section below (it needs ResolveCameoFor): does the type
// carry an icon the board can actually draw? Collector-side filter, so a type
// without one never gets a cell at all.
static bool HasDrawableCameo(TechnoTypeClass* pType);

// A type that already has a production cell on this row is skipped: the board
// shows ONE cell per type, and the animation table keys its records by
// (house, type) - two cells of the same type would be drawn on top of each
// other at one animated slot. Production wins over the count because the
// progress clock is the time-sensitive half, and the case only exists while a
// player is building a type they already have (a second War Factory, a fifth
// pillbox): the count is back the moment the structure is placed.
static bool HasProductionCell(const RowIcon* prodIcons, int nProdIcons, TechnoTypeClass* pType)
{
    for (int i = 0; i < nProdIcons; ++i)
    {
        if (prodIcons[i].Type == pType)
            return true;
    }
    return false;
}

// Turn one tallied group into the cells the row may show: sort the tally, walk
// it in that order and keep the types that are actually drawable, stopping at
// `cap` cells (WatchBar.MaxIconsPerGroup). `drawable` reports how many types
// the group has that COULD be drawn - what the cap or the row's own cell limit
// leaves out is what the caller counts as hidden.
//
// `out` must hold kHardMaxCells cells (caps are clamped to it).
static int AppendCountedGroup(RowIcon* out, UnitBucketEntry* bucket, int count,
                              const RowIcon* prodIcons, int nProdIcons, int cap,
                              int& drawable)
{
    drawable = 0;
    int n = 0;

    if (count > 1)
        std::stable_sort(bucket, bucket + count, BucketBefore);

    for (int i = 0; i < count; ++i)
    {
        if (!HasDrawableCameo(bucket[i].Type))
            continue;
        if (HasProductionCell(prodIcons, nProdIcons, bucket[i].Type))
            continue;

        ++drawable;
        if (n >= cap)
            continue;   // drawable, but the cap keeps it off the row

        out[n]       = RowIcon {};
        out[n].Type  = bucket[i].Type;
        out[n].Count = bucket[i].Count;
        ++n;
    }
    return n;
}

// Build one row: every group is collected first, then laid out in the order
// WatchBar.GroupOrder lists (default: production -> buildings -> vehicles ->
// aircraft -> infantry). Each group is sorted on its own by tech level. The
// row's own cell budget is applied afterwards, by ApplyLineBudgets, so the tail
// is what gets cut there and the LAST group in GroupOrder is the first to lose
// icons. Types with no drawable cameo are dropped on the way in
// (HasDrawableCameo, on the MERGED type for counted groups - the icon would be
// that type's): an icon-less entry renders as an empty recess with a bare
// readout, and the cell budget is better spent on types the board can actually
// show.
static void CollectRow(PlayerRow& r)
{
    const bool wantStructures = Cfg().ShowStructures != 0;
    const bool wantUnits      = Cfg().ShowUnits != 0;

    // ---- production cells
    //
    // The two the game's sidebar calls structures and armour. They always
    // travel together and keep this internal order; GroupOrder only decides
    // where the PAIR sits on the row. Collecting them up front (rather than in
    // place) also lets the counted groups filter against them whatever their
    // position: the board shows one cell per type, and production wins it.
    RowIcon prod[2] = {};
    int nProd = 0;
    if (wantStructures)
    {
        RowIcon slots[2] = {};
        CollectStructureProduction(r.House, slots);
        for (int g = 0; g < 2; ++g)
        {
            if (slots[g].Type && HasDrawableCameo(slots[g].Type))
                prod[nProd++] = slots[g];
        }
    }

    // ---- counted buildings
    //
    // Buildings have their own switch rather than riding on ShowUnits: that key
    // is documented as the fielded-UNIT master, so a board that wants only
    // bases (or only armies) can have either without the other. This is also
    // the group that ships with a cell cap (WatchBar.MaxIconsPerGroup
    // building:3): a base fields far more distinct types than an army does
    // (8-15 mid-game), so an uncapped building block would push every unit off
    // an 8-cell row.
    UnitBucketEntry buildings[kHardMaxTypesPerGroup];
    int nBuildings = 0;
    if (Cfg().CountBuilding)
    {
        CollectBuildings(r.House, buildings, nBuildings);
        ApplyTypeRules(buildings, nBuildings);
    }

    // ---- the three unit groups
    UnitBucketEntry vehicles[kHardMaxTypesPerGroup];
    UnitBucketEntry aircraft[kHardMaxTypesPerGroup];
    UnitBucketEntry infantry[kHardMaxTypesPerGroup];
    int nVehicles = 0;
    int nAircraft = 0;
    int nInfantry = 0;
    if (wantUnits)
    {
        CollectUnits(r.House, vehicles, nVehicles, aircraft, nAircraft, infantry, nInfantry);
        ApplyTypeRules(vehicles, nVehicles);
        ApplyTypeRules(aircraft, nAircraft);
        ApplyTypeRules(infantry, nInfantry);
    }

    // ---- turn every group into the cells it may show
    //
    // Each buffer holds at most one row's worth of cells (kHardMaxCells):
    // WatchBar.MaxIconsPerGroup is applied here, so the layout below is a pure
    // copy. The tally itself is never truncated by these caps - a type the cap
    // leaves off the row is still counted, and `draw*` keeps the bookkeeping
    // straight for the log.
    RowIcon gProd[kHardMaxCells];
    RowIcon gBld[kHardMaxCells];
    RowIcon gInf[kHardMaxCells];
    RowIcon gVeh[kHardMaxCells];
    RowIcon gAir[kHardMaxCells];

    const int capProd = Cfg().MaxIconsPerGroup[kGroupProduction];
    const int showProd = nProd < capProd ? nProd : capProd;
    for (int i = 0; i < showProd; ++i)
        gProd[i] = prod[i];
    const int drawProd = nProd;

    int drawBld = 0, drawInf = 0, drawVeh = 0, drawAir = 0;
    const int showBld = AppendCountedGroup(gBld, buildings, nBuildings, prod, nProd,
                                           Cfg().MaxIconsPerGroup[kGroupBuilding], drawBld);
    const int showInf = AppendCountedGroup(gInf, infantry, nInfantry, prod, nProd,
                                           Cfg().MaxIconsPerGroup[kGroupInfantry], drawInf);
    const int showVeh = AppendCountedGroup(gVeh, vehicles, nVehicles, prod, nProd,
                                           Cfg().MaxIconsPerGroup[kGroupVehicle], drawVeh);
    const int showAir = AppendCountedGroup(gAir, aircraft, nAircraft, prod, nProd,
                                           Cfg().MaxIconsPerGroup[kGroupAircraft], drawAir);

    // ---- lay the blocks out in the configured order
    //
    // Every group is already capped, so this is a copy; the only limit left is
    // the row itself, and it takes cells off the tail - the last group in
    // GroupOrder loses first.
    RowIcon all[2 + 4 * kHardMaxCells];
    int n = 0;

    for (int slot = 0; slot < kGroupCount; ++slot)
    {
        const RowIcon* src = nullptr;
        int srcCount = 0;

        switch (Cfg().GroupOrder[slot])
        {
        case kGroupProduction: src = gProd; srcCount = showProd; break;
        case kGroupBuilding:   src = gBld;  srcCount = showBld;  break;
        case kGroupInfantry:   src = gInf;  srcCount = showInf;  break;
        case kGroupVehicle:    src = gVeh;  srcCount = showVeh;  break;
        case kGroupAircraft:   src = gAir;  srcCount = showAir;  break;
        default: break;
        }

        for (int i = 0; i < srcCount; ++i)
            all[n++] = src[i];
    }

    // Types that did not make it onto the row: the ones a group cap dropped,
    // plus everything the row's own cell budget cuts off the tail.
    //
    // What the row COLLECTS stops at the compile-time cap (the Icons[] array),
    // not at the ini line budget: the budget is applied per frame by
    // ApplyLineBudgets, so a budget that changed because a player left or the
    // view was resized takes effect on the next frame instead of waiting for
    // the next scan (WatchBar.ScanIntervalMs).
    const int drawable = drawProd + drawBld + drawInf + drawVeh + drawAir;
    r.DrawableIcons  = drawable;
    r.IconsCollected = n > kHardMaxCells ? kHardMaxCells : n;
    r.IconCount      = r.IconsCollected;
    r.HiddenIcons    = drawable - r.IconCount;
    if (r.HiddenIcons < 0)
        r.HiddenIcons = 0;
    for (int i = 0; i < r.IconCount; ++i)
        r.Icons[i] = all[i];
}

static int CollectRows(PlayerRow* out, int maxRows)
{
    int n = 0;

    for (auto pHouse : HouseClass::Array)
    {
        if (n >= maxRows)
            break;
        if (!IsVisibleToMe(pHouse))
            continue;

        PlayerRow& r = out[n];
        r.House = pHouse;

        // Real identities: AI shows the engine's own localised TXT_COMPUTER
        // label, the local player shows their spawn.ini game id, other humans
        // show whatever PlainName carries (see ResolveRowName).
        ResolveRowName(r, pHouse);

        CollectRow(r);
        ++n;
    }

    return n;
}

// --------------------------------------------------------------- line budget
//
// "How many lines may one player have" is a TRUNCATION budget, not a reserved
// height: the board's height is content-driven (MetricsFor sums RowLines), so a
// player showing two types draws two cells and one line however tall their
// budget is. What the budget decides is how much of a tall row survives.
//
// WatchBar.LinesMode says where that budget comes from:
//
//   fixed   - WatchBar.MaxLinesPerPlayer, the pre-adaptive behaviour.
//   compact - the band's whole-line capacity shared out among the rows being
//             drawn, so the board provably cannot scroll.
//   loose   - the same, one line per row beyond what fits.
//   ultra   - the same with two.
//
// The arithmetic itself lives in Config.h (CfgVisLines / CfgAllocLines) so that
// tools\ini_check can print the same numbers offline - see PrintLineBudgetTable.

// Whole icon lines this frame's visible band holds.
static int VisLinesNow()
{
    const auto& vb = DSurface::ViewBounds;
    const int pitch = CELL_H + ROW_GAP;
    if (pitch <= 0)
        return 0;

    // Before the first view is set up, PanelY() and BandBottom() deliberately
    // stop clamping (see their comments) - that is "more lines than any board
    // can use", and it is kept here so the pre-view window cannot start
    // truncating rows that the old code drew in full.
    if (vb.Height <= 0)
        return 0x40000000 / pitch;

    return CfgVisLines(Cfg(), vb.Height, vb.Y);
}

// Apply this frame's line budget to every row: measure what each row wants,
// split the band (CfgAllocLines), then trim each row to its share.
//
// Runs EVERY frame on the rows the scan produced, not once per scan: the budget
// depends on the view's height as well as on the player count, so a resolution
// change lands on the next frame instead of at the next scan.
//
// Stability: the split is a pure function of (need[], rowCount, visLines, cap),
// and need[] is read from IconsCollected, which is never trimmed - so the budget
// cannot ratchet itself down, and it does not depend on the height it produces.
// It changes only when a unit TYPE appears or vanishes, which is the same event
// that already adds or removes a cell.
static void ApplyLineBudgets(PlayerRow* rows, int rowCount, int visLines)
{
    // Last split, for the log's change detection. Declared here so the empty
    // case below can invalidate it: a match that ends and the next one that
    // starts with the same player count must still log its split.
    static int s_PrevBudget[kHardMaxRows] = {};
    static int s_PrevRows = -1;

    if (rowCount <= 0)
    {
        s_PrevRows = -1;
        return;
    }

    int need[kHardMaxRows];
    int budget[kHardMaxRows];

    for (int i = 0; i < rowCount; ++i)
    {
        const int lines = (rows[i].IconsCollected + ICONS_PER_LINE - 1) / ICONS_PER_LINE;
        need[i] = lines > 0 ? lines : 1;
        rows[i].LineNeed = need[i];
    }

    CfgAllocLines(Cfg(), need, rowCount, visLines, budget);

    // Report the split whenever it changes. It is a state change like the
    // scroll state (a player left, the view was resized, a row crossed the
    // water level), not per-frame noise - and it is the one line that answers
    // "why is that player's block shorter than the others".
    bool changed = rowCount != s_PrevRows;
    for (int i = 0; i < rowCount && !changed; ++i)
        changed = budget[i] != s_PrevBudget[i];

    if (changed)
    {
        char line[256];
        int len = 0;
        int maxBudget = 0;
        int maxNeed = 0;
        for (int i = 0; i < rowCount; ++i)
        {
            if (len < 180)
                len += wsprintfA(line + len, "%s%d", i ? "," : "", budget[i]);
            if (budget[i] > maxBudget)
                maxBudget = budget[i];
            if (need[i] > maxNeed)
                maxNeed = need[i];
        }

        LogLine("lines: mode=%s rows=%d vis=%d cap=%d -> %s line(s) each "
                "(up to %d cells, rows want up to %d lines)",
                Cfg().LinesMode == kLinesFixed   ? "fixed"   :
                Cfg().LinesMode == kLinesCompact ? "compact" :
                Cfg().LinesMode == kLinesLoose   ? "loose"   : "ultra",
                rowCount, visLines, Cfg().LinesCap, line,
                maxBudget * ICONS_PER_LINE, maxNeed);

        for (int i = 0; i < rowCount; ++i)
            s_PrevBudget[i] = budget[i];
        s_PrevRows = rowCount;
    }

    for (int i = 0; i < rowCount; ++i)
    {
        PlayerRow& r = rows[i];
        r.LineBudget = budget[i];

        const int cells = r.LineBudget * ICONS_PER_LINE;
        if (r.IconCount > cells)
            r.IconCount = cells;

        // The hidden count follows the trim, so the log's "N more type(s) than
        // the row holds" stays truthful in both modes.
        r.HiddenIcons = r.DrawableIcons - r.IconCount;
        if (r.HiddenIcons < 0)
            r.HiddenIcons = 0;
    }
}

// ------------------------------------------------------------------ drawing
//
// All text goes through the explicit six-argument DrawText overload with a font
// bit set. The three-argument convenience overload passes TextPrintType::NoShadow,
// whose font field is LASTPOINT (0) meaning "whatever font was used last", so the
// glyph set is not controlled here.
//
// That matters for the percent sign: the game renders a string containing CJK
// codepoints with its CJK bitmap font, and that font has no '%' glyph, so a
// Chinese item name plus "44%" renders as the name plus "44". Latin digits exist
// in both fonts, which is why only the '%' would be lost. Percentages and counts
// are therefore drawn as their own pure-ASCII strings.
//
// The three fonts are fixed (see the kFont macros at the top of this file):
// they are not part of the ini, because GAME.FNT has not been reversed and a
// font key would only be a way to make the board unreadable.

static void DrawString(DSurface* pSurface, const wchar_t* text, int x, int y,
                       COLORREF color, TextPrintType font)
{
    RectangleStruct bounds = pSurface->GetRect();
    Point2D loc { x, y };

    // Called as DrawTextA, not DrawText: winuser.h defines DrawText as a macro
    // expanding to DrawTextA/DrawTextW, so by the time Surface.h is parsed the
    // member really is named DrawTextA. Phobos' own in-game panel does the same.
    //
    // Takes DSurface*, not Surface*: the six-argument overload lives on DSurface.
    pSurface->DrawTextA(text, &bounds, &loc, color, 0,
                        TextPrintType::FullShadow | font);
}

static int TextHeight(TextPrintType font)
{
    const RectangleStruct r = Drawing::GetTextDimensions(
        L"Ag", Point2D { 0, 0 },
        static_cast<WORD>(static_cast<int>(TextPrintType::FullShadow) |
                          static_cast<int>(font)));
    return r.Height > 0 ? r.Height : 10;
}

// Width of one ASCII string in the given font, so text can be centred.
static int TextWidth(const wchar_t* text, TextPrintType font)
{
    const RectangleStruct r = Drawing::GetTextDimensions(
        text, Point2D { 0, 0 },
        static_cast<WORD>(static_cast<int>(TextPrintType::FullShadow) |
                          static_cast<int>(font)));
    return r.Width;
}

// An ini string that may be a CSF label instead of literal text.
//
//     WatchBar.DoneText=TXT_READY      ; label -> the game's own word for it
//     WatchBar.DoneText=Done           ; no such label -> drawn as written
//
// The lookup is the engine's own (StringTable::TryFetchString), so the board
// reads whatever language the install is set to and needs no table of its own.
// TryFetchString answers with L"MISSING:<label>" for an unknown label, which it
// turns into the caller's default - that is what separates "a label" from "some
// literal text": a literal simply never resolves.
//
// pDefaultLabel is the shipped default for this slot. When the value still IS
// that default and it does not resolve (a CSF without the label - a non-vanilla
// or stripped string table), the built-in literal wins over printing a raw tag
// on screen. A user's own label gets no such rescue: an unresolvable label they
// typed is their typo to see.
//
// Resolved once per config value and cached: the config is read once per process
// and the string table is loaded at startup, so the answer cannot change while
// the game runs - and a literal value, which never resolves, would otherwise
// re-enter the engine's lookup on every frame of every cameo.
static const wchar_t* TextOrCsf(const wchar_t* pValue,
                                const char* pDefaultLabel,
                                const wchar_t* pLiteralFallback)
{
    struct Entry
    {
        const wchar_t* pKey;    // the config buffer's address, stable per process
        const wchar_t* pValue;  // what it resolved to
    };
    static Entry s_Cache[4];
    static int   s_Cached = 0;

    for (int i = 0; i < s_Cached; ++i)
    {
        if (s_Cache[i].pKey == pValue)
            return s_Cache[i].pValue;
    }

    const wchar_t* pResult = pValue;

    if (!pValue || !pValue[0])
    {
        pResult = pLiteralFallback;
    }
    else
    {
        // CSF labels are ASCII. A value with any other codepoint is literal
        // text and is returned before the engine sees it - no guess.
        char label[64];
        int i = 0;
        bool ascii = true;
        for (; pValue[i]; ++i)
        {
            if (pValue[i] > 0x7F || i >= static_cast<int>(sizeof(label)) - 1)
            {
                ascii = false;
                break;
            }
            label[i] = static_cast<char>(pValue[i]);
        }
        label[i] = '\0';

        if (!ascii)
            pResult = pValue;
        else if (const wchar_t* pCsf = StringTable::TryFetchString(label, nullptr))
            pResult = pCsf;
        else if (pDefaultLabel && lstrcmpiA(label, pDefaultLabel) == 0)
            pResult = pLiteralFallback;
    }

    if (s_Cached < static_cast<int>(sizeof(s_Cache) / sizeof(s_Cache[0])))
    {
        s_Cache[s_Cached].pKey   = pValue;
        s_Cache[s_Cached].pValue = pResult;
        ++s_Cached;
    }

    return pResult;
}

// The resolved WatchBar.DoneText / WatchBar.IdleText.
static const wchar_t* DoneText()
{
    return TextOrCsf(Cfg().DoneText, "TXT_READY", L"Done");
}

static const wchar_t* IdleText()
{
    return TextOrCsf(Cfg().IdleText, "TXT_WAITING", L"waiting for match...");
}

// Get-or-load one PCX by name.
//
// Not cached in a static anywhere here: PCX::Instance owns the surface and
// discards it when the scenario changes, so a cached pointer would dangle after
// a restart. Phobos resolves its sidebar art the same way.
static BSurface* LoadPCX(const char* name)
{
    if (!name || !name[0])
        return nullptr;

    if (BSurface* pLoaded = PCX::Instance.GetSurface(name))
        return pLoaded;

    PCX::Instance.LoadFile(name);
    return PCX::Instance.GetSurface(name);
}

// The side index of a house: HouseTypeClass::SideIndex is its slot in [Sides],
// which is the order SideClass::Array is built in. HouseClass::SideIndex is the
// same number cached on the instance and is the fallback for a house whose type
// is not resolved yet. -1 = no side at all (no local house, very early frame).
static int SideIndexOfHouse(HouseClass* pHouse)
{
    if (!pHouse || !pHouse->Type)
        return -1;

    int idx = pHouse->Type->SideIndex;
    if (idx < 0)
        idx = pHouse->SideIndex;
    return idx;
}

// The rules section name of a side index - "GDI", "Nod", ... - or nullptr when
// the index is out of range.
//
// SideClass::Array is the engine's own side table, in [Sides] order, so the name
// handed back is the one the loaded rules already use: the same table the
// engine's own sidebar art indexes into.
static const char* SideSectionName(int sideIdx)
{
    if (sideIdx < 0 || sideIdx >= SideClass::Array.Count)
        return nullptr;

    return SideClass::Array[sideIdx]->ID;
}

// The parts of a side's art set that the board draws, and the key each one is
// read from. The names are the ini-facing ones: CenterPCX is the cameo cell
// frame, On/OffPCX the toggle strip beside the board (On = board open, Off =
// collapsed), Up/DownPCX the two pager buttons along its bottom edge.
//
// These keys are the ONLY source for this art: nothing else is consulted, so
// the board neither depends on nor follows another mod's sidebar configuration.
enum SideArtPartId
{
    kSideArtCenter = 0,
    kSideArtOn,
    kSideArtOff,
    kSideArtUp,
    kSideArtDown,
    kSideArtPartCount
};

static const char* const kSideArtKeySuffix[kSideArtPartCount] = {
    "Center",
    "On",
    "Off",
    "Up",
    "Down",
};

// A per-side PCX, resolved from the loaded rules on every call:
//
//     [GDI]
//     WatchBar.CenterPCX=swside01center.pcx
//     WatchBar.OnPCX=swside01on.pcx
//     WatchBar.OffPCX=swside01off.pcx
//
// The file name lives in the section that owns the art, so a mod can rename or
// re-skin its frames without a rebuild and without a second place to keep in
// sync. Nothing is derived from an index: a side whose art is not numbered, or
// not named swside*, needs no special case.
//
// The section pointer is the engine's own SideClass ID, stable for the process
// lifetime - the contract ReadString's section cache assumes (see the note on
// ArtSectionOf).
static BSurface* SidePCX(int sideIdx, int partId)
{
    if (partId < 0 || partId >= kSideArtPartCount)
        return nullptr;

    const char* pSection = SideSectionName(sideIdx);
    if (!pSection)
        return nullptr;

    CCINIClass* pRules = CCINIClass::INI_Rules;
    if (!pRules)
        return nullptr;

    const char* const keySuffix = kSideArtKeySuffix[partId];

    char key[48];
    char name[128];

    wsprintfA(key, "WatchBar.%sPCX", keySuffix);
    if (pRules->ReadString(pSection, key, "", name, sizeof(name)) > 0)
        return LoadPCX(name);

    return nullptr;
}

// The side whose art the whole board wears: the spectator's own, not each row's.
//
// The frames are chrome, not information - the flag and the name already say
// who a row belongs to - so the whole board wears the local player's skin,
// exactly like their own sidebar does.
//
// CurrentPlayer is the right source rather than ScenarioClass::PlayerSideIndex:
// it is the same house whose side drives the game's own sidebar art (and the
// toggle strip), so the board and the rest of the UI can never disagree. In a
// normal match it is the player themselves; while spectating it is the observer
// house, which still carries the country the spectator picked in the lobby.
static int PanelSideIndex()
{
    return SideIndexOfHouse(HouseClass::CurrentPlayer);
}

// First-sighting registry for the flag log, same contract as the cameo one: one
// line per country per match, so a country whose flag art is unreachable is
// named instead of silently blank.
static const HouseTypeClass* g_SeenFlags[32];
static int g_SeenFlagCount = 0;

static void ResetSeenFlags()
{
    g_SeenFlagCount = 0;
}

static void LogFlagOutcome(const HouseTypeClass* pType, const char* declared, const BSurface* pFlag)
{
    for (int i = 0; i < g_SeenFlagCount; ++i)
    {
        if (g_SeenFlags[i] == pType)
            return;
    }
    if (g_SeenFlagCount >= static_cast<int>(sizeof(g_SeenFlags) / sizeof(g_SeenFlags[0])))
        return;   // registry full: stay silent rather than spam
    g_SeenFlags[g_SeenFlagCount++] = pType;

    const char* pName = declared[0] ? declared : "(none)";
    if (pFlag)
        LogLine("flag %s: File.Flag=\"%s\" loaded", pType->ID, pName);
    else
        LogLine("WARNING: flag %s: no art for File.Flag=\"%s\"", pType->ID, pName);
}

// The country flag, straight out of the country's own rules section:
//
//     [British]
//     File.Flag=C4_FLAG.PCX
//
// The declared spelling does NOT resolve on its own: the art inside the mod's
// MIX is lower-case (c4_flag.pcx) and the engine's file lookup is
// case-sensitive, so the value is lower-cased before it is used. That is a
// no-op for a name that is already correct. The mixed-case spelling is tried
// afterwards, for a MIX that really does carry it.
//
// NO NAME IS DERIVED. A country that declares no File.Flag (or whose art is
// missing) draws no flag at all - the row keeps its name. There is deliberately
// no fallback to a numbered name such as c<N>_flag.pcx: a guess that happens to
// resolve would silently show the WRONG country's flag, and the panel does not
// depend on a country's slot in [Countries] for anything (see docs/config.md).
//
// The 47x23 art carries a magenta background (255,0,255), which is exactly
// PCX's default transparent colour, so BlitToSurface drops it automatically - no
// masking needed.
static BSurface* FlagPCX(HouseClass* pHouse)
{
    if (!pHouse || !pHouse->Type)
        return nullptr;

    char declared[128];
    declared[0] = '\0';

    if (CCINIClass* pRules = CCINIClass::INI_Rules)
    {
        // A mod that dropped the File. prefix still gets its flag.
        if (pRules->ReadString(pHouse->Type->ID, "File.Flag", "", declared, sizeof(declared)) <= 0)
            pRules->ReadString(pHouse->Type->ID, "Flag", "", declared, sizeof(declared));
    }

    char lower[128];
    int n = 0;
    for (; declared[n] && n < static_cast<int>(sizeof(lower)) - 1; ++n)
    {
        const char c = declared[n];
        lower[n] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    lower[n] = '\0';

    BSurface* pFlag = lower[0] ? LoadPCX(lower) : nullptr;
    if (!pFlag && declared[0])
        pFlag = LoadPCX(declared);   // a MIX that really does carry the mixed case

    LogFlagOutcome(pHouse->Type, declared, pFlag);
    return pFlag;
}

// ------------------------------------------------------------------ cameos
//
// EC does not ship SHP cameos for most of its new units. It declares them with
// Ares' `CameoPCX=` key in artmd.ini instead:
//
//     [SAPOWR]
//     CameoPCX=icon_sapowr.pcx
//
// `CameoPCX` is an Ares extension, so the engine's own `ObjectTypeClass::GetCameo`
// knows nothing about it: for those types the SHP cameo field is never filled in,
// and the vanilla lookup hands back XXICON.SHP - the red "missing cameo" art.
// Without the CameoPCX path below, that error icon is what the board would draw
// for the PCX-only types (Socism / Viper / Mobius are almost entirely
// CameoPCX-based, as are many types of the other countries).
//
// Scanning every declared type: 195 art sections use CameoPCX, none of them also
// set Cameo=, so the two never have to be arbitrated - if a PCX name is present
// it is the cameo.
static constexpr int kArtSectionLen = 0x20;

// One stable heap copy per distinct name, so a pointer passed to the engine's
// INI reader keeps meaning the same section for the process lifetime. Distinct
// names get distinct pointers, which is exactly the contract the engine's
// section-pointer cache assumes (see the note above ArtSectionOf). The pool
// grows by ~a dozen bytes per unique section - a few hundred over a session.
static const char* InternSection(const char* pName)
{
    struct InternedName
    {
        InternedName* Next;
        char          Name[1];   // variable-length tail
    };
    static InternedName* s_pHead = nullptr;

    for (InternedName* p = s_pHead; p; p = p->Next)
        if (lstrcmpiA(p->Name, pName) == 0)
            return p->Name;

    const int cb = lstrlenA(pName) + 1;
    InternedName* pNew = static_cast<InternedName*>(
        HeapAlloc(GetProcessHeap(), 0, sizeof(InternedName) + cb));
    if (!pNew)
        return pName;   // allocation failed: the unstable pointer still reads correctly

    pNew->Next = s_pHead;
    lstrcpynA(pNew->Name, pName, cb);
    s_pHead = pNew;
    return pNew->Name;
}

// The art section a type reads from.
//
// Normally a type's art lives in a section named after the type, but rulesmd.ini
// can redirect it with `Image=`, and EC does so in 37 places that matter here
// (e.g. [SAPC3] Image=SAPC2, [MATECH] Image=MATECK, [BATTLEFORT]-style clones).
// Reading [<typeID>] alone would miss every one of those, so the redirect is
// resolved first.
//
// Engine cache hazard (INIClass::ReadString, 0x528A10): the engine caches the
// CALLER's pSection pointer at this+4 together with the matched section node at
// this+8, and a later call with the same POINTER skips the section search
// without comparing content. The engine's own callers pass section names that
// live inside the INI, so for them pointer equality implies same-section. A
// per-frame stack buffer sits at the same address every frame, so the first
// successful read would then be reused for every later section - reading one
// type's CameoPCX (icon_sapowr.pcx) for all of them.
//
// Hence: this function returns an INTERNED pointer (one stable heap copy per
// distinct name), restoring "same pointer == same section". pType->ID is already
// stable per type, so only the Image= redirect needs interning.
static const char* ArtSectionOf(TechnoTypeClass* pType)
{
    const char* pSection = pType->ID;

    // CCINIClass::INI_Rules is the pointer-typed reference (0x887048); the art,
    // AI and RA2MD instances are value references, hence the differing syntax.
    if (CCINIClass* pRules = CCINIClass::INI_Rules)
    {
        char image[kArtSectionLen] = { 0 };
        if (pRules->ReadString(pType->ID, "Image", "", image, sizeof(image)) > 0
            && image[0] != '\0')
        {
            pSection = InternSection(image);
        }
    }

    return pSection;
}

// The type's PCX cameo, or a null Surface when it uses a normal SHP cameo.
//
// Resolved fresh on every call for the same reason the sidebar PCXs are: the
// art INI and PCX::Instance are both rebuilt across a scenario change, so a
// cached surface would dangle.
//
// The outcome fields exist for the diagnostic log: they name the exact stage a
// failure happened at (no CameoPCX key / file load failed / loaded) instead of
// collapsing everything into "no icon drawn".
struct CameoResolution
{
    enum Kind
    {
        None,               // no cameo at all
        PCX,                // PCX cameo loaded and returned
        SHP,                // SHP cameo to draw
        SkippedPlaceholder, // SHP cameo is the XXICON stand-in: draw nothing
    };

    Kind       Kind;
    BSurface*  Surface;        // PCX: the loaded surface
    SHPStruct* Shp;            // SHP/SkippedPlaceholder: the cameo SHP
    char       PcxName[0x40];  // PCX: the file name ("" when no key)
    bool       LoadFailed;     // PCX: ForceLoadFile could not produce a surface
};

static bool IsMissingCameo(SHPStruct* pCameo);

static CameoResolution ResolveCameoFor(TechnoTypeClass* pType);

// The INI-derived part of a cameo, cached per type.
//
// The art data cannot change while the process runs, so the CameoPCX lookup is
// done once per type instead of once per frame per slot. Besides avoiding that
// performance, this keeps every ReadString on the interned section pointers
// (see ArtSectionOf) rather than re-reading through the engine's caller-pointer
// cache with fresh stack buffers.
struct CameoIniInfo
{
    const TechnoTypeClass* Type;
    bool                   HasPcxKey;
    char                   PcxName[0x40];  // lower-cased, "" when no key
};

static CameoIniInfo g_CameoIni[512];
static int          g_CameoIniCount = 0;
static CameoIniInfo g_CameoIniOverflow;   // reused slot once the table is full

// Clear the per-match table (called at the match boundary, see DrawPanel).
// The table is keyed by raw TechnoTypeClass*, and a new scenario rebuilds the
// type arrays - an entry left over from the previous match could alias a new
// type allocated at the same address and serve it the old type's cameo file,
// the same cross-match staleness the type-rule table guards against.
static void ResetCameoIni()
{
    g_CameoIniCount = 0;
}

static const CameoIniInfo* ReadCameoIni(TechnoTypeClass* pType)
{
    CameoIniInfo info;
    info.Type      = pType;
    info.HasPcxKey = false;
    info.PcxName[0] = '\0';

    const char* pSection = ArtSectionOf(pType);

    char name[0x40] = { 0 };
    if (CCINIClass::INI_Art.ReadString(pSection, "CameoPCX", "", name, sizeof(name)) > 0
        && name[0] != '\0')
    {
        info.HasPcxKey = true;
        // The game's own PCX lookups are done with lower-case names.
        CharLowerA(name);
        lstrcpynA(info.PcxName, name, sizeof(info.PcxName));
    }

    LogLine("cameo-init %s: section=%s pcxkey=%s%s",
            pType->ID, pSection,
            info.HasPcxKey ? info.PcxName : "(none)",
            info.HasPcxKey ? "" : " -> will use SHP cameo");

    CameoIniInfo* pSlot = nullptr;
    for (int i = 0; i < g_CameoIniCount; ++i)
    {
        if (g_CameoIni[i].Type == pType)
            return &g_CameoIni[i];   // raced in the same frame: keep the first
    }
    if (g_CameoIniCount < static_cast<int>(sizeof(g_CameoIni) / sizeof(g_CameoIni[0])))
    {
        pSlot = &g_CameoIni[g_CameoIniCount++];
    }
    else
    {
        // Table full (theoretically impossible: fewer buildable types than
        // slots). Reuse one slot - results stay correct, the type just re-reads.
        static bool s_Warned = false;
        if (!s_Warned)
        {
            LogLine("WARNING: cameo ini table full, re-reading per frame from now on");
            s_Warned = true;
        }
        pSlot = &g_CameoIniOverflow;
    }

    *pSlot = info;
    return pSlot;
}

static CameoResolution ResolveCameoFor(TechnoTypeClass* pType)
{
    CameoResolution r;
    r.Kind = CameoResolution::None;
    r.Surface = nullptr;
    r.Shp = nullptr;
    r.PcxName[0] = '\0';
    r.LoadFailed = false;

    if (!pType)
        return r;

    const CameoIniInfo* pInfo = nullptr;
    for (int i = 0; i < g_CameoIniCount; ++i)
    {
        if (g_CameoIni[i].Type == pType)
        {
            pInfo = &g_CameoIni[i];
            break;
        }
    }
    if (!pInfo)
        pInfo = ReadCameoIni(pType);

    if (pInfo->HasPcxKey)
    {
        lstrcpynA(r.PcxName, pInfo->PcxName, sizeof(r.PcxName));

        // PCX::Instance is content-addressed, so calling it with this stable
        // per-type buffer is safe; it re-fetches after a scenario change too.
        if (BSurface* pLoaded = PCX::Instance.GetSurface(r.PcxName))
        {
            r.Kind = CameoResolution::PCX;
            r.Surface = pLoaded;
            return r;
        }

        r.LoadFailed = !PCX::Instance.LoadFile(r.PcxName);
        r.Surface = r.LoadFailed ? nullptr : PCX::Instance.GetSurface(r.PcxName);
        if (r.Surface)
        {
            r.Kind = CameoResolution::PCX;
            return r;
        }

        r.LoadFailed = true;
        // fall through to the SHP fallback, exactly like the old code path did
        // when CameoPCXFor returned null.
    }

    // No PCX key (or the PCX would not load): use the engine's SHP cameo,
    // skipping the XXICON stand-in. LoadFailed stays set so the log can show
    // both the failed PCX and whatever the SHP fallback decided. The SHP
    // pointer is fetched fresh: it is engine-owned and must not be cached.
    if (SHPStruct* pCameo = pType->GetCameo())
    {
        r.Shp = pCameo;
        r.Kind = IsMissingCameo(pCameo)
            ? CameoResolution::SkippedPlaceholder
            : CameoResolution::SHP;
    }

    return r;
}

// First-sighting registry: each type's outcome is logged exactly once, so the
// log stays readable while the panel redraws every frame.
static const TechnoTypeClass* g_SeenCameos[512];
static int g_SeenCameoCount = 0;

static void ResetSeenCameos()
{
    g_SeenCameoCount = 0;
}

static void LogCameoOutcome(const TechnoTypeClass* pType, const CameoResolution& r)
{
    for (int i = 0; i < g_SeenCameoCount; ++i)
    {
        if (g_SeenCameos[i] == pType)
            return;
    }
    if (g_SeenCameoCount < static_cast<int>(sizeof(g_SeenCameos) / sizeof(g_SeenCameos[0])))
        g_SeenCameos[g_SeenCameoCount++] = pType;
    else
        return;   // registry full: stay silent rather than spam

    // BSurface layout (verified in the ForceLoadFile disassembly): +4 = width,
    // +8 = height. A zero-size or null-buffer surface would paint black.
    const int w = r.Surface ? *reinterpret_cast<const int*>(reinterpret_cast<const char*>(r.Surface) + 4) : -1;
    const int h = r.Surface ? *reinterpret_cast<const int*>(reinterpret_cast<const char*>(r.Surface) + 8) : -1;

    if (r.LoadFailed)
    {
        LogLine("cameo %s: PCX LOAD FAILED: %s (surface=%p %dx%d)",
                pType->ID, r.PcxName, r.Surface, w, h);
        // fall through: the SHP outcome (if any) is still reported
    }

    switch (r.Kind)
    {
    case CameoResolution::PCX:
        LogLine("cameo %s: pcx=%s ok surface=%p %dx%d",
                pType->ID, r.PcxName, r.Surface, w, h);
        break;
    case CameoResolution::SHP:
        LogLine("cameo %s: shp drawn%s", pType->ID, r.LoadFailed ? " (after PCX failure)" : "");
        break;
    case CameoResolution::SkippedPlaceholder:
        LogLine("cameo %s: shp=XXICON placeholder, not drawable", pType->ID);
        break;
    case CameoResolution::None:
        if (!r.LoadFailed)
            LogLine("cameo %s: NO CAMEO (no CameoPCX key, no SHP)", pType->ID);
        break;
    }
}

// Does this type carry an icon the board can actually draw? Types without one
// are skipped by the collector entirely: an icon-less type would otherwise get a
// full cell - frame, empty recess and a bare percentage or count badge - which
// reads as a rendering glitch, not as information. (An example is the magnetic
// assembler NAFIST, whose art section declares neither Cameo= nor CameoPCX=.)
//
// The verdict is the painter's own resolution (ResolveCameoFor), so the
// collector and the painter can never disagree about what is drawable:
//   PCX                      - a CameoPCX= file that loads;
//   SHP                      - a real (non-XXICON) Cameo= SHP;
//   None / SkippedPlaceholder - nothing to draw -> the type is hidden.
// A PCX that fails to load still falls through to the SHP cameo inside
// ResolveCameoFor, so a broken PCX alone does not hide a type that has a
// usable SHP.
//
// Cached per type, match-scoped like the other cameo tables (cleared at the
// match boundary - see DrawPanel): the art INI cannot change mid-match and a
// missing PCX file stays missing, so the verdict is stable for the match.
// Caching also keeps the collector from re-attempting a PCX file load for a
// cameo-less type once per unit per frame.
struct CameoVerdict
{
    const TechnoTypeClass* Type;
    bool Drawable;
};

static CameoVerdict g_CameoVerdict[512];
static int          g_CameoVerdictCount = 0;

static void ResetCameoVerdicts()
{
    g_CameoVerdictCount = 0;
}

static bool HasDrawableCameo(TechnoTypeClass* pType)
{
    if (!pType)
        return false;

    for (int i = 0; i < g_CameoVerdictCount; ++i)
    {
        if (g_CameoVerdict[i].Type == pType)
            return g_CameoVerdict[i].Drawable;
    }

    // First sight of this type in this match: resolve exactly the way the
    // painter will, and let the standard cameo log record the outcome once
    // (the seen registry dedupes it).
    const CameoResolution cameos = ResolveCameoFor(pType);
    LogCameoOutcome(pType, cameos);

    const bool drawable = cameos.Kind == CameoResolution::PCX
                       || cameos.Kind == CameoResolution::SHP;

    if (!drawable)
    {
        LogLine("board-skip %s: no drawable cameo, type is hidden from the board",
                pType->ID);
    }

    if (g_CameoVerdictCount < static_cast<int>(sizeof(g_CameoVerdict) / sizeof(g_CameoVerdict[0])))
    {
        g_CameoVerdict[g_CameoVerdictCount].Type     = pType;
        g_CameoVerdict[g_CameoVerdictCount].Drawable = drawable;
        ++g_CameoVerdictCount;
    }
    // Table full (theoretical): later types stay uncached and re-resolve per
    // frame - correct, just repeated work, same trade as the g_CameoIni
    // overflow slot.

    return drawable;
}

// Is this SHP the engine's stand-in for "this type has no cameo"?
//
// Checked by filename against XXICON.SHP rather than by pointer, because the
// placeholder is a shared reference every cameo-less type points at. Drawing it
// is what produced the red error icon; a type that reaches this point has no
// real cameo to show, and an empty recess reads better than a wall of X marks.
static bool IsMissingCameo(SHPStruct* pCameo)
{
    if (!pCameo)
        return true;

    if (const SHPReference* pRef = pCameo->AsReference())
    {
        if (pRef->Filename && !_stricmp(pRef->Filename, GameStrings::XXICON_SHP))
            return true;
    }

    return false;
}

// Draw one cameo's art at the given pixel position - nothing else. The readout
// (progress or count) is drawn on top by the caller, then WashCameo blends the
// whole thing toward the recess; an empty recess beats a red XXICON wall for a
// type that has no cameo at all.
static void DrawCameoArt(DSurface* pSurface, int cx, int cy, TechnoTypeClass* pType)
{
    const CameoResolution cameos = ResolveCameoFor(pType);
    LogCameoOutcome(pType, cameos);

    if (cameos.Kind == CameoResolution::PCX)
    {
        RectangleStruct cameo { cx, cy, CAMEO_W, CAMEO_H };
        PCX::Instance.BlitToSurface(&cameo, pSurface, cameos.Surface);
    }
    else if (cameos.Kind == CameoResolution::SHP)
    {
        RectangleStruct bounds = pSurface->GetRect();
        Point2D loc { cx, cy };
        pSurface->DrawSHP(FileSystem::CAMEO_PAL, cameos.Shp, 0, &loc, &bounds,
            BlitterFlags::bf_400, 0, 0, ZGradient::Ground, 1000, 0,
            nullptr, 0, 0, 0);
    }
}

// The colour a dissolving cameo washes toward.
//
// Sampled from the frame PCX at the centre of the cameo recess, so an icon
// disappears into the frame's own recess rather than an invented shade. One
// sample per frame serves every row, because the whole board shares a single
// frame set (see PanelSideIndex). BSurface::GetPixel returns the raw 16-bit
// pixel, which is the same packing Drawing::Int_To_RGB unpacks (0x7BAE60).
static ColorStruct RecessColor(BSurface* pFrame)
{
    ColorStruct color { 0, 0, 0 };
    if (!pFrame)
        return color;

    Point2D probe { CAMEO_X + CAMEO_W / 2, CAMEO_Y + CAMEO_H / 2 };
    Drawing::Int_To_RGB(static_cast<int>(pFrame->GetPixel(&probe)), color);
    return color;
}

// Blend the frame's recess colour back over a cameo rect at (100-alphaPct)%.
//
// Call this AFTER the art and its readout, so the whole layer dissolves
// together, count chip included. Wash last is deliberate: washing before the
// readout would leave the chip and digits at full opacity and make them pop off
// abruptly instead of fading with the icon. FillRectTrans (0x4BB830) is
// per-channel src-over and takes a 0..100 percentage.
static void WashCameo(DSurface* pSurface, int cx, int cy, int alphaPct,
                      const ColorStruct& recess)
{
    if (alphaPct >= 100)
        return;

    RectangleStruct rect { cx, cy, CAMEO_W, CAMEO_H };
    ColorStruct wash = recess;
    pSurface->FillRectTrans(&rect, &wash, 100 - alphaPct);
}

// One readiness test shared by the clock draw and the progress readout, so
// the two can never disagree about whether the sweep is on screen.
static bool ClockReady()
{
    // WatchBar.ClockEnabled=0 forces the numeric readout even when the SHP is
    // present - for a mod whose gclock2 art does not read at 60x48, or whose
    // players prefer the plain digits.
    if (!Cfg().ClockEnabled)
        return false;

    const SHPStruct* pClock = FileSystem::GCLOCK2_SHP;
    return pClock && pClock->Frames > 0;
}

// The production clock (gclock2) over a structure cameo.
//
// Replicates the engine's own sidebar clock draw verbatim - disassembly at
// 0x6A9E4A: frame = production step + 1, palette FileSystem::SIDEBAR_PAL,
// flags bf_400 | TransLucent50 (0x404), which is also what Phobos'
// SWButtonClass uses for its recharge clock.
//
// FileSystem::GCLOCK2_SHP is the pointer the game itself maintains for the
// CURRENT PLAYER's sidebar. The engine resolves GCLOCK2.SHP through the
// per-side mix chain (SIDEC%02d.MIX / SIDEC%02dMD.MIX, keyed on the local
// player's side, number = SideIndex + 1), so the clock drawn here is the one
// the spectator's own sidebar uses. EC ships no sidec mixes and the global
// gclock2.shp (expandmd) is used; a mod that later ships per-faction
// SIDEC0NMD.MIX files is followed automatically.
static void DrawClock(DSurface* pSurface, int cx, int cy, int step)
{
    const SHPStruct* pClock = FileSystem::GCLOCK2_SHP;
    if (!ClockReady())
        return;

    int frame = step + 1;
    if (frame > pClock->Frames - 1)
        frame = pClock->Frames - 1;   // defensive: never index past the table

    RectangleStruct bounds = pSurface->GetRect();
    Point2D loc { cx, cy };
    pSurface->DrawSHP(FileSystem::SIDEBAR_PAL, FileSystem::GCLOCK2_SHP, frame,
        &loc, &bounds, BlitterFlags::bf_400 | BlitterFlags::TransLucent50,
        0, 0, ZGradient::Ground, 1000, 0, nullptr, 0, 0, 0);
}

// Readout for a structure cameo.
//
// The gclock2 sweep is the progress display, so no percentage is drawn on top of
// it (the two would report the same number). What remains as text:
//
//   done -> the WatchBar.DoneText string in green, centred: a full clock still
//           reads as "building" to anyone who did not watch the sweep complete
//   queue -> a small "+N" in the bottom-right corner
//
// Fallback: with no clock SHP (gclock2.shp missing from the mixes) a bare
// progress number is drawn instead, so the icon never goes mute about its
// progress.
static void DrawProgress(DSurface* pSurface, int x, int y, const RowIcon& icon)
{
    wchar_t buf[64];
    bool centre = false;

    // Shared with the "+N" suffix below, which anchors on the small font's
    // height whether or not a centre text is drawn this frame.
    const int th = TextHeight(kFontSmall);

    if (icon.Done)
    {
        // WatchBar.DoneText, a CSF label by default (TXT_READY = "就绪").
        lstrcpynW(buf, DoneText(), 64);
        centre = true;
    }
    else if (!ClockReady())
    {
        // Bare number: the '%' glyph never survives the font routing in
        // practice, so the fallback does not pretend otherwise.
        wsprintfW(buf, L"%d", icon.Pct);
        centre = true;
    }

    if (centre)
    {
        // The default is pure ASCII, so it renders with the Latin font; a CJK
        // value in WatchBar.DoneText/IdleText is routed to the engine's CJK font automatically.
        const COLORREF col = icon.Done
            ? CfgColor(Cfg().DoneColor)
            : CfgColor(Cfg().ProgressTextColor);

        const int tw = TextWidth(buf, kFontSmall);
        const int tx = x + (CAMEO_W - tw) / 2;
        const int ty = y + (CAMEO_H - th) / 2;

        DrawString(pSurface, buf, tx, ty, col, kFontSmall);
    }

    // Queued count stays out of the centre so it never collides with the
    // clock sweep, and hugs the corner where it reads as a suffix.
    if (icon.Count > 0)
    {
        wsprintfW(buf, L"+%d", icon.Count);
        const int qw = TextWidth(buf, kFontTiny);
        DrawString(pSurface, buf, x + CAMEO_W - qw - 2, y + CAMEO_H - th - 1,
                   CfgColor(Cfg().QueueTextColor), kFontTiny);
    }
}

// Alive-count badge for a unit cameo, in the top-right corner - the same
// convention the reference mock-up uses. Sits on a small grey-black chip so
// it stays legible over any cameo art. Point8 (the game's standard UI font)
// rather than the smaller progress font: the count is the whole point of a
// unit icon and must read at a glance.
static void DrawCount(DSurface* pSurface, int x, int y, int count)
{
    wchar_t buf[16];
    wsprintfW(buf, L"%d", count);

    const int tw = TextWidth(buf, kFont);
    const int th = TextHeight(kFont);

    // The chip covers the glyphs plus their full shadow (+1px right/down) and
    // ends exactly at the cameo's right edge.
    RectangleStruct chip { x + CAMEO_W - tw - 7, y, tw + 7, th + 2 };
    pSurface->FillRect(&chip, CfgColor(Cfg().CountChipColor));

    DrawString(pSurface, buf, x + CAMEO_W - tw - 5, y + 1,
               CfgColor(Cfg().CountTextColor), kFont);
}

// ------------------------------------------------------------------ metrics
//
// Panel dimensions, shared by the draw path and the toggle's placement maths so
// the two can never disagree about where the board ends.
struct PanelMetrics
{
    int Rows;
    int UsedCols;   // icon columns the widest row line actually occupies
    int Height;
    int Lines;      // total icon lines, the scroll unit
};

// Lines of icons one row needs. A player with nothing to show still reserves
// one line, so their flag and name have somewhere to sit.
static int RowLines(const PlayerRow& r)
{
    const int lines = (r.IconCount + ICONS_PER_LINE - 1) / ICONS_PER_LINE;
    return lines > 0 ? lines : 1;
}

// The one and only place the board's size is derived, so the draw path and the
// toggle's placement maths cannot drift apart.
static PanelMetrics MetricsFor(const PlayerRow* rows, int rowCount)
{
    PanelMetrics m;
    m.Rows = rowCount;

    // How far right the board's content actually reaches, in icon columns: the
    // widest FIRST line any row has (a later line is only reached when the
    // first one is full, so min(IconCount, ICONS_PER_LINE) is exactly it). The
    // toggle strip hugs this edge instead of the full 4-column grid, so a
    // sparse board does not have the control floating alongside empty grid.
    m.UsedCols = 0;
    m.Lines    = 0;

    int h = 0;
    for (int i = 0; i < rowCount; ++i)
    {
        const int firstLine =
            rows[i].IconCount < ICONS_PER_LINE ? rows[i].IconCount : ICONS_PER_LINE;
        if (firstLine > m.UsedCols)
            m.UsedCols = firstLine;

        const int lines = RowLines(rows[i]);
        m.Lines += lines;
        h += lines * (CELL_H + ROW_GAP);
    }

    // No title bar: the board is pure content. An empty board still keeps one
    // row of height so the frame does not collapse to nothing.
    m.Height = rowCount > 0 ? h : CELL_H;
    return m;
}

// The metrics DrawPanel computed on its last run. The toggle strip reads this
// instead of re-running the whole production + unit scan every frame - the
// strip's placement can therefore trail the art by one frame, which is
// invisible in practice: the board's size only changes when a unit type
// appears or vanishes, a rare event, and only for that one frame.
static PanelMetrics g_LastMetrics { 0, 0, CELL_H, 0 };

// X of the toggle strip: glued to the right edge of the board's OCCUPIED
// width, not the full grid, so with at most 2 icons it hugs the second icon.
// A full first line lands on exactly the old grid-edge position. Mirrors
// ToggleSWButtonClass::UpdatePosition().
static int ToggleX(const PanelMetrics& m)
{
    return PANEL_X + LABEL_W + m.UsedCols * CELL_W + 2;
}

static int ToggleY(const PanelMetrics& m)
{
    // Centre on the VISIBLE band, not the full content height: with lines
    // clamped away below the fold, a strip centred on the full height would sit
    // beside content that is never drawn.
    const int top   = PanelY();
    const int bandH = BandBottom() - top;
    const int h     = m.Height < bandH ? m.Height : bandH;
    return top + (h - TOGGLE_H) / 2;
}

// --------------------------------------------------------------- animation
//
// The dynamic list animates instead of snapping:
//
//   fade in  - a type enters the list (new unit type appears / building starts)
//   fade out - a type leaves the list (last one dies / building placed or
//              cancelled); its ghost keeps drawing at its old slot while it
//              dissolves, so the list empties smoothly
//   slide    - icons whose slot moved (a neighbour arrived or left) glide to
//              their new slot; the mover always arrives before the ghost
//              finishes dissolving (MOVE_MS < FADE_MS), so a slot never
//              blinks empty in between
//
// All of it uses engine primitives that are already relied on elsewhere
// (FillRectTrans for the dissolve) plus coordinates computed here - no
// additional engine dependencies.
//
// State is keyed on (HouseClass*, TechnoTypeClass*), which is the identity a
// row icon actually has: row indexes shift whenever a player joins or leaves,
// and unit types do not.

// FADE_MS / MOVE_MS come from WatchBar.FadeMs / WatchBar.MoveMs - see the parameter
// block at the top of this file. The invariant "MOVE_MS < FADE_MS" is enforced
// by the config parser, which reports a violation in the log.
//
// Either duration may be 0, which means "that animation is off": a 0 ms glide
// snaps, a 0 ms dissolve is instant. Both helpers below special-case it - the
// raw expressions divide by the duration, and a zero divisor is not harmless
// here. Float 0/0 is NaN (NaN fails the "> 1.0f" clamp, so it reaches the
// blitter as a NaN rectangle), and the integer fade divide would raise a
// divide-by-zero exception in the game process.

// Eased 0..1 progress of a glide that started at "since". 0 ms = already there.
static float GlideProgress(DWORD since, DWORD now)
{
    float t = MOVE_MS ? (now - since) / static_cast<float>(MOVE_MS) : 1.0f;
    if (t > 1.0f)
        t = 1.0f;
    return t * (2.0f - t);   // ease-out quad
}

// Fade alpha 0..100 for a dissolve that started at "since". 0 ms = fully faded in.
static int FadeAlpha(DWORD since, DWORD now)
{
    if (FADE_MS == 0)
        return 100;

    const int alpha = static_cast<int>((now - since) * 100 / FADE_MS);
    return alpha > 100 ? 100 : alpha;
}

struct IconAnim
{
    bool             Used;
    HouseClass*      House;
    TechnoTypeClass* Type;
    int              LastSeen;      // frame stamp, drives LRU eviction

    bool             Present;       // in the current frame's list
    DWORD            FirstSeen;     // drives the fade-in
    DWORD            GoneSince;     // drives the fade-out
    bool             HasGhost;      // dissolving at its old slot
    RowIcon          Ghost;         // last snapshot, so the ghost's readout lives
    RowIcon          Last;          // last snapshot while present

    float            PosLine;       // current animated slot (in cells)
    float            PosCol;
    float            FromLine;      // start of the current glide
    float            FromCol;
    int              TgtLine;       // slot it is heading for
    int              TgtCol;
    DWORD            MoveSince;
};

// Live + ghost records together; one player's line budget (at most
// kHardMaxCells) is the cap the
// list itself enforces, so twice the HARD cap is a generous ceiling. The table
// is sized for the compile-time maximum, not the ini value, so raising the ini
// value can never overrun it.
static constexpr int ANIM_MAX = kHardMaxRows * kHardMaxCells * 2;
static IconAnim g_Anim[ANIM_MAX];
static int g_AnimFrame = 0;

// The record for (house, type), created on first sight. Eviction picks the
// least recently seen record - in practice never fires, a match holds far
// fewer distinct (house, type) pairs than the table has slots.
static IconAnim* AnimFor(HouseClass* pHouse, TechnoTypeClass* pType)
{
    IconAnim* pFree = nullptr;
    IconAnim* pStale = nullptr;
    int staleSeen = 0;

    for (auto& r : g_Anim)
    {
        if (r.Used && r.House == pHouse && r.Type == pType)
            return &r;
        if (!r.Used && !pFree)
            pFree = &r;
        if (r.Used && (!pStale || r.LastSeen < staleSeen))
        {
            pStale = &r;
            staleSeen = r.LastSeen;
        }
    }

    IconAnim* pSlot = pFree ? pFree : pStale;
    *pSlot = IconAnim {};
    pSlot->Used  = true;
    pSlot->House = pHouse;
    pSlot->Type  = pType;
    return pSlot;
}

// Drop every animation. Called at the match boundary: the next match must
// start clean, not inherit a dissolve from the last one.
static void AnimReset()
{
    for (auto& r : g_Anim)
        r = IconAnim {};
}

// Advance the animation state to match this frame's list. Two passes:
// first mark every listed icon present (creating records for arrivals),
// then advance movers and turn every unmarked record into a ghost.
static void AnimUpdate(PlayerRow* rows, int rowCount, DWORD now)
{
    ++g_AnimFrame;

    for (int i = 0; i < rowCount; ++i)
    {
        for (int k = 0; k < rows[i].IconCount; ++k)
        {
            const RowIcon& icon = rows[i].Icons[k];
            const int line = k / ICONS_PER_LINE;
            const int col  = k % ICONS_PER_LINE;

            IconAnim& r = *AnimFor(rows[i].House, icon.Type);
            r.LastSeen = g_AnimFrame;

            if (!r.Present)
            {
                // Arrival. If a fade-out was still running (the same type came
                // back mid-dissolve), resume from the alpha it had reached
                // instead of snapping to invisible.
                if (r.HasGhost && now - r.GoneSince < FADE_MS)
                {
                    const int alpha = 100 - FadeAlpha(r.GoneSince, now);
                    r.FirstSeen = now - static_cast<DWORD>(alpha) * FADE_MS / 100;
                }
                else
                {
                    r.FirstSeen = now;
                }
                r.HasGhost   = false;
                r.Present    = true;
                r.PosLine    = static_cast<float>(line);
                r.PosCol     = static_cast<float>(col);
                r.FromLine   = r.PosLine;
                r.FromCol    = r.PosCol;
                r.TgtLine    = line;
                r.TgtCol     = col;
                r.MoveSince  = now;
            }
            else
            {
                // Re-flow: start a glide from the current animated position,
                // so back-to-back changes stay continuous instead of jumping.
                if (r.TgtLine != line || r.TgtCol != col)
                {
                    r.FromLine  = r.PosLine;
                    r.FromCol   = r.PosCol;
                    r.TgtLine   = line;
                    r.TgtCol    = col;
                    r.MoveSince = now;
                }
            }

            r.Last = icon;
        }
    }

    for (auto& r : g_Anim)
    {
        if (!r.Used)
            continue;

        if (r.Present)
        {
            if (r.LastSeen != g_AnimFrame)
            {
                // not in this frame's list: start dissolving in place
                r.Present   = false;
                r.HasGhost  = true;
                r.GoneSince = now;
                r.Ghost     = r.Last;
                continue;
            }

            const float t = GlideProgress(r.MoveSince, now);
            r.PosLine = r.FromLine + (r.TgtLine - r.FromLine) * t;
            r.PosCol  = r.FromCol  + (r.TgtCol  - r.FromCol)  * t;
        }
        else if (r.HasGhost && now - r.GoneSince > FADE_MS)
        {
            r.Used = false;   // dissolve finished, retire the record
        }
    }
}

// ------------------------------------------------------------------ scroll
//
// When the board no longer fits the visible band it stops adding lines (the fit
// tests in DrawPanel skip every line that would stick out) and the overflow is
// reached by scrolling whole lines.
//
// The scroll is driven by the arrow pair under the board's bottom edge, which
// uses the game's own gadget input path - the same clicks the toggle strip
// answers.
// The mouse wheel is not usable for this: the engine's DirectInput mouse path
// never surfaces the wheel to this process, neither as WM_MOUSEWHEEL in the
// game's message queue (a WH_GETMESSAGE hook observes no wheel message) nor as
// raw input (an INPUTSINK registration receives no event).
//
// Scroll state, shared with DrawPanel:
static int g_ScrollLines    = 0;   // whole lines scrolled past the band top
static int g_MaxScrollLines = 0;   // hidden lines under the band's bottom
static int g_VisLines       = 0;   // whole lines the band currently shows
                                   // (what WatchBar.ScrollStep=page moves by)

static void DrawPanel()
{
    auto pSurface = DSurface::Composite;
    if (!pSurface)
        return;

    // ---- diagnostic: match boundaries, row identity, icon-set changes
    static int s_Frame = 0;
    static int s_PrevRows = -1;
    static TechnoTypeClass* s_PrevIcons[kHardMaxRows][kHardMaxCells] = {};
    static int s_PrevIconCount[kHardMaxRows] = {};
    ++s_Frame;

    // The row scan walks every factory and every unit of every house. That is
    // inexpensive in a normal match, but WatchBar.ScanIntervalMs lets a big mod
    // with thousands of units rescan a few times a second instead of every
    // frame. The animation and drawing paths still run per frame, so the board
    // keeps its motion - only the data refreshes more slowly. 0 = every frame,
    // i.e. the original behaviour.
    static PlayerRow s_Rows[kHardMaxRows];
    static int    s_RowCount = 0;
    static DWORD  s_LastScan = 0;
    static bool   s_Scanned  = false;

    // The band the board may draw in, and how many whole icon lines it holds.
    //
    // Computed BEFORE the scan on purpose: the per-player line budget comes
    // from it (ApplyLineBudgets), and it depends only on the view and on the
    // parameters - never on the rows. That is what keeps the budget from being
    // circular, and it is why the split can be decided before anything has been
    // collected.
    const int top      = PanelY();
    const int bottom   = BandBottom();
    const int visLines = VisLinesNow();
    g_VisLines = visLines;

    const DWORD scanNow = GetTickCount();
    const int   scanMs  = Cfg().ScanIntervalMs;
    if (!s_Scanned || scanMs <= 0 || static_cast<int>(scanNow - s_LastScan) >= scanMs)
    {
        s_RowCount = CollectRows(s_Rows, MAX_ROWS);
        s_LastScan = scanNow;
        s_Scanned  = true;
    }

    PlayerRow* rows = s_Rows;
    const int rowCount = s_RowCount;

    // How many lines each row may show this frame (WatchBar.LinesMode). Applied
    // every frame, not once per scan: a resolution change has to land now, and
    // the split is a few dozen integer operations over rows that are already
    // collected.
    ApplyLineBudgets(rows, rowCount, visLines);

    if (rowCount != s_PrevRows)
    {
        // Forget every row's last icon set on a row-count change: rows are
        // identified by index, so a list that grew or shrank would otherwise
        // misattribute old sets to new rows and skip their first log line.
        // This is also the match boundary (rows go to zero between matches).
        for (int i = 0; i < kHardMaxRows; ++i)
        {
            for (int k = 0; k < kHardMaxCells; ++k)
                s_PrevIcons[i][k] = nullptr;
            s_PrevIconCount[i] = 0;
        }
        AnimReset();

        // Match-scoped type rules (CountAs / IgnoreCount): the type arrays are
        // rebuilt per scenario, so cached resolutions from the previous match
        // must not survive. The prefill also emits the per-match self-check
        // line into the log.
        TypeRulesReset();
        TypeRulesPrefill();

        // Same contract for the cameo tables, both keyed by raw
        // TechnoTypeClass*: ResetCameoVerdicts clears the icon-less skip filter,
        // ResetCameoIni clears the INI cache (an old entry could alias a new
        // type at the same address and serve it the old type's cameo).
        ResetCameoIni();
        ResetCameoVerdicts();

        // The scroll position is match-scoped too. A reflowed board (player
        // arrived/left, new match) starts from the top; a board that merely
        // reflowed its icons is handled by the per-frame clamp.
        if (g_ScrollLines != 0)
        {
            LogLine("scroll: reset (was %d)", g_ScrollLines);
            g_ScrollLines = 0;
        }

        if (rowCount == 0)
        {
            LogLine("---- match end / panel empty (frame %d) ----", s_Frame);
        }
        else
        {
            // panelside is the rules section the board's own frames are read
            // from (the side's own [<Side>] section - "GDI", "Nod", ...), and
            // me= names the house it was taken from (the local player or
            // observer).
            const auto pMe = HouseClass::CurrentPlayer;
            const char* pPanelSide = SideSectionName(PanelSideIndex());
            LogLine("---- rows: %d (was %d, frame %d) panelside=%s me=%s ----",
                    rowCount, s_PrevRows, s_Frame,
                    pPanelSide ? pPanelSide : "(none)",
                    pMe ? pMe->Type->ID : "(none)");
            for (int i = 0; i < rowCount; ++i)
            {
                const char* pRowSide = SideSectionName(SideIndexOfHouse(rows[i].House));
                LogLine("row %d: name=\"%s\" country=%s spawn=%d rowside=%s",
                    i, rows[i].Player,
                    rows[i].House->Type->ID,
                    rows[i].House->GetSpawnPosition(),
                    pRowSide ? pRowSide : "(none)");
            }
            ResetSeenCameos();
            ResetSeenFlags();
        }
        s_PrevRows = rowCount;
    }

    // Log only when a row's icon SET changes - types entering or leaving the
    // row. Counts and percentages tick every frame and would bury the log.
    for (int i = 0; i < rowCount; ++i)
    {
        bool changed = rows[i].IconCount != s_PrevIconCount[i];
        for (int k = 0; k < rows[i].IconCount && !changed; ++k)
            changed = rows[i].Icons[k].Type != s_PrevIcons[i][k];
        if (!changed)
            continue;

        // A row that just lost its last icon has nothing to print, and the loop
        // below would leave sig uninitialised - say so instead of logging stack
        // garbage.
        char sig[512];
        int len = 0;
        for (int k = 0; k < rows[i].IconCount && len < 400; ++k)
        {
            const RowIcon& ic = rows[i].Icons[k];
            if (ic.Building)
            {
                len += ic.Done
                    ? wsprintfA(sig + len, "%s%s@done", k ? " | " : "", ic.Type->ID)
                    : wsprintfA(sig + len, "%s%s@%d%%", k ? " | " : "", ic.Type->ID, ic.Pct);
            }
            else
            {
                len += wsprintfA(sig + len, "%s%s x%d", k ? " | " : "", ic.Type->ID, ic.Count);
            }
        }

        if (len == 0)
            lstrcpynA(sig, "(empty)", sizeof(sig));

        LogLine("icons row%d(\"%s\"): %s", i, rows[i].Player, sig);
        if (rows[i].HiddenIcons > 0)
        {
            LogLine("icons row%d: %d more type(s) than the row holds (%d cells in "
                    "%d line(s), per-group caps applied)", i, rows[i].HiddenIcons,
                    rows[i].LineBudget * ICONS_PER_LINE, rows[i].LineBudget);
        }

        for (int k = 0; k < kHardMaxCells; ++k)
            s_PrevIcons[i][k] = k < rows[i].IconCount ? rows[i].Icons[k].Type : nullptr;
        s_PrevIconCount[i] = rows[i].IconCount;
    }

    if ((s_Frame % 2000) == 0)
        LogLine("heartbeat frame %d rows=%d", s_Frame, rowCount);

    // Geometry from the rows already collected, rather than re-measuring: that
    // would walk the houses a second time and could in principle disagree
    // with this list if a house appeared between the two passes. The rows are
    // already trimmed to their line budget, so this is the height that will
    // actually be drawn.
    const PanelMetrics m = MetricsFor(rows, rowCount);
    g_LastMetrics = m;

    // Clamp the board to the visible band. A line that does not fit is not drawn
    // at all (no partial cells: DSurface blits only clip to the surface edge, so
    // an overhanging cell would smear onto the map); the arrow buttons scroll
    // whole lines to reach the hidden ones. The clamp also absorbs shrinking
    // rows - scrolling past the content is never meaningful.
    //
    // In the adaptive modes this is usually a no-op: "compact" hands out at most
    // visLines lines in total, so m.Lines cannot exceed it - unless the band is
    // shorter than the player list, where every row keeps its one label line and
    // the board scrolls as it always did.
    g_MaxScrollLines   = (visLines > 0 && m.Lines > visLines)
                       ? m.Lines - visLines : 0;
    if (g_ScrollLines > g_MaxScrollLines)
        g_ScrollLines = g_MaxScrollLines;

    // Log the scrollable-state flips, not every scroll step: one line per board
    // phase is diagnosable, one per step is noise.
    static int s_WasMaxScroll = -1;
    if (g_MaxScrollLines != s_WasMaxScroll)
    {
        s_WasMaxScroll = g_MaxScrollLines;
        LogLine("scroll: %s (lines=%d vis=%d hidden=%d)",
                g_MaxScrollLines > 0 ? "overflow" : "fits",
                m.Lines, visLines, g_MaxScrollLines);
    }

    // No backdrop, no border: the frame cells are self-contained art and every
    // text carries a full shadow, so the board sits directly on the terrain
    // instead of over a filled rectangle. The same holds when the frame art is
    // missing - the cells are then simply not drawn, never filled with a
    // stand-in colour.
    if (rowCount == 0)
    {
        DrawString(pSurface, IdleText(), PANEL_X + 5, top + 4,
                   CfgColor(Cfg().IdleTextColor), kFontSmall);
        return;
    }

    // One frame set for the whole board: the local player's own side. Resolved
    // once per frame (it is the same PCX for every row), so the eight rows share
    // a single lookup rather than repeating it per player.
    BSurface* pFrame = SidePCX(PanelSideIndex(), kSideArtCenter);

    // What a dissolving cameo washes toward: this frame's own recess, sampled
    // once for the whole board.
    const ColorStruct recess = RecessColor(pFrame);

    // One clock reading for the whole board, so every icon's fade, glide and
    // pulse advance on the same tick. GetTickCount is wall-clock, which is
    // what "0.2 s" means here; a dropped frame stretches a step, never the total.
    const DWORD now = GetTickCount();
    AnimUpdate(rows, rowCount, now);

    // Per-row block tops: the ghost pass, the label column and the icon grid
    // all position off these, so they can never drift apart. Sized for the
    // hard row cap; rowCount is already clamped to the ini value.
    int rowTop[kHardMaxRows];
    {
        // The whole stack starts g_ScrollLines lines above the band top; lines
        // scrolled out of the band fail the fit tests below.
        int acc = top - g_ScrollLines * (CELL_H + ROW_GAP);
        for (int i = 0; i < rowCount; ++i)
        {
            rowTop[i] = acc;
            acc += RowLines(rows[i]) * (CELL_H + ROW_GAP);
        }
    }

    // ---- pass A: dissolving ghosts, drawn under every live icon
    //
    // A type that left the list keeps drawing at its old slot while it
    // dissolves. Since MOVE_MS < FADE_MS (or MOVE_MS = 0, an instant snap), its
    // replacement is always in place before the ghost is gone, so the slot
    // never blinks empty. FADE_MS = 0 skips this pass entirely - the guard below
    // is true on the very first frame, which is the "no dissolve" behaviour.
    for (const auto& g : g_Anim)
    {
        if (!g.Used || g.Present || !g.HasGhost)
            continue;
        if (now - g.GoneSince >= FADE_MS)
            continue;

        // The ghost needs its house's block to sit in; a house that left the
        // match has no block, so its ghost is dropped with it.
        int i = 0;
        for (; i < rowCount; ++i)
            if (rows[i].House == g.House)
                break;
        if (i >= rowCount)
            continue;

        const int alpha = 100 - FadeAlpha(g.GoneSince, now);
        // Slot position in PIXELS: interpolating in cell units and rounding per
        // frame would quantise the glide to a single mid-flight snap, which is
        // invisible. The same maths runs for ghosts, which sit still, so both
        // passes share one formula.
        const int cellX = PANEL_X + LABEL_W + static_cast<int>(g.PosCol * CELL_W + 0.5f);
        const int cellY = rowTop[i] + static_cast<int>(g.PosLine * (CELL_H + ROW_GAP) + 0.5f);

        // A ghost outside the visible band - scrolled past the top or cut off at
        // the bottom - is not drawn, same rule as live icons.
        if (cellY < top || cellY + CELL_H > bottom)
            continue;

        // No frame art at all (a mod that does not ship the swside set): the
        // cell is simply not drawn. Deliberately no filled recess as a
        // stand-in - see the "no backdrop" note above the pass, the board
        // paints no backing colour of its own.
        if (pFrame)
        {
            RectangleStruct cell { cellX, cellY, CELL_W, CELL_H };
            PCX::Instance.BlitToSurface(&cell, pSurface, pFrame);
        }

        DrawCameoArt(pSurface, cellX + CAMEO_X, cellY + CAMEO_Y, g.Type);

        if (g.Ghost.Building)
        {
            DrawClock(pSurface, cellX + CAMEO_X, cellY + CAMEO_Y, g.Ghost.Step);
            DrawProgress(pSurface, cellX + CAMEO_X, cellY + CAMEO_Y, g.Ghost);
        }
        else if (Cfg().ShowCountChip)
            DrawCount(pSurface, cellX + CAMEO_X, cellY + CAMEO_Y, g.Ghost.Count);

        WashCameo(pSurface, cellX + CAMEO_X, cellY + CAMEO_Y, alpha, recess);
    }

    const int nameH = TextHeight(kFontTiny);

    // ---- pass B: one row per player, labels + live icons
    for (int i = 0; i < rowCount; ++i)
    {
        const PlayerRow& r = rows[i];
        HouseClass* pHouse = r.House;
        const int y = rowTop[i];

        const int lines = RowLines(r);

        // Which of this block's lines fall inside the visible band. The label
        // centres on the VISIBLE lines only, so a block whose second line is
        // below the fold keeps its flag beside its first line instead of
        // floating halfway toward content that is never drawn.
        int firstVis = -1;
        int lastVis  = -1;
        for (int k = 0; k < lines; ++k)
        {
            const int lineY = y + k * (CELL_H + ROW_GAP);
            if (lineY >= top && lineY + CELL_H <= bottom)
            {
                if (firstVis < 0)
                    firstVis = k;
                lastVis = k;
            }
        }
        if (firstVis < 0)
            continue;   // whole block outside the band

        const int visY0 = y + firstVis * (CELL_H + ROW_GAP);
        const int visH  = (lastVis - firstVis + 1) * (CELL_H + ROW_GAP) - ROW_GAP;

        // Country flag, replacing the country name, with the player id beneath
        // it. The label column has to hold both, so the two are stacked
        // explicitly: flag on top, name below, centred on the whole block.
        const int stackH = FLAG_H + 2 + nameH;              // flag + gap + name
        const int stackY = visY0 + (visH - stackH) / 2;
        // Centred in the label column, same as the name, so the two line up.
        const int flagX  = PANEL_X + (LABEL_W - FLAG_W) / 2;
        const int flagY  = stackY;

        if (BSurface* pFlag = FlagPCX(pHouse))
        {
            RectangleStruct dst { flagX, flagY, FLAG_W, FLAG_H };
            PCX::Instance.BlitToSurface(&dst, pSurface, pFlag);
        }

        // Player id, in the player's own house colour so a row is identifiable
        // at a glance. This is the only text left on the label column. The
        // name may contain CJK (spawn.ini nicknames, the localised AI label),
        // so the row's wide string is drawn as-is; the engine routes strings
        // with CJK codepoints to its CJK bitmap font on its own.
        const wchar_t* wname = r.PlayerW;
        const int pw = TextWidth(wname, kFontTiny);
        DrawString(pSurface, wname, PANEL_X + (LABEL_W - pw) / 2,
                   stackY + FLAG_H + 2,
                   Drawing::RGB_To_Int(pHouse->Color.R, pHouse->Color.G, pHouse->Color.B),
                   kFontTiny);

        // ---- the dynamic icon strip
        //
        // Icons flow left to right, four per line, wrapping onto a new line
        // under the previous one. Each icon draws at its ANIMATED slot (which
        // may be mid-glide or mid-fade), not at its list index. pFrame is the
        // board-wide frame resolved above, not this house's.
        for (int k = 0; k < r.IconCount; ++k)
        {
            const RowIcon& icon = r.Icons[k];
            const IconAnim& rec = *AnimFor(pHouse, icon.Type);

            // Animated slot position in PIXELS (not rounded to cells - see the
            // ghost-pass note above: cell-rounded drawing quantises the glide
            // away).
            const int cellX = PANEL_X + LABEL_W + static_cast<int>(rec.PosCol * CELL_W + 0.5f);
            const int cellY = y + static_cast<int>(rec.PosLine * (CELL_H + ROW_GAP) + 0.5f);

            // Outside the band, not drawn. A mover gliding toward a line below
            // the fold vanishes at the edge - the same "no content past the
            // band" rule the rest of the board follows.
            if (cellY < top || cellY + CELL_H > bottom)
                continue;

            // Same rule as the ghost pass: with no frame art the cell is left
            // empty rather than filled with a stand-in colour.
            if (pFrame)
            {
                RectangleStruct cell { cellX, cellY, CELL_W, CELL_H };
                PCX::Instance.BlitToSurface(&cell, pSurface, pFrame);
            }

            const int cx = cellX + CAMEO_X;
            const int cy = cellY + CAMEO_Y;

            // Art, then readout, then the dissolve wash over the whole layer -
            // so the chip and digits fade together with the art instead of
            // popping off when the wash runs last on its own.
            const int alpha = FadeAlpha(rec.FirstSeen, now);

            if (alpha > 0)
            {
                DrawCameoArt(pSurface, cx, cy, icon.Type);

                if (icon.Building)
                {
                    // Structure under construction: the engine's own gclock2
                    // sweep over the cameo is the progress display.
                    DrawClock(pSurface, cx, cy, icon.Step);
                    DrawProgress(pSurface, cx, cy, icon);
                }
                else if (Cfg().ShowCountChip)
                    DrawCount(pSurface, cx, cy, icon.Count);

                WashCameo(pSurface, cx, cy, alpha, recess);
            }
        }
    }

    // ---- scroll gauge
    //
    // With lines cut off above or below, a 2px gauge on the board's left edge
    // shows that there is more board than the band holds, and where in it the
    // are: track = the band, thumb = the visible share of the content, slid
    // down by the scroll position. Without it, hidden lines are undetectable.
    if (g_MaxScrollLines > 0)
    {
        const int trackH = bottom - top;
        int thumbH = visLines * trackH / m.Lines;
        if (thumbH < 12)
            thumbH = 12;
        const int thumbY = top + (trackH - thumbH) * g_ScrollLines / g_MaxScrollLines;

        RectangleStruct track { PANEL_X, top, 2, trackH };
        pSurface->FillRect(&track, CfgColor(Cfg().ScrollTrackColor));

        RectangleStruct thumb { PANEL_X, thumbY, 2, thumbH };
        pSurface->FillRect(&thumb, CfgColor(Cfg().ScrollThumbColor));
    }
}

// ------------------------------------------------------------------ toggle
//
// The board is shown or hidden by a 10x50 strip pinned to its right edge - the
// same swsideNNon/off art, the same gadget mechanics and the same two-stage
// press-then-release trigger the super-weapon sidebar uses.
//
// The whole control follows the board's gate: when the board is not up for me
// (a participant under the shipped WatchBar.SpectatorOnly=1, or no match),
// neither the board nor this strip exists on screen, and the strip's hit-box is
// parked off the playfield so it cannot swallow map clicks (see UpdatePosition).
//
// The art is the *current player's* side, so the strip matches their sidebar.
// The strip's X glides to a changed target with the same 90 ms ease-out the
// icons use: the occupied width changes exactly when icons arrive or dissolve,
// and everything else on screen is gliding at that moment too, so a control
// that teleports 72 px sideways would stick out. First placement after being
// parked or collapsed snaps into place; only changes while openly placed glide.
//
// The pager's right-hand button rides the same ease for the same reason (the
// board's occupied width is what it is glued to), so it owns a second state
// block rather than sharing the strip's - the two have different targets.
struct GlideState
{
    float Cur  = 0.0f;    // animated position, pixels
    float From = 0.0f;    // glide start
    int   Tgt  = 0;       // target
    DWORD Since = 0;
    bool  Placed = false; // false until the next Track call snaps
};

static GlideState s_TglGlide;
static GlideState s_ArrowGlide;

static int GlideTrack(GlideState& g, int targetX)
{
    const DWORD now = GetTickCount();

    if (!g.Placed)
    {
        g.Placed = true;
        g.Tgt    = targetX;
        g.Cur    = g.From = static_cast<float>(targetX);
    }
    else if (targetX != g.Tgt)
    {
        g.Tgt   = targetX;
        g.From  = g.Cur;           // restart from the current pose, as icons do
        g.Since = now;
    }

    // MOVE_MS = 0 (glide off) makes GlideProgress report "finished", so the
    // control snaps to the target instead of sweeping - and no NaN can reach
    // the gadget position through g.Cur.
    g.Cur = g.From + (g.Tgt - g.From) * GlideProgress(g.Since, now);

    return static_cast<int>(g.Cur + 0.5f);
}

// Forget a glide so the next GlideTrack snaps into place instead of sweeping in
// from wherever the control last stood.
static void GlideForget(GlideState& g)
{
    g.Placed = false;
}

static int ToggleTrackX(int targetX)
{
    return GlideTrack(s_TglGlide, targetX);
}

static void ToggleForgetPlacement()
{
    GlideForget(s_TglGlide);
}

class WatchBarToggleButtonClass : public GadgetClass
{
public:
    // Which side's on/off art to use. Re-resolved every draw so a scenario
    // change (or a different local player) cannot leave stale art on screen.
    // Shares PanelSideIndex with the board itself, so the strip and the frames
    // it sits beside always come from the same side.
    static int CurrentSideIndex()
    {
        return PanelSideIndex();
    }

    // Place the strip: on the panel's right edge while the board is open, and
    // hugging the top-left corner while it is closed so the control stays
    // reachable. Mirrors ToggleSWButtonClass::UpdatePosition().
    //
    // Gated: while the board is not up for me - a participant under the
    // shipped WatchBar.SpectatorOnly=1, or no match at all - the strip is parked
    // fully off the playfield. The gadget's hit-box IS its X/Y rect, so an
    // off-screen rect can neither be clicked nor cover part of the map: a strip
    // left on screen would be an invisible control swallowing map clicks.
    //
    // Called from the draw hook *before* input is processed, not from Draw():
    // the gadget's hit-box comes from X/Y, so updating it during painting would
    // leave the clickable area a frame behind where the art is drawn.
    //
    // Uses g_LastMetrics (DrawPanel's last run) rather than re-measuring: the
    // full scan is not worth a per-frame repeat just for the strip's Y. See
    // the note on g_LastMetrics.
    void UpdatePosition()
    {
        if (!BoardVisibleToMe())
        {
            ToggleForgetPlacement();
            this->X = -TOGGLE_W - 4;
            this->Y = -TOGGLE_H - 4;
            return;
        }

        if (g_PanelOpen)
        {
            this->X = ToggleTrackX(ToggleX(g_LastMetrics));
            this->Y = ToggleY(g_LastMetrics);
        }
        else
        {
            // Collapsed: sit exactly where the open board's top-left corner is,
            // so the control does not jump around when the board is toggled and
            // stays in the same place at any resolution.
            ToggleForgetPlacement();
            this->X = PANEL_X;
            this->Y = PanelY();
        }
    }

    WatchBarToggleButtonClass()
        : GadgetClass(0, 0, TOGGLE_W, TOGGLE_H,
                      GadgetFlag::LeftPress | GadgetFlag::LeftRelease, false)
    { }

    bool Draw(bool forced) override
    {
        // Gated: paint nothing while the board is not up for me, so no mode
        // transition can flash the strip for a frame. UpdatePosition has already
        // parked the hit-box off-screen.
        if (!BoardVisibleToMe())
            return true;

        const auto pSurface = DSurface::Composite;
        if (!pSurface)
            return false;

        const int side = CurrentSideIndex();

        // OnPCX is the "sidebar is open" art and OffPCX the "collapsed" art.
        BSurface* pStrip = SidePCX(side, g_PanelOpen ? kSideArtOn : kSideArtOff);

        if (pStrip)
        {
            RectangleStruct dst { this->X, this->Y, TOGGLE_W, TOGGLE_H };
            PCX::Instance.BlitToSurface(&dst, pSurface, pStrip);
        }
        else
        {
            // Visible fallback so a missing PCX never makes the control vanish.
            RectangleStruct dst { this->X, this->Y, TOGGLE_W, TOGGLE_H };
            pSurface->FillRect(&dst, CfgColor(Cfg().ToggleGlyphOffColor));

            RectangleStruct inner { this->X + 1, this->Y + TOGGLE_H / 2 - 1, TOGGLE_W - 2, 2 };
            pSurface->FillRect(&inner, CfgColor(Cfg().ToggleGlyphOnColor));
        }

        return true;
    }

    // Two-stage trigger: act on the matching release, exactly as
    // ToggleSWButtonClass does, so a press that is dragged off does not fire.
    bool Action(GadgetFlag flags, DWORD* pKey, KeyModifier modifier) override
    {
        if (flags & GadgetFlag::LeftRelease)
            g_PanelOpen = !g_PanelOpen;

        this->GadgetClass::Action(flags, pKey, KeyModifier::None);
        return true;
    }

    void OnMouseEnter() override
    {
        MouseClass::Instance.UpdateCursor(MouseCursorType::Default, false);
        this->GadgetClass::OnMouseEnter();
    }
};

static WatchBarToggleButtonClass* g_pToggle = nullptr;

// Create the toggle once, after the scenario's sidebar exists.
//
// Deliberately no ScenarioClass::Start hook: the toggle is a plain gadget that
// only reads HouseClass/HouseTypeClass state and draws a PCX, so it holds no
// pointer into scenario-owned memory that could dangle. Recreating it per
// scenario would risk leaving a freed gadget linked into the engine's list, so
// one stable gadget is kept for the process lifetime.
static void EnsureToggle()
{
    if (g_pToggle)
        return;

    // GScreenClass::Instance owns the gadget list; AddButton links it in so it
    // receives input and gets drawn by DrawOnTop.
    g_pToggle = GameCreate<WatchBarToggleButtonClass>();
    g_pToggle->Zap();
    GScreenClass::Instance.AddButton(g_pToggle);

    // Position it now, so the gadget has a sane hit-box even before the first
    // frame is drawn.
    g_pToggle->UpdatePosition();
}

// ------------------------------------------------------------------ arrows
//
// The board's scroll control: two buttons glued together at the MIDDLE of its
// bottom edge - up on the left of the pair, down on the right - hanging just
// under the lowest visible row, so they read as one control rather than as two
// loose buttons parked at the board's corners.
//
// They use the game's own gadget input path - the same clicks the toggle strip
// answers - because the mouse wheel never reaches this process (see the scroll
// section).
//
// Same audience rule as the strip: spectator + board open, hit-box parked
// off-screen otherwise so it can neither be clicked nor eat map clicks.
// Visible whenever the board is open, dimmed while the board fits or sits at a
// scroll limit, so the control is discoverable; a click while dimmed is a no-op
// by the clamp. Press and hold repeats: one line at once, then one line every
// ScrollRepeatRateMs after a ScrollRepeatDelayMs delay.
//
// The art is WatchBar.UpPCX / WatchBar.DownPCX from the side's own rules
// section; with neither declared the buttons fall back to a chevron drawn with
// FillRect, so the control never depends on a mod shipping that art.
// Sizes, horizontal position and timings come from WatchBar.ScrollButtonWidth /
// ScrollButtonHeight / ScrollButtonOffsetX / ScrollRepeatDelayMs /
// ScrollRepeatRateMs - see the parameter block at the top of this file.

// Bottom edge the arrow pair hangs from: the bottom of the lowest line the
// band actually shows. Fits -> the last content row's bottom; overflow ->
// the band-capped line, a constant while the board overflows (scroll is
// line-aligned), so the pair never chases the scroll position.
static int ScrollHangY(const PanelMetrics& m)
{
    const int top   = PanelY();
    const int bandH = BandBottom() - top;
    const int visH  = bandH > 0
        ? (bandH / (CELL_H + ROW_GAP)) * (CELL_H + ROW_GAP) - ROW_GAP
        : m.Height;
    return top + (m.Height < visH ? m.Height : visH);
}

// X of the PAIR: the two buttons sit side by side with no gap between them, and
// the pair as a whole is centred on the board's OCCUPIED width - the same edge
// the toggle strip hugs - so the control belongs to the board rather than
// floating beside empty grid. WatchBar.ScrollButtonOffsetX shifts the pair off
// that centre. Clamped into the board so a board narrower than the pair cannot
// push the up button past its left edge.
static int ArrowPairX(const PanelMetrics& m)
{
    const int board = LABEL_W + m.UsedCols * CELL_W;
    const int left  = PANEL_X + (board - 2 * SCROLL_BTN_W) / 2 + SCROLL_BTN_OFFSET_X;
    return left < PANEL_X ? PANEL_X : left;
}

// Y of both buttons: flush under the board's bottom edge, clamped into the band
// so a full board (whose last line ends right at the band) cannot push the
// control off screen - it tucks up against the bottom edge instead.
static int ArrowY(const PanelMetrics& m)
{
    int y = ScrollHangY(m);
    const int limit = BandBottom() - SCROLL_BTN_H;
    if (y > limit)
        y = limit;
    return y < PanelY() ? PanelY() : y;
}

class WatchBarArrowButtonClass : public GadgetClass
{
public:
    explicit WatchBarArrowButtonClass(bool up)
        : GadgetClass(0, 0, SCROLL_BTN_W, SCROLL_BTN_H,
                      GadgetFlag::LeftPress | GadgetFlag::LeftRelease, false),
          m_Up(up)
    { }

    // Called from the draw hook before input is processed - the hit-box is
    // the X/Y rect, so it must lead the painted art, not trail it. Also ticks
    // the hold-to-repeat, which has no engine timer of its own.
    void UpdatePosition()
    {
        if (!BoardVisibleToMe() || !g_PanelOpen)
        {
            m_Held = false;
            GlideForget(s_ArrowGlide);
            this->X = -SCROLL_BTN_W - 4;
            this->Y = -SCROLL_BTN_H - 4;
            return;
        }

        const PanelMetrics& m = g_LastMetrics;

        // Up on the left of the pair, down on the right: the two read as one
        // control. Both ride the SAME glide, so a change in the board's
        // occupied width sweeps the pair as a unit instead of letting the two
        // halves drift apart mid-sweep.
        const int pairX = GlideTrack(s_ArrowGlide, ArrowPairX(m));
        this->X = m_Up ? pairX : pairX + SCROLL_BTN_W;
        this->Y = ArrowY(m);

        if (m_Held)
        {
            const DWORD now = GetTickCount();
            if (static_cast<int>(now - m_NextRepeat) >= 0)
            {
                Step();
                m_NextRepeat = now + SCROLL_REPEAT_RATE_MS;
            }
        }
    }

    bool Draw(bool forced) override
    {
        if (!BoardVisibleToMe() || !g_PanelOpen)
            return true;   // parked: paint nothing

        auto pSurface = DSurface::Composite;
        if (!pSurface)
            return false;

        const bool active = m_Up ? g_ScrollLines > 0
                                 : g_MaxScrollLines > 0
                                   && g_ScrollLines < g_MaxScrollLines;

        RectangleStruct rect { this->X, this->Y, SCROLL_BTN_W, SCROLL_BTN_H };

        // WatchBar.UpPCX / DownPCX from the side's own rules section. One
        // picture per direction, so the button's state is carried by a wash
        // instead of a second file: dimmed = nothing to scroll that way, a
        // bright wash = held down.
        if (BSurface* pArt = SidePCX(PanelSideIndex(),
                                     m_Up ? kSideArtUp : kSideArtDown))
        {
            PCX::Instance.BlitToSurface(&rect, pSurface, pArt);

            if (!active)
            {
                const WatchBarColor& c = Cfg().CountChipColor;
                ColorStruct dim { static_cast<BYTE>(c.R), static_cast<BYTE>(c.G),
                                  static_cast<BYTE>(c.B) };
                pSurface->FillRectTrans(&rect, &dim, 45);
            }
            else if (m_Held)
            {
                ColorStruct lit { 255, 255, 255 };
                pSurface->FillRectTrans(&rect, &lit, 30);
            }
            return true;
        }

        // No art: the drawn chevron. The count chip's grey-black plate keeps it
        // reading as board furniture rather than map UI.
        const COLORREF glyph = m_Held   ? CfgColor(Cfg().ScrollGlyphHeldColor)
                               : active ? CfgColor(Cfg().ScrollGlyphActiveColor)
                                        : CfgColor(Cfg().ScrollGlyphIdleColor);
        pSurface->FillRect(&rect, CfgColor(Cfg().CountChipColor));

        // A solid chevron: four 2px rows, widest at the base, centred in the
        // button width. Drawn with FillRect - no font dependency, pixel-exact.
        // Sized off the button so it stays centred at any ScrollButtonWidth.
        for (int r = 0; r < 4; ++r)
        {
            const int w    = m_Up ? 2 + 2 * r : 8 - 2 * r;
            const int xOff = (SCROLL_BTN_W - w) / 2;
            const int yOff = (SCROLL_BTN_H - 8) / 2;
            RectangleStruct bar { this->X + xOff, this->Y + yOff + 2 * r, w, 2 };
            pSurface->FillRect(&bar, glyph);
        }
        return true;
    }

    // Scroll buttons act on PRESS (a scrollbar convention; the two-stage
    // press-then-release of the toggle is for toggles, where acting on a
    // dragged-off release would flip state by accident). SWButtonClass'
    // Action branches on LeftPress too, so the flag is delivered.
    bool Action(GadgetFlag flags, DWORD* pKey, KeyModifier modifier) override
    {
        if (flags & GadgetFlag::LeftPress)
        {
            m_Held       = true;
            m_NextRepeat = GetTickCount() + SCROLL_REPEAT_DELAY_MS;
            Step();
        }
        else if (flags & GadgetFlag::LeftRelease)
        {
            m_Held = false;
        }

        this->GadgetClass::Action(flags, pKey, KeyModifier::None);
        return true;
    }

    void OnMouseEnter() override
    {
        MouseClass::Instance.UpdateCursor(MouseCursorType::Default, false);
        this->GadgetClass::OnMouseEnter();
    }

private:
    // One scroll step, clamped into [0, g_MaxScrollLines]; a no-op while the
    // board fits or sits at a limit, which is what the dimmed button state
    // indicates.
    //
    // The step is WatchBar.ScrollStep: N lines, or "page" (stored as 0) for one
    // whole screenful - g_VisLines is however many whole lines the band shows
    // right now, so a page click always lands exactly one screen further. A
    // page that would overshoot the end clamps, so the last move can be short.
    void Step()
    {
        int n = Cfg().ScrollStep;
        if (n <= 0)
            n = g_VisLines;
        if (n < 1)
            n = 1;

        int next = g_ScrollLines + (m_Up ? -n : n);
        if (next < 0)
            next = 0;
        if (next > g_MaxScrollLines)
            next = g_MaxScrollLines;
        if (next != g_ScrollLines)
        {
            static bool s_Noted = false;
            if (!s_Noted)
            {
                s_Noted = true;
                LogLine("scroll: arrows first use (%d -> %d of %d, step=%d)",
                        g_ScrollLines, next, g_MaxScrollLines, n);
            }
            g_ScrollLines = next;
        }
    }

    bool  m_Up         = false;
    bool  m_Held       = false;
    DWORD m_NextRepeat = 0;
};

static WatchBarArrowButtonClass* g_pScrollUp   = nullptr;
static WatchBarArrowButtonClass* g_pScrollDown = nullptr;

// Create the pair once, after the scenario's sidebar exists - the same
// process-lifetime reasoning as EnsureToggle: plain gadgets holding no
// scenario-owned pointers, so recreating them per scenario would only risk
// leaving freed gadgets in the engine's list.
static void EnsureScrollButtons()
{
    if (g_pScrollUp)
        return;

    g_pScrollUp   = GameCreate<WatchBarArrowButtonClass>(true);
    g_pScrollDown = GameCreate<WatchBarArrowButtonClass>(false);
    g_pScrollUp->Zap();
    g_pScrollDown->Zap();
    GScreenClass::Instance.AddButton(g_pScrollUp);
    GScreenClass::Instance.AddButton(g_pScrollDown);

    // Sane hit-boxes even before the first frame is drawn.
    g_pScrollUp->UpdatePosition();
    g_pScrollDown->UpdatePosition();
}

// ------------------------------------------------------------------ hooks

// The panel's settings, read out of the game's own uimd.ini.
//
// CCINIClass::INI_UIMD (0x887208) is the INI object the engine loaded uimd.ini
// into, so a uimd.ini that lives inside a MIX - EC ships no loose one - is read
// exactly as the game read it, and there is no second parser to disagree with
// the engine's about quoting, comments or encoding.
//
// The section pointer handed to the engine is Config.cpp's compile-time
// constant, never a stack buffer: INIClass caches the CALLER's section pointer
// inside the object and skips the section search when the same pointer comes
// back (the hazard ArtSectionOf documents above), so a stable pointer is what
// makes repeated reads correct as well as fast.
static int EngineUimdRead(const char* section, const char* key, const char* def,
                          char* out, int cap)
{
    return CCINIClass::INI_UIMD.ReadString(section, key, def, out, cap);
}

static int EngineUimdKeyCount(const char* section)
{
    return CCINIClass::INI_UIMD.GetKeyCount(section);
}

static const char* EngineUimdKeyName(const char* section, int index)
{
    return CCINIClass::INI_UIMD.GetKeyName(section, index);
}

static const WatchBarIniSource kEngineUimdSource =
{
    "uimd.ini (engine)",
    &EngineUimdRead,
    &EngineUimdKeyCount,
    &EngineUimdKeyName
};

// Top-level draw point, the same address Phobos' ObjectInfo uses. By this point
// the game has loaded DSurface::Composite (0x88731C) into a register, so the
// surface is valid for immediate drawing.
DEFINE_HOOK(0x4F4583, GScreenClass_DrawOnTop_WatchBar, 0x6)
{
    // Config first: it carries the log settings and every geometry value the
    // gadgets below are positioned with. Loading here - not inside DrawPanel -
    // means an author whose board never appears still gets the config report
    // and the exe-compatibility warning in the log.
    //
    // This hook only runs in-game, which is well after the engine has read
    // uimd.ini, so the source is installed first and the values are there on
    // the very first call. Reading the config is a one-shot: the engine loads
    // uimd.ini at startup, so a changed value needs a game restart (the log
    // explains this when a key that is no longer reloadable is present).
    static bool s_SourceInstalled = false;
    if (!s_SourceInstalled)
    {
        s_SourceInstalled = true;
        ConfigSetIniSource(&kEngineUimdSource);
    }
    ConfigBeginFrame();

    static bool s_Announced = false;
    static int  s_SeenSectionFound = -1;
    if (!s_Announced)
    {
        s_Announced = true;
        s_SeenSectionFound = Cfg().SectionFound;

        // Apply the log settings before anything is written, then report.
        ApplyLogSettings();
        LogLine("==== WatchBar %s (%s %s) ====", WATCHBAR_VERSION, __DATE__, __TIME__);
        DrainConfigMessages();
    }
    else if (Cfg().SectionFound != s_SeenSectionFound)
    {
        // The [WatchBar] section turned up after the first read (see the retry
        // in ConfigBeginFrame): report again, so the log never leaves "using
        // built-in defaults" standing for a board that is using real settings.
        s_SeenSectionFound = Cfg().SectionFound;
        ApplyLogSettings();
        DrainConfigMessages();
    }

    EnsureToggle();

    // The board's scroll input: the arrow pair under the board's bottom edge
    // (up/down glued together, centred on its occupied width). The mouse wheel
    // never reaches this process (see the scroll section).
    EnsureScrollButtons();

    // Log the gate only when it FLIPS, so the log shows exactly when and why
    // the panel appeared or vanished (observer flag / Defeated flag / spawn
    // slot) without any per-frame noise. `board` is the answer the drawing and
    // the gadgets use, `spectating` the answer the row filter uses - with
    // SpectatorOnly=0 the two differ for a participant, and that difference is
    // exactly what WatchBar.ParticipantRows decides.
    const bool spectating = IsSpectating();
    static bool s_WasSpectating = false;
    static bool s_GateSeen = false;
    if (!s_GateSeen || spectating != s_WasSpectating)
    {
        s_GateSeen = true;
        s_WasSpectating = spectating;
        const auto pMe = HouseClass::CurrentPlayer;
        LogLine("gate: board=%d spectating=%d participantRows=%s "
                "(observer=%d defeated=%d spawn=%d)",
                BoardVisibleToMe() ? 1 : 0,
                spectating ? 1 : 0,
                ParticipantRowsName(Cfg().ParticipantRows),
                HouseClass::IsCurrentPlayerObserver() ? 1 : 0,
                (pMe && pMe->Defeated) ? 1 : 0,
                pMe ? pMe->GetSpawnPosition() : -2);
    }

    // Gated: the board and its toggle exist only for an audience - a spectator,
    // or a participant when WatchBar.SpectatorOnly=0 asked for the board. Under
    // the shipped default a participant sees neither, must not see it (their own
    // production already lives on the game's real sidebar) and must not have an
    // invisible clickable control on their playfield. BoardVisibleToMe() is the
    // gate, and the row filter inside uses IsSpectating() plus
    // WatchBar.ParticipantRows, so the two can disagree about WHO is listed but
    // never about whether the board exists.
    // UpdatePosition parks the gadgets off-screen for the non-spectator case;
    // Draw() additionally paints nothing.
    g_pToggle->UpdatePosition();
    g_pScrollUp->UpdatePosition();
    g_pScrollDown->UpdatePosition();

    // The gadget itself is drawn by the game's own DrawOnTop pass; this hook
    // only paints the board underneath it. Gating here (not inside DrawPanel)
    // also skips the per-frame production + unit scans when the board is not up.
    if (g_PanelOpen && BoardVisibleToMe())
        DrawPanel();

    return 0;
}

// NOTE on map-click swallowing.
//
// The board is painted over the playfield, so a click on it falls through and
// orders whatever unit sits underneath. Phobos guards against this with a hook
// at DisplayClass::ProcessClickCoords (0x692419); that hook is not replicated
// here because the register that carries the click point at that address has not
// been confirmed on a live process. Guessing it would read garbage coordinates
// and could block clicks anywhere on the map, which is worse than the
// fall-through. The address itself is reachable: 0x692419 - 0x400000 = 0x292419,
// inside .text (VA 0x1000, VSize 0x3DF38D), and the bytes there read as valid
// code (`0F BF 0E 0F BF 56 02` = movsx ecx, word [esi] / movsx edx, word
// [esi+2]), consistent with Phobos' hook.
//
// The exposure is small: the board covers a corner of the screen, and a stray
// order beneath it is immediately visible and reversible.

// Hotkey as an optional convenience; the visible toggle strip is the primary
// control.
class WatchBarToggleCommandClass : public CommandClass
{
public:
    virtual const char* GetName() const override
    {
        return "Toggle WatchBar";
    }

    virtual const wchar_t* GetUIName() const override
    {
        return L"WatchBar";
    }

    virtual const wchar_t* GetUICategory() const override
    {
        return StringTable::LoadString(GameStrings::TXT_INTERFACE);
    }

    virtual const wchar_t* GetUIDescription() const override
    {
        return L"Show or hide the in-game spectator board.";
    }

    virtual void Execute(WWKey eInput) const override
    {
        g_PanelOpen = !g_PanelOpen;
    }
};

// Register after Ares/Phobos, using the same callback address they use.
DEFINE_HOOK(0x533066, CommandClassCallback_Register_WatchBar, 0x6)
{
    CommandClass::Array.AddItem(GameCreate<WatchBarToggleCommandClass>());

    return 0;
}
