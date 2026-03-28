$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Prefix = if ($env:PREFIX) { $env:PREFIX } else { Join-Path $env:ProgramFiles "CGTerm" }
$BinDir = Join-Path $Prefix "bin"
$DataDir = Join-Path $Prefix "share\cgterm\assets"

Write-Host ""
Write-Host " ============================================"
Write-Host "  CGTerm 3.0 - Windows Installer (PowerShell)"
Write-Host " ============================================"
Write-Host ""

# ---------------------------------------------------
# Check for GCC (MinGW)
# ---------------------------------------------------
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $gcc) {
    Write-Host "[!] GCC not found." -ForegroundColor Red
    Write-Host "    CGTerm requires MSYS2 with MinGW-w64 to compile."
    Write-Host ""
    Write-Host "    If MSYS2 is already installed, make sure MinGW"
    Write-Host "    bin directory is in your PATH."
    Write-Host ""
    Write-Host "    To install MSYS2:"
    Write-Host "      1. Download from https://www.msys2.org/"
    Write-Host "      2. Run the installer"
    Write-Host "      3. Open 'MSYS2 MinGW 64-bit' from Start menu"
    Write-Host "      4. Run: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make"
    Write-Host ""
    $open = Read-Host "Would you like to open the MSYS2 download page? [y/n]"
    if ($open -eq 'y') {
        Start-Process "https://www.msys2.org/"
        Write-Host ""
        Write-Host "    After installing MSYS2, re-run this script."
    }
    exit 1
}
Write-Host "[+] Found GCC"

# ---------------------------------------------------
# Check for Make
# ---------------------------------------------------
$make = Get-Command make -ErrorAction SilentlyContinue
if (-not $make) {
    $make = Get-Command mingw32-make -ErrorAction SilentlyContinue
    if (-not $make) {
        Write-Host "[!] Make not found." -ForegroundColor Red
        Write-Host "    Install it with: pacman -S mingw-w64-x86_64-make"
        Write-Host "    Or:              pacman -S make"
        exit 1
    }
    $makecmd = "mingw32-make"
    Write-Host "[+] Found mingw32-make"
} else {
    $makecmd = "make"
    Write-Host "[+] Found make"
}

# ---------------------------------------------------
# Check for SDL 1.2
# ---------------------------------------------------
$sdlcfg = Get-Command sdl-config -ErrorAction SilentlyContinue
if (-not $sdlcfg) {
    Write-Host "[!] SDL 1.2 development files not found." -ForegroundColor Red
    Write-Host ""
    $inst = Read-Host "Would you like to install SDL 1.2 now? [y/n]"
    if ($inst -eq 'y') {
        Write-Host "[*] Installing SDL 1.2 via pacman..."
        & pacman -S --noconfirm mingw-w64-x86_64-SDL
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[!] Failed to install SDL. Try manually:" -ForegroundColor Red
            Write-Host "    pacman -S mingw-w64-x86_64-SDL"
            exit 1
        }
        Write-Host "[+] SDL 1.2 installed"
    } else {
        Write-Host "[!] SDL 1.2 is required to build CGTerm."
        Write-Host "    Install it with: pacman -S mingw-w64-x86_64-SDL"
        exit 1
    }
} else {
    $sdlver = & sdl-config --version
    Write-Host "[+] Found SDL $sdlver"
    if (-not $sdlver.StartsWith("1.2")) {
        Write-Host "[!] CGTerm requires SDL 1.2, found $sdlver" -ForegroundColor Red
        Write-Host "    Install SDL 1.2 with: pacman -S mingw-w64-x86_64-SDL"
        exit 1
    }
}

# ---------------------------------------------------
# Build
# ---------------------------------------------------
Write-Host ""
Write-Host "[*] Building CGTerm 3.0..."
& $makecmd -C $Root EXESUFFIX=.exe SOCKETLIBS=-lws2_32 clean all
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[!] Build failed." -ForegroundColor Red
    exit 1
}
Write-Host "[+] Build successful"

# ---------------------------------------------------
# Install
# ---------------------------------------------------
Write-Host ""
Write-Host "[*] Install location: $Prefix"
$proceed = Read-Host "Proceed with installation? [y/n]"
if ($proceed -ne 'y') {
    Write-Host "[*] Skipping installation. Binaries are in $Root\bin"
    exit 0
}

New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null

Copy-Item "$Root\bin\cgterm.exe" $BinDir -Force
Copy-Item "$Root\bin\cgchat.exe" $BinDir -Force -ErrorAction SilentlyContinue
Copy-Item "$Root\bin\cgedit.exe" $BinDir -Force -ErrorAction SilentlyContinue
Copy-Item "$Root\assets\*.bmp" $DataDir -Force
Copy-Item "$Root\assets\*.kbd" $DataDir -Force
Copy-Item "$Root\assets\*.wav" $DataDir -Force
Copy-Item "$Root\assets\*.txt" $DataDir -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "[+] Installed binaries to $BinDir"
Write-Host "[+] Installed assets to   $DataDir"

# ---------------------------------------------------
# Add to PATH (optional)
# ---------------------------------------------------
if ($env:PATH -notlike "*$BinDir*") {
    Write-Host ""
    $addpath = Read-Host "Add $BinDir to your PATH? [y/n]"
    if ($addpath -eq 'y') {
        try {
            [Environment]::SetEnvironmentVariable("PATH",
                "$([Environment]::GetEnvironmentVariable('PATH', 'User'));$BinDir", "User")
            Write-Host "[+] Added to PATH. Restart your terminal for it to take effect."
        } catch {
            Write-Host "[!] Could not update PATH automatically." -ForegroundColor Yellow
            Write-Host "    Add this manually: $BinDir"
        }
    }
}

Write-Host ""
Write-Host " ============================================"
Write-Host "  CGTerm 3.0 installed successfully!"
Write-Host ""
Write-Host "  Run: cgterm"
Write-Host "  Or:  $BinDir\cgterm.exe"
Write-Host " ============================================"
