@echo off
REM ===========================================================================
REM  Check the [WatchBar] section of a uimd.ini without launching the game.
REM
REM  Usage:
REM    tools\check_ini.bat "D:\Game\Extracted\uimd.ini"
REM
REM  Builds the checker on first use, then runs it against a staged copy of the
REM  file. Reports the effective values plus every typo / clamp the parser found.
REM  Exit code 1 means there were warnings.
REM
REM  The panel reads its settings out of the engine's own uimd.ini, which is
REM  usually packed inside a MIX: point this at the copy you extracted/edited
REM  (XCC Mixer -> extract uimd.ini -> edit -> put it back).
REM
REM  NOTE: keep this file ASCII-only (cmd reads .bat as the OEM codepage).
REM ===========================================================================
setlocal
set "SCRIPTDIR=%~dp0"
pushd "%SCRIPTDIR%"

set "INI=%~1"
if not defined INI (
    echo Usage: tools\check_ini.bat "path\to\uimd.ini"
    echo.
    echo The panel's settings live in uimd.ini, section [WatchBar].
    echo Point this at the uimd.ini you are editing - usually the copy you
    echo extracted from the mod's MIX with XCC Mixer.
    popd
    exit /b 2
)
if not exist "%INI%" (
    echo [ERROR] ini not found: %INI%
    popd
    exit /b 2
)

set "STAGE=%SCRIPTDIR%_ini_check"
if not exist "%STAGE%" mkdir "%STAGE%"
copy /y "%INI%" "%STAGE%\uimd.ini" >nul

REM Always rebuild. A stale checker is worse than a slow one: it would report
REM keys the DLL understands as typos, and the whole point of this tool is to
REM be the source of truth about the ini. Compiling three small files takes a
REM couple of seconds.
goto :build

:build
REM Toolchain detection lives OUTSIDE any parenthesised block: a `set` inside
REM one is not visible to the same block without delayed expansion.
set "VCVARS="
for %%E in (BuildTools Community Professional Enterprise) do (
    if not defined VCVARS (
        if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars32.bat" (
            set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars32.bat"
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
    echo [ERROR] vcvars32.bat not found - install the VS2022 C++ toolchain.
    popd
    exit /b 2
)
call "%VCVARS%" >nul 2>&1

REM Build from inside the staging folder: /Fo with a directory needs a trailing
REM backslash, and a quoted "dir\" would be parsed as an escaped quote.
pushd "%STAGE%"
cl /nologo /EHsc /std:c++20 /O2 /MT /DNOMINMAX /I "..\..\src" ^
   "..\ini_check.cpp" "..\..\src\Config.cpp" ^
   /Fe:ini_check.exe /Fo:.\ >nul
set "BUILDRC=%ERRORLEVEL%"
popd
if not "%BUILDRC%"=="0" (
    echo [ERROR] checker failed to build.
    popd
    exit /b 1
)

:run
"%STAGE%\ini_check.exe"
set "RC=%ERRORLEVEL%"

popd
endlocal & exit /b %RC%
