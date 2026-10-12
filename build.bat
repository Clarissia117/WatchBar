@echo off
REM ===========================================================================
REM  Build WatchBar.dll - in-game spectator board for Yuri's Revenge.
REM
REM  Usage:
REM    build.bat                 build with the default toolchain search
REM    build.bat --vcvars "C:\...\vcvars32.bat"   use a specific toolchain
REM
REM  Requires the MSVC x86 toolchain (VS2022 Build Tools is enough).
REM
REM  NOTE: keep this file ASCII-only. cmd.exe reads .bat as the OEM codepage
REM  (936 here), so non-ASCII text corrupts batch parsing.
REM ===========================================================================
setlocal
set "SCRIPTDIR=%~dp0"
pushd "%SCRIPTDIR%"

set "VCVARS="
if /i "%~1"=="--vcvars" set "VCVARS=%~2"

REM --- locate vcvars32.bat ---------------------------------------------------
REM Checked in order: explicit argument, the usual BuildTools / Community /
REM Professional / Enterprise installs. A wrong toolchain produces a DLL that
REM builds and then silently fails to hook, so failing here is the friendly
REM outcome.
if not defined VCVARS (
    for %%E in (BuildTools Community Professional Enterprise) do (
        if not defined VCVARS (
            if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars32.bat" (
                set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars32.bat"
            )
        )
    )
)
if not defined VCVARS (
    for %%E in (BuildTools Community Professional Enterprise) do (
        if not defined VCVARS (
            if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars32.bat" (
                set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars32.bat"
            )
        )
    )
)

if not defined VCVARS (
    echo [ERROR] vcvars32.bat not found.
    echo         Install the "Desktop development with C++" workload of
    echo         Visual Studio 2022 Build Tools, or pass the path:
    echo             build.bat --vcvars "C:\path\to\vcvars32.bat"
    popd
    exit /b 2
)

echo Toolchain: %VCVARS%
call "%VCVARS%" >nul 2>&1

if not exist obj mkdir obj

REM --- resources -------------------------------------------------------------
REM VERSIONINFO + manifest from src\WatchBar.rc. The version is read back out of
REM Config.h, so the file properties and the in-game log banner cannot drift
REM apart. The manifest is embedded as resource id 2 (the DLL one).
set "WBVER="
for /f "tokens=3" %%v in ('findstr /c:"#define WATCHBAR_VERSION" src\Config.h') do set "WBVER=%%~v"
if not defined WBVER (
    echo [FAILED] could not read WATCHBAR_VERSION from src\Config.h.
    popd
    exit /b 1
)
set "WBVERC=%WBVER:.=,%"
set "WBVERC=%WBVERC%,0"
echo Resources: version %WBVER%

REM The manifest is plain XML, so its version is the one copy that can silently
REM drift from Config.h - checked here.
findstr /c:"%WBVER%.0" src\WatchBar.manifest >nul
if errorlevel 1 (
    echo [FAILED] src\WatchBar.manifest does not carry version %WBVER%.0.
    popd
    exit /b 1
)

rc /nologo /dWB_VER_STR=\"%WBVER%\" /dWB_VER_NUM=%WBVERC% /i src /fo obj\WatchBar.res src\WatchBar.rc
if errorlevel 1 (
    echo [FAILED] resource compilation did not succeed.
    popd
    exit /b 1
)

REM /std:c++20  YRpp uses is_const_v / concepts / inline variables.
REM /DNOMINMAX  windows.h min/max macros break YRpp's YRMath.h templates.
REM /DSYR_VER=2  emits the .syhks00 section. WITHOUT THIS THE HOOKS SILENTLY
REM              DO NOTHING - the DLL loads but never fires. The self-check
REM              below fails the build if it is missing.
REM /GS /guard:cf /SAFESEH  hardening; inert in the game (CFG checks indirect
REM              calls only when the process opts in, and gamemd.exe does not).
REM /RELEASE /PDBALTPATH  a real checksum in the header, and no developer path
REM              baked into the binary.
REM Only /I YRpp: adding /I YRpp\Helpers makes CRT <string.h> resolve to
REM              Helpers/String.h and breaks cstring compilation.
cl /nologo /LD /std:c++20 /EHsc /O2 /MT /GS /guard:cf /DNOMINMAX /DSYR_VER=2 ^
   /D "WIN32" /D "_WINDOWS" /D "_USRDLL" /D "_CRT_SECURE_NO_WARNINGS" ^
   /I YRpp ^
   src\WatchBar.cpp src\Config.cpp ^
   /Fe:WatchBar.dll /Fo:obj\ /Fd:obj\vc.pdb ^
   /link /DLL /INCREMENTAL:NO /SAFESEH /DYNAMICBASE /NXCOMPAT /RELEASE ^
   /MANIFEST:NO /PDBALTPATH:%%_PDB%% ^
   obj\WatchBar.res ^
   /EXPORT:GScreenClass_DrawOnTop_WatchBar ^
   /EXPORT:CommandClassCallback_Register_WatchBar

if errorlevel 1 (
    echo.
    echo [FAILED] Compilation did not succeed.
    popd
    exit /b 1
)

echo.
echo [OK] WatchBar.dll built.
echo.
echo Self-check: the .syhks00 section must exist or Syringe will not hook,
echo             and the version resource must be linked in.
REM Checked with findstr, not objdump: objdump is not part of the MSVC
REM toolchain, and both names are plain ASCII in the PE header.
findstr /c:".syhks00" WatchBar.dll >nul
if errorlevel 1 (
    echo [FAILED] .syhks00 section NOT found - the hooks would be silent.
    echo          Check that /DSYR_VER=2 is still on the compile line.
    popd
    exit /b 1
)
findstr /c:".rsrc" WatchBar.dll >nul
if errorlevel 1 (
    echo [FAILED] no .rsrc section - src\WatchBar.rc was not linked in.
    popd
    exit /b 1
)

echo [OK] Hook declaration section and version resource present.

popd
endlocal
