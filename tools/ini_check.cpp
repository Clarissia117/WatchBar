// ============================================================================
// WatchBar - uimd.ini [WatchBar] checker
// ----------------------------------------------------------------------------
// Parses a uimd.ini with the SAME code the DLL uses (src/Config.cpp) and prints
// the resulting values plus every complaint the parser has.
//
// Why it exists: tuning by launching the game is slow. This turns "did I spell
// that key right / is that colour valid / is MoveMs below FadeMs" into a
// one-second console answer.
//
// The DLL reads the settings out of the engine's already-loaded uimd.ini; this
// tool has no engine, so it reads a loose uimd.ini from its own folder - which
// is why check_ini.bat stages the target file as uimd.ini next to this exe.
//
// Exit code: 0 = clean, 1 = warnings (unknown keys / clamped values).
// ============================================================================
#include <stdio.h>
#include "Config.h"

static const char* SortName(int id)
{
    switch (id)
    {
    case kSortCount: return "count";
    case kSortName:  return "name";
    default:         return "tech";
    }
}

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

static const char* ParticipantRowsName(int mode)
{
    switch (mode)
    {
    case kRowsAllies: return "allies";
    case kRowsAll:    return "all";
    default:          return "own";
    }
}

// What an adaptive line budget works out to, at the resolutions the game runs
// at. Offline, so it uses the same arithmetic the DLL does (CfgVisLines /
// CfgAllocLines in Config.h) with a view height equal to the screen height -
// which is what ViewBounds is, the sidebar sitting to the RIGHT of it.
//
// The table answers the only question an author has about this feature: "with
// N players, how many lines does each one get?" Every row is assumed to want
// the maximum, which is the worst case; a row that wants fewer frees its spare
// lines for the others.
static void PrintLineBudgetTable(const WatchBarConfig& c)
{
    static const int kHeights[] = { 600, 720, 768, 900, 960, 1080, 1440 };

    printf("\n  lines per player by view height (IconsPerLine=%d, cap=%d):\n",
           c.IconsPerLine, c.LinesCap);
    printf("    viewH  :");
    for (int h : kHeights)
        printf("%6d", h);
    printf("\n    visLine:");
    for (int h : kHeights)
        printf("%6d", CfgVisLines(c, h));
    printf("\n");

    for (int rows = 1; rows <= kHardMaxRows; ++rows)
    {
        printf("    %2d ply :", rows);
        for (int h : kHeights)
        {
            int need[kHardMaxRows];
            int out[kHardMaxRows];
            for (int i = 0; i < rows; ++i)
                need[i] = kHardMaxLinesPerPlayer;
            CfgAllocLines(c, need, rows, CfgVisLines(c, h), out);
            printf("%6d", out[0]);
        }
        printf("\n");
    }

    printf("  (all rows assumed to want the maximum, so rows may differ by one line\n"
           "   when the band does not divide evenly; a row wanting fewer frees its\n"
           "   lines for the others. compact cannot scroll while players <= visLine,\n"
           "   and every row wanting more than its share still gets at least\n"
           "   visLine / players.)\n");
}

// Wide -> the console's ANSI codepage, so a CJK WatchBar.DoneText value prints
// as itself in a cp936 console instead of as question marks.
static const char* Narrow(const wchar_t* w, char* buf, int cap)
{
    buf[0] = '\0';
    if (!w || !*w)
        return buf;
    if (WideCharToMultiByte(CP_ACP, 0, w, -1, buf, cap, nullptr, nullptr) <= 0)
    {
        lstrcpynA(buf, "<cannot display>", cap);
    }
    return buf;
}

int main()
{
    ConfigLoad();
    const WatchBarConfig& c = g_WatchBarCfg;

    printf("WatchBar %s\n", WATCHBAR_VERSION);
    printf("ini     : %s\n", ConfigIniPath());
    if (c.Source == kCfgSourceNone)
        printf("          NOT FOUND - every value below is the built-in default.\n");
    else if (!c.SectionFound)
        printf("          no [WatchBar] section - every value below is the built-in\n"
               "          default. Paste the block from docs\\uimd-sample.ini.\n");
    else
        printf("source  : %s, %d key(s) in [WatchBar]\n", c.SourceName, c.SectionFound);

    printf("\n-- geometry --\n");
    printf("WatchBar.PanelX=%d  WatchBar.TopMargin=%d  WatchBar.TopPercent=%d%%  "
           "WatchBar.BandMargin=%d\n", c.PanelX, c.TopMargin, c.TopPercent, c.BandMargin);
    printf("WatchBar.LabelWidth=%d  WatchBar.CellWidth=%d  WatchBar.CellHeight=%d  "
           "WatchBar.RowGap=%d\n",
           c.LabelWidth, c.CellWidth, c.CellHeight, c.RowGap);
    printf("WatchBar.IconsPerLine=%d (hard cap %d)  WatchBar.MaxLinesPerPlayer=%d "
           "(hard cap %d)  -> %d cells per player\n",
           c.IconsPerLine, kHardMaxIconsPerLine, c.MaxLinesPerPlayer,
           kHardMaxLinesPerPlayer, CfgRowCells(c));
    printf("WatchBar.LinesMode=%s  WatchBar.LinesCap=%d (hard cap %d)%s\n",
           LinesModeName(c.LinesMode), c.LinesCap, kHardMaxLinesPerPlayer,
           c.LinesMode == kLinesFixed
               ? "   <- fixed: MaxLinesPerPlayer above is the budget"
               : "   <- adaptive: the band and the player count decide");
    if (c.LinesMode != kLinesFixed)
        PrintLineBudgetTable(c);
    printf("WatchBar.MaxRows=%d (hard cap %d)\n", c.MaxRows, kHardMaxRows);
    printf("WatchBar.Cameo=%dx%d @ %d,%d  WatchBar.Flag=%dx%d  WatchBar.Toggle=%dx%d\n",
           c.CameoWidth, c.CameoHeight, c.CameoOffsetX, c.CameoOffsetY,
           c.FlagWidth, c.FlagHeight, c.ToggleWidth, c.ToggleHeight);

    printf("\n-- art --\n");
    printf("WatchBar.ClockEnabled=%d\n", c.ClockEnabled);
    printf("  (file names are not settings: the side's rules section carries\n"
           "   WatchBar.CenterPCX / OnPCX / OffPCX / UpPCX / DownPCX, the country's\n"
           "   carries File.Flag)\n");

    printf("\n-- colours --\n");
    printf("WatchBar.DoneColor=%d,%d,%d  WatchBar.ProgressTextColor=%d,%d,%d  "
           "WatchBar.QueueTextColor=%d,%d,%d\n",
           c.DoneColor.R, c.DoneColor.G, c.DoneColor.B,
           c.ProgressTextColor.R, c.ProgressTextColor.G, c.ProgressTextColor.B,
           c.QueueTextColor.R, c.QueueTextColor.G, c.QueueTextColor.B);
    printf("WatchBar.CountChipColor=%d,%d,%d  WatchBar.CountTextColor=%d,%d,%d  "
           "WatchBar.IdleTextColor=%d,%d,%d\n",
           c.CountChipColor.R, c.CountChipColor.G, c.CountChipColor.B,
           c.CountTextColor.R, c.CountTextColor.G, c.CountTextColor.B,
           c.IdleTextColor.R, c.IdleTextColor.G, c.IdleTextColor.B);
    printf("WatchBar.ScrollTrackColor=%d,%d,%d  WatchBar.ScrollThumbColor=%d,%d,%d\n",
           c.ScrollTrackColor.R, c.ScrollTrackColor.G, c.ScrollTrackColor.B,
           c.ScrollThumbColor.R, c.ScrollThumbColor.G, c.ScrollThumbColor.B);
    printf("WatchBar.ScrollGlyphActiveColor=%d,%d,%d  ScrollGlyphIdleColor=%d,%d,%d  "
           "ScrollGlyphHeldColor=%d,%d,%d\n",
           c.ScrollGlyphActiveColor.R, c.ScrollGlyphActiveColor.G, c.ScrollGlyphActiveColor.B,
           c.ScrollGlyphIdleColor.R, c.ScrollGlyphIdleColor.G, c.ScrollGlyphIdleColor.B,
           c.ScrollGlyphHeldColor.R, c.ScrollGlyphHeldColor.G, c.ScrollGlyphHeldColor.B);
    printf("WatchBar.ToggleGlyphOnColor=%d,%d,%d  WatchBar.ToggleGlyphOffColor=%d,%d,%d\n",
           c.ToggleGlyphOnColor.R, c.ToggleGlyphOnColor.G, c.ToggleGlyphOnColor.B,
           c.ToggleGlyphOffColor.R, c.ToggleGlyphOffColor.G, c.ToggleGlyphOffColor.B);

    char done[96], idle[160];
    printf("\n-- text (fonts are fixed: Point8 badge / Point6Grad rest) --\n");
    printf("WatchBar.DoneText=\"%s\"  WatchBar.IdleText=\"%s\"\n",
           Narrow(c.DoneText, done, sizeof(done)),
           Narrow(c.IdleText, idle, sizeof(idle)));

    if (c.ScrollStep <= 0)
        printf("\n-- animation --\nWatchBar.FadeMs=%d  WatchBar.MoveMs=%d  "
               "WatchBar.ScrollStep=page (one screenful)\n", c.FadeMs, c.MoveMs);
    else
        printf("\n-- animation --\nWatchBar.FadeMs=%d  WatchBar.MoveMs=%d  "
               "WatchBar.ScrollStep=%d line(s)\n", c.FadeMs, c.MoveMs, c.ScrollStep);
    printf("WatchBar.ScrollButton=%dx%d  WatchBar.ScrollRepeat=%dms after %dms  "
           "WatchBar.ScanIntervalMs=%d\n",
           c.ScrollButtonWidth, c.ScrollButtonHeight,
           c.ScrollRepeatRateMs, c.ScrollRepeatDelayMs, c.ScanIntervalMs);
    printf("WatchBar.ScrollButtonOffsetX=%d  (0 = the glued pair is centred on "
           "the board's occupied width)\n", c.ScrollButtonOffsetX);

    printf("\n-- content --\n");
    printf("WatchBar.ShowStructures=%d  WatchBar.ShowUnits=%d  WatchBar.ShowCountChip=%d  "
           "WatchBar.SortMode=%s\n",
           c.ShowStructures, c.ShowUnits, c.ShowCountChip, SortName(c.SortMode));
    printf("count: WatchBar.CountBuilding=%d  CountInfantry=%d  CountVehicle=%d  "
           "CountAircraft=%d\n",
           c.CountBuilding, c.CountInfantry, c.CountVehicle, c.CountAircraft);
    printf("WatchBar.GroupOrder:");
    for (int i = 0; i < kGroupCount; ++i)
        printf("%s%s", i ? " > " : " ", GroupName(c.GroupOrder[i]));
    printf("\nWatchBar.MaxIconsPerGroup:");
    for (int i = 0; i < kGroupCount; ++i)
    {
        const int cells = c.MaxIconsPerGroup[c.GroupOrder[i]];
        if (cells >= kHardMaxCells)
            printf(" %s=none", GroupName(c.GroupOrder[i]));
        else
            printf(" %s=%d", GroupName(c.GroupOrder[i]), cells);
    }
    printf("\n  (every tallied type is counted; caps only limit cells)\n");

    printf("\n-- gate --\n");
    printf("WatchBar.SpectatorOnly=%d  WatchBar.ShowWhenDefeated=%d\n",
           c.SpectatorOnly, c.ShowWhenDefeated);
    printf("WatchBar.ParticipantRows=%s%s\n",
           ParticipantRowsName(c.ParticipantRows),
           c.SpectatorOnly
               ? "   <- participants see no board, so this is not read"
               : "   <- participants see the board: this is who they see");
    printf("  (a spectator always sees every combatant; ParticipantRows only ever\n"
           "   narrows what a player who is actually fighting gets to see)\n");

    printf("\n-- log --\n");
    printf("WatchBar.LogEnabled=%d  WatchBar.LogLevel=%d  WatchBar.LogMaxKB=%d\n"
           "WatchBar.LogPath=%s\n",
           c.LogEnabled, c.LogLevel, c.LogMaxKB, c.LogPath);

    int problems = 0;

    if (c.MessageCount)
    {
        printf("\n--- parser notes (%d) ---\n", c.MessageCount);
        for (int i = 0; i < c.MessageCount; ++i)
            printf("  %s\n", c.Messages[i]);
        problems += c.MessageCount;
    }

    if (c.UnknownKeyCount)
    {
        printf("\n--- unrecognised keys (%d), check the spelling ---\n", c.UnknownKeyCount);
        for (int i = 0; i < c.UnknownKeyCount; ++i)
            printf("  %s\n", c.UnknownKeys[i]);
        problems += c.UnknownKeyCount;
    }

    printf("\n%s\n", problems ? "RESULT: warnings above - the DLL will still run."
                              : "RESULT: clean.");
    return problems ? 1 : 0;
}
