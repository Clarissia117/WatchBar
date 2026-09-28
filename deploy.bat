@echo off
REM ===========================================================================
REM  Deploy WatchBar.dll into a YR mod folder and wire up Syringe.
REM
REM  Usage:
REM    deploy.bat --gamedir "D:\Path"                  dry run - shows the plan
REM    deploy.bat --gamedir "D:\Path" --apply          actually write changes
REM    deploy.bat --gamedir "D:\Path" --apply --sample also append the
REM                                                    [WatchBar] block to the
REM                                                    game's uimd.ini
REM
REM  --gamedir is REQUIRED: the script carries no default path, so it cannot
REM  write into someone else's install by accident.
REM
REM  Dry run is the default on purpose: this edits a game file.
REM  The script backs up ClientDefinitions.ini and is idempotent.
REM
REM  The panel ships NO ini of its own: its settings live in the game's uimd.ini,
REM  section [WatchBar], and every one of them has a built-in default. Appending
REM  the block is therefore opt-in (--sample) - it only puts the keys on disk so
REM  they can be edited. A uimd.ini packed inside a MIX cannot be touched from
REM  here; the script says what to do instead.
REM
REM  NOTE: keep this file ASCII-only. cmd.exe reads .bat as the OEM codepage
REM  (936 here), so non-ASCII text corrupts batch parsing.
REM ===========================================================================
setlocal

REM Capture the script directory before anything else. `shift` also shifts %0,
REM and a plain `goto :label` cannot cross into the second half of this file
REM reliably once we have cd'd, so all argument handling lives in one block.
set "SCRIPTDIR=%~dp0"
pushd "%SCRIPTDIR%"

set "GAMEDIR="
set "DLL=WatchBar.dll"
set "SAMPLE=docs\uimd-sample.ini"
set "APPLY=0"
set "ADD_SAMPLE=0"

REM Arguments are parsed in a loop so their order does not matter
REM (deploy.bat --gamedir X --sample --apply works as well as
REM  deploy.bat --apply --sample --gamedir X).
:args
if "%~1"=="" goto :args_done
if /i "%~1"=="--apply"   ( set "APPLY=1" & shift & goto :args )
if /i "%~1"=="--sample"  ( set "ADD_SAMPLE=1" & shift & goto :args )
if /i "%~1"=="--gamedir" ( set "GAMEDIR=%~2" & shift & shift & goto :args )
if /i "%~1"=="--dll"     ( set "DLL=%~2" & shift & shift & goto :args )
echo [WARNING] unknown option ignored: %~1
shift
goto :args
:args_done

set "CFG=%GAMEDIR%\Resources\ClientDefinitions.ini"
set "PY=python"
set "PATCH=%SCRIPTDIR%patch_config.py"
set "STAGE=precheck"

REM No default game folder on purpose. Require it instead of guessing, so this
REM script can never write into a path that only made sense on the author's PC.
if not defined GAMEDIR (
    echo [ERROR] No game folder given. Pass the mod folder explicitly:
    echo             deploy.bat --gamedir "D:\Path\to\game" [--apply] [--sample]
    set "STAGE=fail"
)
if defined GAMEDIR if not exist "%GAMEDIR%" (
    echo [ERROR] Game folder not found: %GAMEDIR%
    set "STAGE=fail"
)

echo Game folder : %GAMEDIR%
echo Target DLL  : %DLL%
echo Config file : %CFG%
if "%APPLY%"=="1" (echo Mode        : APPLY - files will be modified) else (echo Mode        : DRY RUN - nothing will be written)
echo.

if not exist "%DLL%"            ( echo [ERROR] %DLL% not found. Run build.bat first. & set "STAGE=fail" )
if not exist "%SAMPLE%"         ( echo [WARNING] %SAMPLE% not found - --sample will be skipped. )
if not exist "%PATCH%"          ( echo [ERROR] patch_config.py not found next to this script. & set "STAGE=fail" )

REM No CnCNet client? A plain Syringe shortcut works too - say so instead of
REM failing, because that is the normal setup for a hand-made mod install.
REM (Skipped when the precheck already failed, so a bad --gamedir cannot be
REM  downgraded into "no config found" and then get a copy attempted into it.)
if not "%STAGE%"=="fail" if not exist "%CFG%" (
    echo [WARNING] %CFG% not found.
    echo           This mod is not launched through a CnCNet client. Add the DLL
    echo           by hand instead - see the install table in README.md.
    set "STAGE=noconfig"
)

if "%STAGE%"=="precheck" (
    %PY% "%PATCH%" check "%CFG%" "%DLL%"
    if errorlevel 1 (
        set "STAGE=needpatch"
    ) else (
        echo [OK] Config already contains -i=%DLL%.
        set "STAGE=copyonly"
    )
)

REM The config may already be wired, but the DLL on disk may be stale: always
REM refresh the DLL, so a rebuild followed by a re-run of this script cannot
REM leave the previous binary in the game folder.
if "%STAGE%"=="copyonly" (
    if "%APPLY%"=="0" (
        echo.
        echo Planned change:
        echo   copy /y "%DLL%" "%GAMEDIR%\%DLL%"
        echo.
        echo Dry run only. Re-run with --apply to write.
        set "STAGE=done"
    ) else (
        copy /y "%DLL%" "%GAMEDIR%\%DLL%" >nul
        if errorlevel 1 (
            echo [ERROR] Failed to copy DLL.
            set "STAGE=fail"
        ) else (
            echo Copied %DLL% to %GAMEDIR%.
            set "STAGE=done"
        )
    )
)

if "%STAGE%"=="noconfig" (
    if "%APPLY%"=="1" (
        copy /y "%DLL%" "%GAMEDIR%\%DLL%" >nul
        echo Copied %DLL%. Now add -i=%DLL% to the Syringe command line.
    ) else (
        echo Planned change:
        echo   copy /y "%DLL%" "%GAMEDIR%\%DLL%"
        echo Dry run only. Re-run with --apply to write.
    )
    set "STAGE=done"
)

if "%STAGE%"=="needpatch" (
    echo Planned change:
    echo.
    if "%APPLY%"=="1" (
        %PY% "%PATCH%" apply "%CFG%" "%DLL%"
    ) else (
        %PY% "%PATCH%" plan "%CFG%" "%DLL%"
    )
    if errorlevel 1 (
        echo.
        echo [ERROR] Cannot patch config ^(see message above^). Config left untouched.
        set "STAGE=fail"
    ) else (
        set "STAGE=patched"
    )
)

if "%STAGE%"=="patched" (
    echo.
    if "%APPLY%"=="0" (
        echo Dry run only. Re-run with --apply to write.
        set "STAGE=done"
    ) else (
        if not exist "%CFG%.bak" copy /y "%CFG%" "%CFG%.bak" >nul
        echo Backed up to ClientDefinitions.ini.bak
        copy /y "%DLL%" "%GAMEDIR%\%DLL%" >nul
        if errorlevel 1 (
            echo [ERROR] Failed to copy DLL.
            set "STAGE=fail"
        ) else (
            echo Copied %DLL%.
            echo.
            echo Result:
            findstr /c:"ExtraCommandLineParams" "%CFG%"
            set "STAGE=done"
        )
    )
)

REM ---------------------------------------------------------------- uimd.ini
REM The panel needs nothing in uimd.ini to run - every key has a built-in
REM default - so this step only exists to put an editable block on disk.
if not "%STAGE%"=="done" goto :summary

set "UIMD=%GAMEDIR%\uimd.ini"
if not exist "%UIMD%" (
    echo.
    echo [INFO] %UIMD% is not a loose file - it is packed inside a MIX.
    echo        To get an editable block, extract uimd.ini with XCC Mixer,
    echo        append %SAMPLE% to it, then put it back. The panel runs
    echo        with built-in defaults until then.
    goto :summary
)

findstr /i /c:"[WatchBar]" "%UIMD%" >nul
if not errorlevel 1 (
    echo.
    echo [OK] %UIMD% already has a [WatchBar] section - left untouched.
    goto :summary
)

if "%ADD_SAMPLE%"=="0" (
    echo.
    echo [INFO] %UIMD% has no [WatchBar] section - the panel will use its
    echo        built-in defaults. Re-run with --sample to append the block,
    echo        or paste %SAMPLE% in by hand.
    goto :summary
)

if not exist "%SAMPLE%" (
    echo.
    echo [WARNING] %SAMPLE% not found - cannot append the block.
    goto :summary
)

if "%APPLY%"=="0" (
    echo.
    echo Planned change:
    echo   copy /y "%UIMD%" "%UIMD%.bak"
    echo   type "%SAMPLE%" ^>^> "%UIMD%"
    goto :summary
)

if not exist "%UIMD%.bak" copy /y "%UIMD%" "%UIMD%.bak" >nul
type "%SAMPLE%" >> "%UIMD%"
echo.
echo Appended %SAMPLE% to %UIMD% ^(backup: uimd.ini.bak^).
echo Every key in that block is at its default value - edit it and restart
echo the game to change the panel.

:summary
echo.
echo ============================================================
if "%STAGE%"=="fail" (
    echo  FAILED - see errors above.
) else (
    echo  In game, the board is shown/hidden by the small strip on
    echo  its right edge - the same control the super-weapon sidebar
    echo  uses. No hotkey binding is required.
    echo.
    echo  Optional: Options - Keyboard - "WatchBar" also toggles it.
    echo.
    echo  Tuning: the panel has no ini of its own. Its settings live
    echo  in uimd.ini, section [WatchBar], as WatchBar.^<key^> -
    echo  every key is documented in docs\config.md, and
    echo  docs\uimd-sample.ini is a block you can paste in.
    echo  A changed value needs a game restart: the engine reads
    echo  uimd.ini once at startup.
    echo  Log: %GAMEDIR%\WatchBar.log
    echo.
    echo  To undo: restore ClientDefinitions.ini.bak, delete %DLL%
    echo           from the game folder, and drop the [WatchBar]
    echo           section from uimd.ini if you added one.
)
echo ============================================================

REM Report the outcome through the exit code too, so a wrapper script can tell a
REM dry run from a failure. %STAGE% is expanded on this line, i.e. before the
REM endlocal on the same line takes effect.
popd
if "%STAGE%"=="fail" ( endlocal & exit /b 1 )
endlocal
exit /b 0
