// ============================================================================
// WatchBar - runtime configuration (parser)
// ----------------------------------------------------------------------------
// One section, one prefix, two possible sources.
//
// The settings live in the game's own uimd.ini, under [WatchBar]:
//
//     [WatchBar]
//     WatchBar.PanelX=0
//     WatchBar.DoneColor=120,255,140
//
// The parser reads them either out of the engine's already-loaded CCINIClass
// (the in-game path: the DLL installs that source - see WatchBar.cpp) or out of
// a loose uimd.ini beside the executable (tools\ini_check, and the fallback).
// Both go through the same ApplyKey below, so the checker can never disagree
// with the game about what a key means.
//
// The loose-file reader is hand-rolled rather than GetPrivateProfileString:
// that API caches the file and would run a UTF-8 value through the ANSI
// codepage. It also skips a UTF-8 BOM so a Notepad save cannot break it.
// ============================================================================
#include "Config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

WatchBarConfig g_WatchBarCfg;

// [WatchBar] MaxIconsPerGroup default for the building block: the building
// group is the only counted group that can be arbitrarily large (a base runs
// 8-15 distinct types mid-game), so it ships capped and the rest do not. Named
// here because Validate() has to tell "the author set it" from "this is just
// the default".
static constexpr int kDefaultMaxIconsBuilding = 3;

// [WatchBar] GroupOrder default: the board's own long-standing layout, which is
// what players of this panel already read - structures and armour in
// production, then the base, then the army with the vehicles (and the aircraft
// that ride with them) ahead of the infantry.
static const int kDefaultGroupOrder[kGroupCount] =
{
    kGroupProduction, kGroupBuilding, kGroupVehicle, kGroupAircraft, kGroupInfantry
};

static const char* GroupName(int group)
{
    switch (group)
    {
    case kGroupProduction: return "production";
    case kGroupBuilding:   return "building";
    case kGroupInfantry:   return "infantry";
    case kGroupVehicle:    return "vehicle";
    case kGroupAircraft:   return "aircraft";
    default:               return "?";
    }
}

// ---------------------------------------------------------------- utilities
static bool EqNoCase(const char* a, const char* b)
{
    return a && b && _stricmp(a, b) == 0;
}

// The spelling the log uses for a WatchBarLinesMode value.
static const char* LinesModeName(int mode)
{
    switch (mode)
    {
    case kLinesFixed:   return "fixed";
    case kLinesCompact: return "compact";
    case kLinesLoose:   return "loose";
    case kLinesUltra:   return "ultra";
    default:            return "?";
    }
}

static void CopyStr(char* dst, size_t n, const char* src)
{
    if (!n)
        return;
    lstrcpynA(dst, src ? src : "", static_cast<int>(n));
}

static void Trim(char* s)
{
    if (!s)
        return;
    char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    for (char* e = s + strlen(s); e > s; --e)
    {
        if (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')
            e[-1] = '\0';
        else
            break;
    }
}

static int ToInt(const char* v, int def)
{
    if (!v || !*v)
        return def;
    char* end = nullptr;
    const long n = strtol(v, &end, 0);   // 0 = accept 0x.. and decimal
    if (end == v)
        return def;
    return static_cast<int>(n);
}

static bool ToBool(const char* v, int def)
{
    if (!v || !*v)
        return def != 0;
    if (EqNoCase(v, "1") || EqNoCase(v, "yes") || EqNoCase(v, "true") || EqNoCase(v, "on"))
        return true;
    if (EqNoCase(v, "0") || EqNoCase(v, "no") || EqNoCase(v, "false") || EqNoCase(v, "off"))
        return false;
    return ToInt(v, def) != 0;
}

// "R,G,B" or "0xRRGGBB". Channels are clamped: an out-of-range colour would
// wrap inside Drawing::RGB_To_Int and paint an unintended colour.
//
// `clamped` reports the component that had to be clamped, or -1 when the value
// was in range. The caller (ColorKey) turns that into the same "out of range"
// note IntKey emits for a clamped number, so a colour typo is not the one
// mistake the log stays silent about.
static bool ToColor(const char* v, WatchBarColor& out, int& clamped)
{
    clamped = -1;
    if (!v || !*v)
        return false;

    int r = -1, g = -1, b = -1;
    if (strchr(v, ','))
    {
        r = ToInt(v, -1);
        const char* p = strchr(v, ',');
        g = ToInt(p + 1, -1);
        p = p ? strchr(p + 1, ',') : nullptr;
        b = p ? ToInt(p + 1, -1) : -1;
    }
    else
    {
        const int hex = ToInt(v, -1);
        if (hex < 0)
            return false;
        r = (hex >> 16) & 0xFF;
        g = (hex >> 8) & 0xFF;
        b = hex & 0xFF;
    }

    if (r < 0 || g < 0 || b < 0)
        return false;

    out.R = r > 255 ? 255 : r;
    out.G = g > 255 ? 255 : g;
    out.B = b > 255 ? 255 : b;

    if (r > 255)      clamped = r;
    else if (g > 255) clamped = g;
    else if (b > 255) clamped = b;

    return true;
}

// ---------------------------------------------------------------- messages
void ConfigNote(WatchBarConfig& c, const char* fmt, ...)
{
    if (c.MessageCount >= kCfgMaxMessages)
        return;

    char* dst = c.Messages[c.MessageCount];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(dst, 159, fmt, args);
    va_end(args);
    dst[159] = '\0';
    ++c.MessageCount;
}

static void NoteUnknown(WatchBarConfig& c, const char* key)
{
    if (c.UnknownKeyCount >= kCfgMaxUnknownKeys)
        return;
    _snprintf(c.UnknownKeys[c.UnknownKeyCount], 63, "[WatchBar] %s", key);
    c.UnknownKeys[c.UnknownKeyCount][63] = '\0';
    ++c.UnknownKeyCount;
}

// ------------------------------------------------------------------- paths
static const char* ExeFolder()
{
    static char dir[MAX_PATH];
    static bool done = false;
    if (done)
        return dir;

    done = true;
    if (!GetModuleFileNameA(nullptr, dir, MAX_PATH))
    {
        dir[0] = '\0';
        return dir;
    }
    char* slash = strrchr(dir, '\\');
    if (slash)
        slash[1] = '\0';
    else
        dir[0] = '\0';
    return dir;
}

static void BuildPath(char* dst, size_t n, const char* leaf)
{
    _snprintf(dst, n - 1, "%s%s", ExeFolder(), leaf);
    dst[n - 1] = '\0';
}

const char* ConfigIniPath()
{
    static char path[MAX_PATH];
    static bool done = false;
    if (!done)
    {
        done = true;
        BuildPath(path, sizeof(path), "uimd.ini");
    }
    return path;
}

// The host executable's PE timestamp. This is how the panel tells "the mod
// ships a patched gamemd.exe" (hook addresses may mean something else there)
// apart from "the panel is broken": a mismatch is logged, never fatal.
static DWORD ReadExeTimestamp()
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
        return 0;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    DWORD ts = 0, got = 0;
    LONG  pe = 0;
    SetFilePointer(h, 0x3C, nullptr, FILE_BEGIN);
    if (ReadFile(h, &pe, 4, &got, nullptr) && got == 4 && pe > 0)
    {
        DWORD sig = 0;
        SetFilePointer(h, pe, nullptr, FILE_BEGIN);
        if (ReadFile(h, &sig, 4, &got, nullptr) && got == 4 && sig == 0x00004550)
        {
            SetFilePointer(h, pe + 8, nullptr, FILE_BEGIN);
            if (!ReadFile(h, &ts, 4, &got, nullptr) || got != 4)
                ts = 0;
        }
    }
    CloseHandle(h);
    return ts;
}

// ------------------------------------------------------------------ defaults
// These are the reference values. A missing or empty [WatchBar] section must
// change nothing: with no key at all the board is exactly this configuration.
static void SetDefaults(WatchBarConfig& c)
{
    memset(&c, 0, sizeof(c));

    // geometry
    c.PanelX            = 0;
    c.TopMargin         = 10;
    c.TopPercent        = 25;
    c.BandMargin        = 4;
    c.LabelWidth        = 60;
    c.CellWidth         = 62;
    c.CellHeight        = 50;
    c.RowGap            = 2;
    c.IconsPerLine      = 4;
    c.MaxLinesPerPlayer = 3;
    c.LinesMode         = kLinesFixed;
    c.LinesCap          = kHardMaxLinesPerPlayer;
    c.MaxRows           = 8;
    c.CameoWidth        = 60;
    c.CameoHeight       = 48;
    c.CameoOffsetX      = 1;
    c.CameoOffsetY      = 1;
    c.FlagWidth         = 47;
    c.FlagHeight        = 23;
    c.ToggleWidth       = 10;
    c.ToggleHeight      = 50;

    // art (file names come from rules - see Config.h)
    c.ClockEnabled    = 1;

    // colours
    c.DoneColor              = WatchBarColor { 120, 255, 140 };
    c.ProgressTextColor      = WatchBarColor { 255, 255, 255 };
    c.QueueTextColor         = WatchBarColor { 205, 205, 205 };
    c.CountChipColor         = WatchBarColor { 36, 36, 36 };
    c.CountTextColor         = WatchBarColor { 255, 255, 255 };
    c.IdleTextColor          = WatchBarColor { 130, 130, 130 };
    c.ScrollTrackColor       = WatchBarColor { 40, 40, 40 };
    c.ScrollThumbColor       = WatchBarColor { 185, 185, 185 };
    c.ScrollGlyphActiveColor = WatchBarColor { 200, 200, 200 };
    c.ScrollGlyphIdleColor   = WatchBarColor { 110, 110, 110 };
    c.ScrollGlyphHeldColor   = WatchBarColor { 255, 255, 255 };
    c.ToggleGlyphOnColor     = WatchBarColor { 210, 210, 210 };
    c.ToggleGlyphOffColor    = WatchBarColor { 70, 70, 70 };

    // text (the fonts themselves are fixed in WatchBar.cpp)
    // CSF labels, not literals: the engine's string table is what makes the
    // board read in the game's own language. A value that is not a label is
    // drawn as written (see TextOrCsf in WatchBar.cpp).
    lstrcpynW(c.DoneText, L"TXT_READY", 24);
    lstrcpynW(c.IdleText, L"TXT_WAITING", 48);

    // animation
    c.FadeMs              = 100;
    c.MoveMs              = 90;
    c.ScrollStep          = 1;
    c.ScrollButtonWidth   = 10;
    c.ScrollButtonHeight  = 14;
    c.ScrollButtonOffsetX = 0;
    c.ScrollRepeatDelayMs = 350;
    c.ScrollRepeatRateMs  = 120;
    c.ScanIntervalMs      = 0;

    // content
    c.ShowStructures   = 1;
    c.ShowUnits        = 1;
    c.CountBuilding    = 0;   // off: the board is production + fielded units
    c.CountInfantry    = 1;
    c.CountVehicle     = 1;
    c.CountAircraft    = 1;
    c.ShowCountChip    = 1;
    c.SortMode         = kSortTech;
    for (int i = 0; i < kGroupCount; ++i)
    {
        c.GroupOrder[i] = kDefaultGroupOrder[i];
        // kHardMaxCells is "no cap": the row can never hold more cells than
        // that, so a group capped at it behaves as if uncapped.
        c.MaxIconsPerGroup[i] = kHardMaxCells;
    }
    c.MaxIconsPerGroup[kGroupBuilding] = kDefaultMaxIconsBuilding;

    // gate
    c.SpectatorOnly    = 1;
    c.ParticipantRows  = kRowsOwn;   // own house: no wallhack by default
    c.ShowWhenDefeated = 1;

    // log
    c.LogEnabled = 1;
    c.LogLevel   = kLogInfo;
    c.LogMaxKB   = 1024;
}

// ============================================================================
// settings sources
// ============================================================================

// The section every key lives in. A compile-time constant, never a stack
// buffer: the engine caches the CALLER's section pointer inside the INI object
// and skips the section search when the same pointer comes back, so a stable
// pointer is what makes repeated reads correct as well as fast (the same
// contract ArtSectionOf documents in WatchBar.cpp).
static const char kCfgSection[] = "WatchBar";

static const WatchBarIniSource* s_pSource = nullptr;   // engine source, if any

// When the settings were first read, for the bounded "the section was not there
// yet" retry in ConfigBeginFrame.
static DWORD s_FirstLoadTick = 0;
static const DWORD kSectionRetryMs = 10000;

void ConfigSetIniSource(const WatchBarIniSource* pSource)
{
    s_pSource = pSource;
}

// ------------------------------------------------------------- loose file
// A uimd.ini parsed straight off the disk: what tools\ini_check reads, and the
// fallback when no engine source was installed. The whole file is parsed once
// into a small table, so the per-key lookups below are plain scans of that
// table rather than re-reads of the file.
struct FileEntry
{
    char Section[32];
    char Key[64];
    char Value[200];
};

enum { kFileMaxEntries = 512 };

static FileEntry s_FileEntries[kFileMaxEntries];
static int       s_FileEntryCount = 0;
static bool      s_FileParsed = false;
static bool      s_FileFound = false;

// Strip an inline comment only when it is clearly one: " ;" or " #" after
// whitespace. A value never needs those.
static void StripInlineComment(char* val)
{
    for (char* p = val; *p; ++p)
    {
        if ((*p == ';' || *p == '#') && p > val && (p[-1] == ' ' || p[-1] == '\t'))
        {
            *p = '\0';
            Trim(val);
            return;
        }
    }
}

static void FileParse(WatchBarConfig& c)
{
    s_FileParsed = true;

    static char buf[64 * 1024];
    FILE* f = nullptr;
    if (fopen_s(&f, ConfigIniPath(), "rb") != 0 || !f)
        return;

    const size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = '\0';
    s_FileFound = true;

    char* scan = buf;
    if (len >= 3 && memcmp(scan, "\xEF\xBB\xBF", 3) == 0)
        scan += 3;   // UTF-8 BOM from a Notepad save

    char section[32] = "";
    char* line = scan;
    while (line && *line)
    {
        char* next = strchr(line, '\n');
        if (next)
            *next++ = '\0';

        Trim(line);

        if (line[0] == ';' || line[0] == '#' || line[0] == '\0')
        {
            // comment or blank
        }
        else if (line[0] == '[')
        {
            char* close = strchr(line, ']');
            if (close)
                *close = '\0';
            CopyStr(section, sizeof(section), line + 1);
            Trim(section);
        }
        else
        {
            char* eq = strchr(line, '=');
            if (eq)
            {
                *eq = '\0';
                char* key = line;
                char* val = eq + 1;
                Trim(key);
                Trim(val);
                StripInlineComment(val);

                // Quoted strings keep their content verbatim.
                const size_t vl = strlen(val);
                if (vl >= 2 && val[0] == '"' && val[vl - 1] == '"')
                {
                    val[vl - 1] = '\0';
                    ++val;
                }

                if (*key && *section)
                {
                    if (s_FileEntryCount >= kFileMaxEntries)
                    {
                        ConfigNote(c, "%s holds more than %d keys - the rest of the "
                                      "file is ignored (this reader is for "
                                      "tools\\ini_check; the game reads uimd.ini "
                                      "through the engine and has no such limit)",
                                   ConfigIniPath(), kFileMaxEntries);
                        break;
                    }
                    FileEntry& e = s_FileEntries[s_FileEntryCount++];
                    CopyStr(e.Section, sizeof(e.Section), section);
                    CopyStr(e.Key, sizeof(e.Key), key);
                    CopyStr(e.Value, sizeof(e.Value), val);
                }
            }
        }

        line = next;
    }
}

static int FileRead(const char* section, const char* key, const char* def,
                    char* out, int cap)
{
    for (int i = 0; i < s_FileEntryCount; ++i)
    {
        const FileEntry& e = s_FileEntries[i];
        if (EqNoCase(e.Section, section) && EqNoCase(e.Key, key))
        {
            CopyStr(out, static_cast<size_t>(cap), e.Value);
            return static_cast<int>(strlen(out));
        }
    }
    CopyStr(out, static_cast<size_t>(cap), def ? def : "");
    return 0;
}

static int FileKeyCount(const char* section)
{
    int n = 0;
    for (int i = 0; i < s_FileEntryCount; ++i)
    {
        if (EqNoCase(s_FileEntries[i].Section, section))
            ++n;
    }
    return n;
}

static const char* FileKeyName(const char* section, int index)
{
    int n = 0;
    for (int i = 0; i < s_FileEntryCount; ++i)
    {
        if (!EqNoCase(s_FileEntries[i].Section, section))
            continue;
        if (n == index)
            return s_FileEntries[i].Key;
        ++n;
    }
    return nullptr;
}

static const WatchBarIniSource kFileSource =
{
    "uimd.ini (file)",
    &FileRead,
    &FileKeyCount,
    &FileKeyName
};

// ------------------------------------------------------------- dispatchers
static int IniRead(const char* section, const char* key, const char* def,
                   char* out, int cap)
{
    if (s_pSource)
        return s_pSource->Read(section, key, def, out, cap);
    return FileRead(section, key, def, out, cap);
}

static int IniKeyCount(const char* section)
{
    if (s_pSource)
        return s_pSource->KeyCount(section);
    return FileKeyCount(section);
}

static const char* IniKeyName(const char* section, int index)
{
    if (s_pSource)
        return s_pSource->KeyName(section, index);
    return FileKeyName(section, index);
}

// ------------------------------------------------------------- value helpers
// One token of GroupOrder / MaxIconsPerGroup -> WatchBarIconGroup, or -1 when
// it is not a group name. The names are the ones the other content keys use,
// so the mapping needs no lookup table in the author's head.
static int GroupFromName(const char* s)
{
    if (EqNoCase(s, "production") || EqNoCase(s, "prod")) return kGroupProduction;
    if (EqNoCase(s, "building")   || EqNoCase(s, "buildings")) return kGroupBuilding;
    if (EqNoCase(s, "infantry"))  return kGroupInfantry;
    if (EqNoCase(s, "vehicle")    || EqNoCase(s, "vehicles")) return kGroupVehicle;
    if (EqNoCase(s, "aircraft"))  return kGroupAircraft;
    return -1;
}

struct Ctx
{
    WatchBarConfig& c;
    const char*     key;   // as written, minus the WatchBar. prefix
    const char*     val;
    bool            handled;
};

static void IntKey(Ctx& x, int& dst, int lo, int hi)
{
    const int v = ToInt(x.val, dst);
    int clamped = v;
    if (clamped < lo) clamped = lo;
    if (clamped > hi) clamped = hi;
    if (clamped != v)
        ConfigNote(x.c, kCfgKeyPrefix "%s=%d out of range [%d..%d], using %d",
                   x.key, v, lo, hi, clamped);
    dst = clamped;
    x.handled = true;
}

static void BoolKey(Ctx& x, int& dst)
{
    dst = ToBool(x.val, dst) ? 1 : 0;
    x.handled = true;
}

static void ColorKey(Ctx& x, WatchBarColor& dst)
{
    WatchBarColor parsed;
    int clamped = -1;
    if (ToColor(x.val, parsed, clamped))
    {
        dst = parsed;
        // Same wording as IntKey's clamp note: an out-of-range channel is
        // clamped, never silent. Only the first offending component is named.
        if (clamped >= 0)
            ConfigNote(x.c, kCfgKeyPrefix "%s=%s has a component above 255 (%d), "
                          "clamped to 0..255 - using %d,%d,%d",
                       x.key, x.val, clamped, dst.R, dst.G, dst.B);
    }
    else
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s=\"%s\" is not R,G,B - keeping default",
                   x.key, x.val);
    }
    x.handled = true;
}

// A user-visible string. The value arrives as bytes (the engine hands back what
// the file held), so decode UTF-8 first and fall back to the game's ANSI
// codepage: an author who saves uimd.ini in either encoding gets what they
// typed, and one who writes garbage gets a log line instead of a row of boxes.
static void WideKey(Ctx& x, wchar_t* dst, int cap)
{
    x.handled = true;

    if (!x.val || !*x.val)
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s is empty - keeping default", x.key);
        return;
    }

    wchar_t tmp[160];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, x.val, -1, tmp, 160);
    if (n <= 0)
        n = MultiByteToWideChar(CP_ACP, 0, x.val, -1, tmp, 160);
    if (n <= 0)
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s cannot be decoded - keeping default", x.key);
        return;
    }

    // Control characters would render as font garbage; refuse the whole value.
    for (int i = 0; i < n - 1; ++i)
    {
        if (tmp[i] < 0x20)
        {
            ConfigNote(x.c, kCfgKeyPrefix "%s contains control characters - keeping default",
                       x.key);
            return;
        }
    }

    if (n > cap)
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s is %d characters, the limit is %d - truncated",
                   x.key, n - 1, cap - 1);
        tmp[cap - 1] = L'\0';
    }

    lstrcpynW(dst, tmp, cap);
}

// ------------------------------------------------------------------- parsing

// Keys that no longer exist. They are reported with the reason instead of the
// generic "unrecognised key" line, which would suggest a spelling mistake.
static bool RemovedKey(Ctx& x)
{
    if (EqNoCase(x.key, "MaxIconsPerRow"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was replaced - a row's cell count is now "
                        kCfgKeyPrefix "MaxLinesPerPlayer x " kCfgKeyPrefix
                        "IconsPerLine. This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }

    if (EqNoCase(x.key, "ExpectExeTimestamp") || EqNoCase(x.key, "WarnOnExeMismatch"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - the host check is fixed at "
                        "YR 1.001 gamemd.exe and always reports a mismatch. "
                        "This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }

    if (EqNoCase(x.key, "BucketMaxTypes"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - the board now tallies every "
                        "type exactly, so there is no statistics limit to tune. "
                        "This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }

    // The pager's two buttons are glued together at the middle of the board's
    // bottom edge, so there is no gap between them to tune.
    if (EqNoCase(x.key, "ScrollButtonGap"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - the pager's up and down "
                        "buttons now sit together, centred on the board's bottom "
                        "edge, so nothing is between them. Use "
                        kCfgKeyPrefix "ScrollButtonOffsetX to move the pair. "
                        "This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }

    // The art file names moved into the loaded rules, where the section that
    // owns each picture already lives: the side section for the frames and the
    // toggle strip, the country section for the flag.
    if (EqNoCase(x.key, "FrameArtPattern"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - the frame and strip art is read "
                        "from the side's own section in rulesmd.ini, one key per part: "
                        "WatchBar.CenterPCX / WatchBar.OnPCX / WatchBar.OffPCX. "
                        "This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }
    if (EqNoCase(x.key, "FlagArtPattern"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - the flag is read from the "
                        "country's own section in rulesmd.ini (File.Flag=...). "
                        "This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }
    if (EqNoCase(x.key, "FlagMaxIndex"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - the flag name is no longer "
                        "derived from the country index, so there is no index range "
                        "to bound. This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }
    if (EqNoCase(x.key, "MaxSideArt") || EqNoCase(x.key, "ArtNumberMode")
        || EqNoCase(x.key, "ArtNumberFixed"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - art is no longer numbered. The "
                        "board wears the local player's own side and looks that "
                        "side's section up in rulesmd.ini by name (the engine's own "
                        "side table, so the count is whatever the mod defines). "
                        "This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }

    // Fonts are fixed: GAME.FNT has not been reversed, so Point8 is a ceiling
    // rather than a choice and a font key would only be a way to make the board
    // unreadable.
    if (EqNoCase(x.key, "Font") || EqNoCase(x.key, "FontMain")
        || EqNoCase(x.key, "FontSmall") || EqNoCase(x.key, "FontTiny"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - the three fonts are fixed "
                        "(Point8 for the count badge, Point6Grad for everything "
                        "else) and are not configurable. This line can be deleted.",
                   x.key);
        x.handled = true;
        return true;
    }

    // The panel no longer reads a file it could watch: the settings live in
    // uimd.ini, which the engine loads once at startup.
    if (EqNoCase(x.key, "HotReload") || EqNoCase(x.key, "PollIntervalMs"))
    {
        ConfigNote(x.c, kCfgKeyPrefix "%s was removed - the settings live in uimd.ini, "
                        "which the engine reads once at startup, so there is no file "
                        "to watch: change a value, then restart the game. "
                        "This line can be deleted.", x.key);
        x.handled = true;
        return true;
    }

    return false;
}

static void ApplyKey(Ctx& x)
{
    WatchBarConfig& c = x.c;
    const char* k = x.key;

    if (RemovedKey(x))
        return;

    if      (EqNoCase(k, "PanelX"))          IntKey(x, c.PanelX, -4096, 4096);
    else if (EqNoCase(k, "TopMargin"))       IntKey(x, c.TopMargin, -4096, 4096);
    else if (EqNoCase(k, "TopPercent"))      IntKey(x, c.TopPercent, 0, 100);
    else if (EqNoCase(k, "BandMargin"))      IntKey(x, c.BandMargin, 0, 512);
    else if (EqNoCase(k, "LabelWidth"))      IntKey(x, c.LabelWidth, 0, 512);
    else if (EqNoCase(k, "CellWidth"))       IntKey(x, c.CellWidth, 16, 512);
    else if (EqNoCase(k, "CellHeight"))      IntKey(x, c.CellHeight, 16, 512);
    else if (EqNoCase(k, "RowGap"))          IntKey(x, c.RowGap, 0, 64);
    else if (EqNoCase(k, "IconsPerLine"))    IntKey(x, c.IconsPerLine, 1, kHardMaxIconsPerLine);
    else if (EqNoCase(k, "MaxLinesPerPlayer")) IntKey(x, c.MaxLinesPerPlayer, 1, kHardMaxLinesPerPlayer);
    else if (EqNoCase(k, "LinesCap"))        IntKey(x, c.LinesCap, 1, kHardMaxLinesPerPlayer);
    else if (EqNoCase(k, "MaxRows"))         IntKey(x, c.MaxRows, 1, kHardMaxRows);
    else if (EqNoCase(k, "CameoWidth"))      IntKey(x, c.CameoWidth, 1, 512);
    else if (EqNoCase(k, "CameoHeight"))     IntKey(x, c.CameoHeight, 1, 512);
    else if (EqNoCase(k, "CameoOffsetX"))    IntKey(x, c.CameoOffsetX, -64, 256);
    else if (EqNoCase(k, "CameoOffsetY"))    IntKey(x, c.CameoOffsetY, -64, 256);
    else if (EqNoCase(k, "FlagWidth"))       IntKey(x, c.FlagWidth, 0, 256);
    else if (EqNoCase(k, "FlagHeight"))      IntKey(x, c.FlagHeight, 0, 256);
    else if (EqNoCase(k, "ToggleWidth"))     IntKey(x, c.ToggleWidth, 0, 256);
    else if (EqNoCase(k, "ToggleHeight"))    IntKey(x, c.ToggleHeight, 0, 256);

    else if (EqNoCase(k, "ClockEnabled"))    BoolKey(x, c.ClockEnabled);

    else if (EqNoCase(k, "DoneColor"))              ColorKey(x, c.DoneColor);
    else if (EqNoCase(k, "ProgressTextColor"))      ColorKey(x, c.ProgressTextColor);
    else if (EqNoCase(k, "QueueTextColor"))         ColorKey(x, c.QueueTextColor);
    else if (EqNoCase(k, "CountChipColor"))         ColorKey(x, c.CountChipColor);
    else if (EqNoCase(k, "CountTextColor"))         ColorKey(x, c.CountTextColor);
    else if (EqNoCase(k, "IdleTextColor"))          ColorKey(x, c.IdleTextColor);
    else if (EqNoCase(k, "ScrollTrackColor"))       ColorKey(x, c.ScrollTrackColor);
    else if (EqNoCase(k, "ScrollThumbColor"))       ColorKey(x, c.ScrollThumbColor);
    else if (EqNoCase(k, "ScrollGlyphActiveColor")) ColorKey(x, c.ScrollGlyphActiveColor);
    else if (EqNoCase(k, "ScrollGlyphIdleColor"))   ColorKey(x, c.ScrollGlyphIdleColor);
    else if (EqNoCase(k, "ScrollGlyphHeldColor"))   ColorKey(x, c.ScrollGlyphHeldColor);
    else if (EqNoCase(k, "ToggleGlyphOnColor"))     ColorKey(x, c.ToggleGlyphOnColor);
    else if (EqNoCase(k, "ToggleGlyphOffColor"))    ColorKey(x, c.ToggleGlyphOffColor);

    else if (EqNoCase(k, "DoneText")) WideKey(x, c.DoneText, 24);
    else if (EqNoCase(k, "IdleText")) WideKey(x, c.IdleText, 48);

    else if (EqNoCase(k, "FadeMs"))              IntKey(x, c.FadeMs, 0, 5000);
    else if (EqNoCase(k, "MoveMs"))              IntKey(x, c.MoveMs, 0, 5000);
    else if (EqNoCase(k, "ScrollButtonWidth"))   IntKey(x, c.ScrollButtonWidth, 2, 256);
    else if (EqNoCase(k, "ScrollButtonHeight"))  IntKey(x, c.ScrollButtonHeight, 2, 128);
    else if (EqNoCase(k, "ScrollButtonOffsetX")) IntKey(x, c.ScrollButtonOffsetX, -4096, 4096);
    else if (EqNoCase(k, "ScrollRepeatDelayMs")) IntKey(x, c.ScrollRepeatDelayMs, 0, 10000);
    else if (EqNoCase(k, "ScrollRepeatRateMs"))  IntKey(x, c.ScrollRepeatRateMs, 0, 10000);
    else if (EqNoCase(k, "ScanIntervalMs"))      IntKey(x, c.ScanIntervalMs, 0, 10000);
    else if (EqNoCase(k, "ScrollStep"))
    {
        x.handled = true;
        // A number of lines, or "page" = however many whole lines the band
        // currently shows (so one click moves exactly one screenful).
        if (EqNoCase(x.val, "page") || EqNoCase(x.val, "max") ||
            EqNoCase(x.val, "visible") || EqNoCase(x.val, "0"))
        {
            c.ScrollStep = 0;
        }
        else
        {
            const int v = ToInt(x.val, 1);
            int clamped = v < 1 ? 1 : (v > 64 ? 64 : v);
            if (clamped != v)
                ConfigNote(c, kCfgKeyPrefix "ScrollStep=%d out of range [1..64] "
                              "(or \"page\") - using %d", v, clamped);
            c.ScrollStep = clamped;
        }
    }

    else if (EqNoCase(k, "LinesMode"))
    {
        x.handled = true;

        // Where the per-player line budget comes from: a fixed ini value, or
        // the band's own capacity shared out among the rows being drawn. See
        // WatchBarLinesMode in Config.h. A number is accepted as a shorthand
        // (0..3), because the whole point of the key is that n is derived and
        // the author only picks how much slack to allow.
        if (EqNoCase(x.val, "fixed") || EqNoCase(x.val, "off") ||
            EqNoCase(x.val, "manual") || EqNoCase(x.val, "0"))
            c.LinesMode = kLinesFixed;
        else if (EqNoCase(x.val, "compact") || EqNoCase(x.val, "tight") ||
                 EqNoCase(x.val, "n") || EqNoCase(x.val, "1"))
            c.LinesMode = kLinesCompact;
        else if (EqNoCase(x.val, "loose") || EqNoCase(x.val, "n+1") ||
                 EqNoCase(x.val, "2"))
            c.LinesMode = kLinesLoose;
        else if (EqNoCase(x.val, "ultra") || EqNoCase(x.val, "n+2") ||
                 EqNoCase(x.val, "3"))
            c.LinesMode = kLinesUltra;
        else
            ConfigNote(c, kCfgKeyPrefix "LinesMode=%s is not one of fixed / compact / "
                          "loose / ultra - keeping fixed", x.val);
    }

    else if (EqNoCase(k, "ShowStructures")) BoolKey(x, c.ShowStructures);
    else if (EqNoCase(k, "ShowUnits"))      BoolKey(x, c.ShowUnits);
    else if (EqNoCase(k, "CountBuilding"))  BoolKey(x, c.CountBuilding);
    else if (EqNoCase(k, "CountInfantry"))  BoolKey(x, c.CountInfantry);
    else if (EqNoCase(k, "CountVehicle"))   BoolKey(x, c.CountVehicle);
    else if (EqNoCase(k, "CountAircraft"))  BoolKey(x, c.CountAircraft);
    else if (EqNoCase(k, "MaxIconsPerGroup"))
    {
        x.handled = true;

        // Same comma list as GroupOrder, but a SET of independent entries
        // rather than a permutation: "<group>:<cells>", unlisted groups keep
        // their default. A bad entry is dropped on its own (with a note) -
        // unlike GroupOrder, where one hole makes the whole order ambiguous,
        // here the other entries still mean exactly what they say.
        const char* p = x.val;

        while (*p)
        {
            while (*p == ' ' || *p == '\t' || *p == ',')
                ++p;
            if (!*p)
                break;

            const char* start = p;
            while (*p && *p != ',')
                ++p;

            char token[32];
            size_t len = static_cast<size_t>(p - start);
            if (len >= sizeof(token))
                len = sizeof(token) - 1;
            memcpy(token, start, len);
            token[len] = '\0';
            Trim(token);

            char* colon = strchr(token, ':');
            if (!colon)
            {
                ConfigNote(c, kCfgKeyPrefix "MaxIconsPerGroup: \"%s\" is not "
                              "<group>:<cells> - entry ignored", token);
                continue;
            }

            *colon = '\0';
            char* name = token;
            char* num  = colon + 1;
            Trim(name);
            Trim(num);

            const int group = name[0] ? GroupFromName(name) : -1;
            if (group < 0)
            {
                ConfigNote(c, kCfgKeyPrefix "MaxIconsPerGroup: \"%s\" is not a "
                              "group (production/building/infantry/vehicle/"
                              "aircraft) - entry ignored", name);
                continue;
            }

            const int cells = ToInt(num, -1);
            if (cells < 1 || cells > kHardMaxCells)
            {
                ConfigNote(c, kCfgKeyPrefix "MaxIconsPerGroup %s=%d out of range "
                              "[1..%d] - entry ignored",
                           GroupName(group), cells, kHardMaxCells);
                continue;
            }

            c.MaxIconsPerGroup[group] = cells;
        }
    }
    else if (EqNoCase(k, "ShowCountChip"))  BoolKey(x, c.ShowCountChip);
    else if (EqNoCase(k, "GroupOrder"))
    {
        x.handled = true;

        // A comma-separated permutation of the five group names. The list must
        // name every group exactly once: a partial list would leave the position
        // of the unlisted groups undefined, and the purpose of this key is that
        // the row reads exactly as the author wrote it. Anything wrong keeps the
        // default order and says so.
        int  parsed[kGroupCount];
        int  n = 0;
        bool ok = true;
        const char* p = x.val;

        while (ok && *p)
        {
            while (*p == ' ' || *p == '\t' || *p == ',')
                ++p;
            if (!*p)
                break;

            const char* start = p;
            while (*p && *p != ',')
                ++p;

            char token[24];
            size_t len = static_cast<size_t>(p - start);
            if (len >= sizeof(token))
                len = sizeof(token) - 1;
            memcpy(token, start, len);
            token[len] = '\0';
            Trim(token);

            const int group = token[0] ? GroupFromName(token) : -1;
            if (group < 0)
            {
                ConfigNote(c, kCfgKeyPrefix "GroupOrder: \"%s\" is not a group "
                              "(production/building/infantry/vehicle/aircraft)"
                              " - keeping the default order", token);
                ok = false;
                break;
            }

            for (int i = 0; i < n; ++i)
            {
                if (parsed[i] == group)
                {
                    ConfigNote(c, kCfgKeyPrefix "GroupOrder: \"%s\" listed twice "
                                  "- keeping the default order", token);
                    ok = false;
                    break;
                }
            }
            if (!ok)
                break;

            if (n >= kGroupCount)
            {
                ConfigNote(c, kCfgKeyPrefix "GroupOrder has more than %d entries "
                              "- keeping the default order", kGroupCount);
                ok = false;
                break;
            }

            parsed[n++] = group;
        }

        if (ok && n != kGroupCount)
        {
            ConfigNote(c, kCfgKeyPrefix "GroupOrder names %d of the %d groups "
                          "- keeping the default order", n, kGroupCount);
            ok = false;
        }

        if (ok)
        {
            for (int i = 0; i < kGroupCount; ++i)
                c.GroupOrder[i] = parsed[i];
        }
    }
    else if (EqNoCase(k, "SortMode"))
    {
        x.handled = true;
        if      (EqNoCase(x.val, "tech"))  c.SortMode = kSortTech;
        else if (EqNoCase(x.val, "count")) c.SortMode = kSortCount;
        else if (EqNoCase(x.val, "name"))  c.SortMode = kSortName;
        else ConfigNote(c, kCfgKeyPrefix "SortMode=\"%s\" is not tech/count/name "
                           "- keeping default", x.val);
    }

    else if (EqNoCase(k, "SpectatorOnly"))    BoolKey(x, c.SpectatorOnly);
    else if (EqNoCase(k, "ShowWhenDefeated")) BoolKey(x, c.ShowWhenDefeated);
    else if (EqNoCase(k, "ParticipantRows"))
    {
        x.handled = true;
        if      (EqNoCase(x.val, "own"))    c.ParticipantRows = kRowsOwn;
        else if (EqNoCase(x.val, "allies")) c.ParticipantRows = kRowsAllies;
        else if (EqNoCase(x.val, "all"))    c.ParticipantRows = kRowsAll;
        else ConfigNote(c, kCfgKeyPrefix "ParticipantRows=\"%s\" is not own/allies/all "
                           "- keeping default", x.val);
    }

    else if (EqNoCase(k, "LogEnabled")) BoolKey(x, c.LogEnabled);
    else if (EqNoCase(k, "LogLevel"))   IntKey(x, c.LogLevel, 0, 3);
    else if (EqNoCase(k, "LogMaxKB"))   IntKey(x, c.LogMaxKB, 1, 102400);
    else if (EqNoCase(k, "LogPath"))    { CopyStr(c.LogPath, sizeof(c.LogPath), x.val); x.handled = true; }

    // Free-form notes for the author: accepted and ignored so they never show
    // up as unknown keys. Nothing in the panel reads them.
    else if (EqNoCase(k, "Version") || EqNoCase(k, "Notes"))
    {
        x.handled = true;
    }
}

// The WatchBar. prefix is part of the documented spelling; the bare key is
// accepted too, so a line copied out of docs/config.md cannot be wrong.
static const char* StripPrefix(const char* key)
{
    static const size_t prefixLen = sizeof(kCfgKeyPrefix) - 1;
    if (key && _strnicmp(key, kCfgKeyPrefix, prefixLen) == 0)
        return key + prefixLen;
    return key;
}

// Every key this build understands, WITHOUT the prefix - the ones ApplyKey
// applies and the ones RemovedKey explains. Two things use it:
//
//   - reading values BY NAME. Walking whatever the section happens to carry is
//     enough for a file parsed here, but the engine's key enumeration is an
//     engine detail, and a value missed because an index was off by one would
//     silently produce a wrong board. Naming the keys means a value can only be
//     missed if it is not in this list at all.
//   - distinguishing a typo from a key this build knows, so the two lists do
//     not have to be compared by hand (the self-check in ApplySection reports a
//     key that is listed but unhandled).
static const char* const kKnownKeys[] =
{
    // geometry
    "PanelX", "TopMargin", "TopPercent", "BandMargin", "LabelWidth",
    "CellWidth", "CellHeight", "RowGap", "IconsPerLine", "MaxLinesPerPlayer",
    "LinesMode", "LinesCap",
    "MaxRows", "CameoWidth", "CameoHeight", "CameoOffsetX", "CameoOffsetY",
    "FlagWidth", "FlagHeight", "ToggleWidth", "ToggleHeight",

    // art
    "ClockEnabled",

    // colours
    "DoneColor", "ProgressTextColor", "QueueTextColor", "CountChipColor",
    "CountTextColor", "IdleTextColor", "ScrollTrackColor", "ScrollThumbColor",
    "ScrollGlyphActiveColor", "ScrollGlyphIdleColor", "ScrollGlyphHeldColor",
    "ToggleGlyphOnColor", "ToggleGlyphOffColor",

    // text
    "DoneText", "IdleText",

    // animation / scanning
    "FadeMs", "MoveMs", "ScrollStep", "ScrollButtonWidth", "ScrollButtonHeight",
    "ScrollButtonOffsetX",
    "ScrollRepeatDelayMs", "ScrollRepeatRateMs", "ScanIntervalMs",

    // content
    "ShowStructures", "ShowUnits", "CountBuilding", "CountInfantry",
    "CountVehicle", "CountAircraft", "MaxIconsPerGroup", "GroupOrder",
    "ShowCountChip", "SortMode",

    // gate
    "SpectatorOnly", "ParticipantRows", "ShowWhenDefeated",

    // log
    "LogEnabled", "LogLevel", "LogMaxKB", "LogPath",

    // free-form notes, accepted and ignored
    "Version", "Notes",

    // gone, reported with the reason instead of "unrecognised key"
    "BucketMaxTypes", "FrameArtPattern", "FlagArtPattern", "FlagMaxIndex",
    "MaxSideArt", "ArtNumberMode", "ArtNumberFixed",
    "Font", "FontMain", "FontSmall", "FontTiny",
    "HotReload", "PollIntervalMs", "MaxIconsPerRow",
    "ExpectExeTimestamp", "WarnOnExeMismatch", "ScrollButtonGap",
};

static bool IsKnownKey(const char* bare)
{
    for (const char* pKnown : kKnownKeys)
    {
        if (EqNoCase(bare, pKnown))
            return true;
    }
    return false;
}

// Read one key, prefixed first and bare second. Returns the value, or an empty
// string when the section carries neither spelling.
static void ReadKeyValue(const char* bare, char* out, int cap)
{
    char full[64];
    _snprintf(full, sizeof(full), kCfgKeyPrefix "%s", bare);
    full[sizeof(full) - 1] = '\0';

    if (IniRead(kCfgSection, full, "", out, cap) > 0 && out[0])
        return;
    if (IniRead(kCfgSection, bare, "", out, cap) > 0 && out[0])
        return;
    out[0] = '\0';
}

static void ApplySection(WatchBarConfig& c)
{
    // How many keys the section carries at all: 0 means the author never
    // configured the panel (or misspelled the section name), which the log
    // reports as "built-in defaults" rather than as a board ignoring its ini.
    c.SectionFound = IniKeyCount(kCfgSection);

    // 1. Every known key, read by name.
    for (const char* pKey : kKnownKeys)
    {
        char val[256];
        ReadKeyValue(pKey, val, sizeof(val));
        if (!val[0])
            continue;

        Ctx x { c, pKey, val, false };
        ApplyKey(x);
        if (!x.handled)
        {
            // Listed above but not applied: the two halves of this file have
            // drifted apart. Loud, because it is a bug in the panel, not in the
            // author's ini.
            ConfigNote(c, "internal: " kCfgKeyPrefix "%s is listed but not handled "
                          "- please report this", pKey);
        }
    }

    // 2. Whatever else the section carries is a typo (or a key from a future
    //    build). Reported with the spelling the author used, so it can be found
    //    in the file as-is.
    for (int i = 0; i < c.SectionFound; ++i)
    {
        const char* pName = IniKeyName(kCfgSection, i);
        if (!pName || !*pName)
            continue;
        if (!IsKnownKey(StripPrefix(pName)))
            NoteUnknown(c, pName);
    }
}

// A file that still carries the old per-section layout (an author who pasted
// their WatchBar.ini into uimd.ini) would otherwise report every line as an
// unknown key without saying why. One line explains the whole file instead.
//
// The list is every section the old file had, not just the ones whose keys this
// build still recognises: the point is to name the reason for a whole block of
// ignored lines, and an author who pasted [Text] or [Anim] deserves the same
// answer as one who pasted [Layout]. The first section found is reported and
// the rest are left alone - one explanation is enough.
static void LegacySectionNote(WatchBarConfig& c)
{
    static const char* const kOldSections[] = {
        "Layout", "Colors", "Text", "Anim", "Content", "Gate", "Log", "Compat",
        "Art", "Behavior", "Info", "Font",
    };

    for (const char* pOld : kOldSections)
    {
        if (IniKeyCount(pOld) > 0)
        {
            ConfigNote(c, "the old [%s] section is not read any more: every setting "
                          "now lives in [WatchBar] as WatchBar.<key> - see "
                          "docs/config.md (and docs\\uimd-sample.ini for the whole "
                          "block).", pOld);
            return;
        }
    }
}

static void Validate(WatchBarConfig& c)
{
    // The move must land before the dissolve finishes, otherwise a replacement
    // icon is still gliding when its ghost is gone. The original invariant was
    // "MOVE_MS < FADE_MS"; a hand-edited ini is exactly where that breaks.
    //
    // Both keys may be 0, which switches that animation off (instant fade /
    // instant snap) - see GlideProgress and FadeAlpha in WatchBar.cpp, which
    // are what keep the zero divisor out of the render path. A 0 FadeMs has no
    // dissolve left to race, so only a positive FadeMs constrains MoveMs.
    if (c.FadeMs > 0 && c.MoveMs >= c.FadeMs)
    {
        const int fixed = c.FadeMs > 1 ? c.FadeMs - 1 : 0;
        ConfigNote(c, kCfgKeyPrefix "MoveMs=%d must stay below " kCfgKeyPrefix
                      "FadeMs=%d - using %d", c.MoveMs, c.FadeMs, fixed);
        c.MoveMs = fixed;
    }

    // ---- cross-key sanity
    //
    // Keys that are silently inert are the ones an author never finds: the
    // combination is legal, it just does nothing. Each case below is reported
    // once per load (the log and tools\check_ini.bat both show these lines)
    // instead of being left for a debugging session.

    // The adaptive line modes take the budget from the band and the row count;
    // MaxLinesPerPlayer is the FIXED budget and is not read then. Without this
    // line, an author who writes LinesMode=compact next to MaxLinesPerPlayer=6
    // would see a 6-line budget silently ignored.
    if (c.LinesMode != kLinesFixed)
    {
        ConfigNote(c, kCfgKeyPrefix "LinesMode=%s ignores " kCfgKeyPrefix
                      "MaxLinesPerPlayer=%d (%d cells) - the band sets the budget, "
                      kCfgKeyPrefix "LinesCap=%d is the ceiling",
                   LinesModeName(c.LinesMode), c.MaxLinesPerPlayer, CfgRowCells(c),
                   c.LinesCap);
    }

    if (!c.ShowUnits && (c.CountInfantry || c.CountVehicle || c.CountAircraft))
    {
        ConfigNote(c, kCfgKeyPrefix "ShowUnits=0 hides every unit group, so "
                      kCfgKeyPrefix "CountInfantry/CountVehicle/CountAircraft "
                      "have no effect");
    }

    if (!c.CountBuilding && c.MaxIconsPerGroup[kGroupBuilding] != kDefaultMaxIconsBuilding)
    {
        ConfigNote(c, kCfgKeyPrefix "MaxIconsPerGroup building:%d has no effect "
                      "while " kCfgKeyPrefix "CountBuilding=0 (the building group "
                      "is off)", c.MaxIconsPerGroup[kGroupBuilding]);
    }

    // A customised icon order is echoed once per load: the row's layout is the
    // first thing an author checks when an icon seems to be missing, and with a
    // custom order it is the LAST group listed that gets cut first.
    bool defaultOrder = true;
    for (int i = 0; i < kGroupCount && defaultOrder; ++i)
        defaultOrder = c.GroupOrder[i] == kDefaultGroupOrder[i];

    if (!defaultOrder)
    {
        char line[96] = "";
        for (int i = 0; i < kGroupCount; ++i)
        {
            if (i)
                lstrcatA(line, ",");
            lstrcatA(line, GroupName(c.GroupOrder[i]));
        }
        ConfigNote(c, kCfgKeyPrefix "GroupOrder = %s (the last group is cut first)", line);
    }
}

// --------------------------------------------------------------------- load
void ConfigLoad()
{
    WatchBarConfig& c = g_WatchBarCfg;
    SetDefaults(c);
    c.Loaded = true;
    c.ExeTimestamp = ReadExeTimestamp();

    // Which source do we have? The engine's own uimd.ini when the DLL installed
    // one, otherwise a loose uimd.ini beside the exe.
    if (s_pSource)
    {
        c.Source     = kCfgSourceEngine;
        c.SourceName = s_pSource->Name;
    }
    else
    {
        if (!s_FileParsed)
            FileParse(c);
        if (s_FileFound)
        {
            c.Source     = kCfgSourceFile;
            c.SourceName = kFileSource.Name;
        }
        else
        {
            c.Source     = kCfgSourceNone;
            c.SourceName = "none";
        }
    }

    if (c.Source != kCfgSourceNone)
    {
        ApplySection(c);
        LegacySectionNote(c);
    }

    Validate(c);

    // Default the log next to the executable: the game's working directory is
    // not guaranteed, so a relative path would put the log somewhere unexpected.
    if (!c.LogPath[0])
        BuildPath(c.LogPath, sizeof(c.LogPath), "WatchBar.log");
}

void ConfigBeginFrame()
{
    WatchBarConfig& c = g_WatchBarCfg;

    if (!c.Loaded)
    {
        ConfigLoad();
        s_FirstLoadTick = GetTickCount();
        return;
    }

    // Insurance for the one assumption this file cannot verify by itself: that
    // the engine already has uimd.ini in memory when the first frame asks for
    // it. While [WatchBar] is still missing, look again for a few seconds - a
    // mod that simply configures nothing retries too, but only for that bounded
    // window, so the cost is one section lookup per frame and then nothing.
    if (c.SectionFound || c.Source == kCfgSourceNone)
        return;
    if (GetTickCount() - s_FirstLoadTick > kSectionRetryMs)
        return;
    if (IniKeyCount(kCfgSection) <= 0)
        return;

    ConfigLoad();
    ConfigNote(c, "the [WatchBar] section showed up after the first read - "
                  "settings reloaded");
}
