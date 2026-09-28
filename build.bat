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

REM /std:c++20  YRpp uses is_const_v / concepts / inline variables.
REM /DNOMINMAX  windows.h min/max macros break YRpp's YRMath.h templates.
REM /DSYR_VER=2  emits the .syhks00 section. WITHOUT THIS THE HOOKS SILENTLY
REM              DO NOTHING - the DLL loads but never fires. The self-check
REM              below fails the build if it is missing.
REM Only /I YRpp: adding /I YRpp\Helpers makes CRT <string.h> resolve to
REM              Helpers/String.h and breaks cstring compilation.
cl /nologo /LD /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DSYR_VER=2 ^
   /D "WIN32" /D "_WINDOWS" /D "_USRDLL" /D "_CRT_SECURE_NO_WARNINGS" ^
   /I YRpp ^
   src\WatchBar.cpp src\Config.cpp ^
   /Fe:WatchBar.dll /Fo:obj\ /Fd:obj\vc.pdb ^
   /link /DLL /INCREMENTAL:NO ^
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
echo Self-check: the .syhks00 section must exist or Syringe will not hook.
objdump -h WatchBar.dll 2>nul | findstr syhks
if errorlevel 1 (
    echo [FAILED] .syhks00 section NOT found - the hooks would be silent.
    echo          Check that /DSYR_VER=2 is still on the compile line.
    popd
    exit /b 1
)

echo [OK] Hook declaration section present.

popd
endlocal
