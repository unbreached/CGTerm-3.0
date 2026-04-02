@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0..

echo.
echo  ============================================
echo   CGTerm 3.0 - Installer Builder
echo  ============================================
echo.

:: ---------------------------------------------------
:: Check for build tools
:: ---------------------------------------------------
where gcc >nul 2>&1
if errorlevel 1 (
    echo [!] GCC not found. Run install-windows.bat first to set up build tools.
    exit /b 1
)

where make >nul 2>&1
if errorlevel 1 (
    where mingw32-make >nul 2>&1
    if errorlevel 1 (
        echo [!] Make not found. Run install-windows.bat first to set up build tools.
        exit /b 1
    ) else (
        set MAKE=mingw32-make
    )
) else (
    set MAKE=make
)

:: ---------------------------------------------------
:: Check for NSIS
:: ---------------------------------------------------
where makensis >nul 2>&1
if errorlevel 1 (
    :: Check common install locations
    set NSIS_PATH=
    if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
        set "NSIS_PATH=C:\Program Files (x86)\NSIS\makensis.exe"
    )
    if exist "C:\Program Files\NSIS\makensis.exe" (
        set "NSIS_PATH=C:\Program Files\NSIS\makensis.exe"
    )
    if "!NSIS_PATH!"=="" (
        echo [!] NSIS (Nullsoft Scriptable Install System) not found.
        echo     NSIS is needed to create the installer .exe
        echo.
        echo     Download from: https://nsis.sourceforge.io/Download
        echo.
        set /p OPEN_NSIS="Would you like to open the NSIS download page? [y/n] "
        if /i "!OPEN_NSIS!"=="y" (
            start https://nsis.sourceforge.io/Download
        )
        echo.
        echo     After installing NSIS, re-run this script.
        exit /b 1
    )
) else (
    set "NSIS_PATH=makensis"
)
echo [+] Found NSIS

:: ---------------------------------------------------
:: Build CGTerm
:: ---------------------------------------------------
echo [*] Building CGTerm 3.0...
%MAKE% -C "%ROOT%" EXESUFFIX=.exe SOCKETLIBS=-lws2_32 clean all
if errorlevel 1 (
    echo [!] Build failed.
    exit /b 1
)
echo [+] Build successful

:: ---------------------------------------------------
:: Prepare staging area
:: ---------------------------------------------------
echo [*] Preparing installer files...

if not exist "%ROOT%\dist\staging" mkdir "%ROOT%\dist\staging"

:: Find and copy SDL.dll
:: Check common locations where MinGW puts SDL.dll
set SDL_DLL=
for %%P in (
    "%MINGW_PREFIX%\bin\SDL.dll"
    "C:\msys64\mingw64\bin\SDL.dll"
    "C:\msys64\mingw32\bin\SDL.dll"
    "C:\MinGW\bin\SDL.dll"
) do (
    if exist %%P (
        set "SDL_DLL=%%~P"
    )
)

if "!SDL_DLL!"=="" (
    :: Try to find it via sdl-config
    for /f "tokens=*" %%L in ('sdl-config --libs 2^>nul') do (
        for %%W in (%%L) do (
            echo %%W | findstr /i /c:"-L" >nul
            if not errorlevel 1 (
                set "SDL_LIB_DIR=%%W"
                set "SDL_LIB_DIR=!SDL_LIB_DIR:-L=!"
                if exist "!SDL_LIB_DIR!\SDL.dll" (
                    set "SDL_DLL=!SDL_LIB_DIR!\SDL.dll"
                )
                :: Also check ../bin relative to lib dir
                if exist "!SDL_LIB_DIR!\..\bin\SDL.dll" (
                    set "SDL_DLL=!SDL_LIB_DIR!\..\bin\SDL.dll"
                )
            )
        )
    )
)

if "!SDL_DLL!"=="" (
    echo [!] Could not find SDL.dll
    echo     Please copy SDL.dll to: %ROOT%\dist\staging\SDL.dll
    echo     Then re-run this script.
    echo.
    echo     SDL.dll is usually in your MinGW bin directory, e.g.:
    echo       C:\msys64\mingw64\bin\SDL.dll
    exit /b 1
)

echo [+] Found SDL.dll: !SDL_DLL!
copy /Y "!SDL_DLL!" "%ROOT%\dist\staging\SDL.dll" >nul

:: ---------------------------------------------------
:: Check for .ico file (create from .icns if missing)
:: ---------------------------------------------------
if not exist "%ROOT%\assets\AppIcon.ico" (
    echo [*] Note: assets\AppIcon.ico not found.
    echo     The installer will work without it, but won't have a custom icon.
    echo     To add one, place AppIcon.ico in the assets folder.
)

:: ---------------------------------------------------
:: Build installer
:: ---------------------------------------------------
echo [*] Creating installer...

if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"

"!NSIS_PATH!" "%ROOT%\scripts\cgterm-installer.nsi"
if errorlevel 1 (
    echo [!] NSIS failed to create installer.
    echo     If the error mentions a missing .ico file, you can either:
    echo     - Add assets\AppIcon.ico
    echo     - Or remove the MUI_ICON line from cgterm-installer.nsi
    exit /b 1
)

echo.
echo  ============================================
echo   Installer created successfully!
echo.
echo   File: %ROOT%\dist\CGTerm-3.0-Setup.exe
echo.
echo   This is a standalone installer that includes:
echo   - CGTerm binaries
echo   - SDL runtime library
echo   - All assets (fonts, keyboard layouts, sounds)
echo   - Default configuration with BBS bookmarks
echo  ============================================
endlocal
