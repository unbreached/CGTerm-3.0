@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0..
if "%PREFIX%"=="" set PREFIX=%ProgramFiles%\CGTerm
set BINDIR=%PREFIX%\bin
:: Assets must live in a directory the binary actually probes. On Windows
:: paths.c looks in exe_dir\assets, then parent\assets, then exe_dir — never
:: share\cgterm\assets. Use exe_dir\assets (= %BINDIR%\assets) so it is found.
set DATADIR=%BINDIR%\assets

echo.
echo  ============================================
echo   CGTerm 3.0 - Windows Installer
echo  ============================================
echo.

:: ---------------------------------------------------
:: Check for MSYS2 / MinGW
:: ---------------------------------------------------
where gcc >nul 2>&1
if errorlevel 1 (
    echo [!] GCC not found.
    echo     CGTerm requires MSYS2 with MinGW-w64 to compile.
    echo.
    echo     If MSYS2 is already installed, make sure you are
    echo     running this script from the MSYS2 MinGW 64-bit shell
    echo     or that MinGW bin directory is in your PATH.
    echo.
    echo     To install MSYS2:
    echo       1. Download from https://www.msys2.org/
    echo       2. Run the installer
    echo       3. Open "MSYS2 MinGW 64-bit" from the Start menu
    echo       4. Run: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make
    echo.
    set /p INSTALL_MSYS2="Would you like to open the MSYS2 download page? [y/n] "
    if /i "!INSTALL_MSYS2!"=="y" (
        start https://www.msys2.org/
        echo.
        echo     After installing MSYS2, re-run this script from
        echo     the MSYS2 MinGW 64-bit shell.
    )
    exit /b 1
)
echo [+] Found GCC

:: ---------------------------------------------------
:: Check for Make
:: ---------------------------------------------------
where make >nul 2>&1
if errorlevel 1 (
    where mingw32-make >nul 2>&1
    if errorlevel 1 (
        echo [!] Make not found.
        echo     Install it with: pacman -S mingw-w64-x86_64-make
        echo     Or:              pacman -S make
        exit /b 1
    ) else (
        set MAKE=mingw32-make
        echo [+] Found mingw32-make
    )
) else (
    set MAKE=make
    echo [+] Found make
)

:: ---------------------------------------------------
:: Check for SDL 1.2 development files
:: ---------------------------------------------------
where sdl-config >nul 2>&1
if errorlevel 1 (
    echo [!] SDL 1.2 development files not found.
    echo.
    set /p INSTALL_SDL="Would you like to install SDL 1.2 now? [y/n] "
    if /i "!INSTALL_SDL!"=="y" (
        echo [*] Installing SDL 1.2 via pacman...
        pacman -S --noconfirm mingw-w64-x86_64-SDL
        if errorlevel 1 (
            echo [!] Failed to install SDL. Try manually:
            echo     pacman -S mingw-w64-x86_64-SDL
            exit /b 1
        )
        echo [+] SDL 1.2 installed
    ) else (
        echo [!] SDL 1.2 is required to build CGTerm.
        echo     Install it with: pacman -S mingw-w64-x86_64-SDL
        exit /b 1
    )
) else (
    for /f "tokens=*" %%v in ('sdl-config --version') do set SDL_VER=%%v
    echo [+] Found SDL !SDL_VER!
    echo !SDL_VER! | findstr /b "1.2" >nul
    if errorlevel 1 (
        echo [!] CGTerm requires SDL 1.2, found !SDL_VER!
        echo     Install SDL 1.2 with: pacman -S mingw-w64-x86_64-SDL
        exit /b 1
    )
)

:: ---------------------------------------------------
:: Build
:: ---------------------------------------------------
echo.
echo [*] Building CGTerm 3.0...
%MAKE% -C "%ROOT%" EXESUFFIX=.exe SOCKETLIBS=-lws2_32 clean all
if errorlevel 1 (
    echo.
    echo [!] Build failed.
    exit /b 1
)
echo [+] Build successful

:: ---------------------------------------------------
:: Install
:: ---------------------------------------------------
echo.
echo [*] Install location: %PREFIX%
set /p DO_INSTALL="Proceed with installation? [y/n] "
if /i not "%DO_INSTALL%"=="y" (
    echo [*] Skipping installation. Binaries are in %ROOT%\bin
    goto :done
)

if not exist "%BINDIR%" mkdir "%BINDIR%"
if not exist "%DATADIR%" mkdir "%DATADIR%"

copy /Y "%ROOT%\bin\cgterm.exe" "%BINDIR%" >nul
if exist "%ROOT%\bin\cgchat.exe" copy /Y "%ROOT%\bin\cgchat.exe" "%BINDIR%" >nul
if exist "%ROOT%\bin\cgedit.exe" copy /Y "%ROOT%\bin\cgedit.exe" "%BINDIR%" >nul

copy /Y "%ROOT%\assets\*.bmp" "%DATADIR%" >nul
copy /Y "%ROOT%\assets\*.kbd" "%DATADIR%" >nul
copy /Y "%ROOT%\assets\*.wav" "%DATADIR%" >nul
if exist "%ROOT%\assets\*.xm" copy /Y "%ROOT%\assets\*.xm" "%DATADIR%" >nul 2>nul
if exist "%ROOT%\assets\*.txt" copy /Y "%ROOT%\assets\*.txt" "%DATADIR%" >nul 2>nul
:: the font menu needs the fonts\ subdirectory too
if not exist "%DATADIR%\fonts" mkdir "%DATADIR%\fonts"
copy /Y "%ROOT%\assets\fonts\*.bmp" "%DATADIR%\fonts" >nul

echo.
echo [+] Installed binaries to %BINDIR%
echo [+] Installed assets to   %DATADIR%

:: ---------------------------------------------------
:: Add to PATH (optional)
:: ---------------------------------------------------
echo %PATH% | findstr /i /c:"%BINDIR%" >nul
if errorlevel 1 (
    echo.
    set /p ADD_PATH="Add %BINDIR% to your PATH? [y/n] "
    if /i "!ADD_PATH!"=="y" (
        :: Append to the USER PATH from the registry, not the merged %PATH%.
        :: setx PATH "%PATH%;..." would copy the whole machine PATH into the
        :: user scope and truncate it at 1024 chars.
        set "USERPATH="
        for /f "tokens=2*" %%a in ('reg query HKCU\Environment /v PATH 2^>nul') do set "USERPATH=%%b"
        if defined USERPATH (
            setx PATH "!USERPATH!;%BINDIR%" >nul 2>&1
        ) else (
            setx PATH "%BINDIR%" >nul 2>&1
        )
        if errorlevel 1 (
            echo [!] Could not update PATH automatically.
            echo     Add this manually: %BINDIR%
        ) else (
            echo [+] Added to PATH. Restart your terminal for it to take effect.
        )
    )
)

:done
echo.
echo  ============================================
echo   CGTerm 3.0 installed successfully!
echo.
echo   Run: cgterm
echo   Or:  %BINDIR%\cgterm.exe
echo  ============================================
endlocal
