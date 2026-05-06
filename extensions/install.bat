@echo off
REM Install all qrenderdoc extensions in this directory into the user's
REM qrenderdoc extensions folder by copying each one.
REM
REM   <repo>\extensions\<name>  ==>  %APPDATA%\qrenderdoc\extensions\<name>
REM
REM Copying (rather than junction-linking) makes the installed extension
REM independent of this repo's location: you can move, rename, or delete
REM the repo afterwards and the installed extension keeps working.
REM
REM Re-running is safe: each existing target (directory or junction) is
REM removed and replaced with a fresh copy. After running, open qrenderdoc
REM -> Tools -> Manage Extensions and tick "Loaded" for each new extension.

setlocal enabledelayedexpansion

set "src_dir=%~dp0"
if "%src_dir:~-1%"=="\" set "src_dir=%src_dir:~0,-1%"

set "dst_dir=%APPDATA%\qrenderdoc\extensions"

if not exist "%dst_dir%" mkdir "%dst_dir%"

set /a installed=0

for /d %%D in ("%src_dir%\*") do (
    if exist "%%D\extension.json" (
        set "name=%%~nxD"
        set "target=%dst_dir%\!name!"

        if exist "!target!" (
            REM A junction is a reparse point; rmdir without /S deletes only the
            REM link, leaving the original source intact. A regular directory
            REM needs /S /Q to be removed recursively.
            fsutil reparsepoint query "!target!" >nul 2>&1
            if !errorlevel! equ 0 (
                rmdir "!target!"
            ) else (
                rmdir /S /Q "!target!"
            )
        )

        xcopy "%%D" "!target!\" /E /I /Y /Q >nul
        if !errorlevel! equ 0 (
            if exist "!target!\__pycache__" rmdir /S /Q "!target!\__pycache__"
            echo installed: "!target!"
            set /a installed=installed+1
        ) else (
            echo failed:    "!target!" ^(xcopy returned !errorlevel!^)
        )
    )
)

if %installed% equ 0 (
    echo no extensions found in "%src_dir%" 1>&2
    exit /b 1
)

echo.
echo Done. Open qrenderdoc -^> Tools -^> Manage Extensions and tick "Loaded" for each.
endlocal
