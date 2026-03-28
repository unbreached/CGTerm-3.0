; CGTerm 3.0 - NSIS Installer Script
; Creates a standalone Windows installer with all dependencies

!include "MUI2.nsh"
!include "FileFunc.nsh"

;-----------------------------------------------------
; General
;-----------------------------------------------------
Name "CGTerm 3.0 - C64 Scene Edition"
OutFile "..\dist\CGTerm-3.0-Setup.exe"
InstallDir "$PROGRAMFILES\CGTerm"
InstallDirRegKey HKLM "Software\CGTerm" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

;-----------------------------------------------------
; Version info embedded in the .exe
;-----------------------------------------------------
VIProductVersion "3.0.0.0"
VIAddVersionKey "ProductName" "CGTerm"
VIAddVersionKey "ProductVersion" "3.0"
VIAddVersionKey "FileDescription" "CGTerm 3.0 - C64 BBS Terminal"
VIAddVersionKey "LegalCopyright" "Genesis Project"

;-----------------------------------------------------
; Interface
;-----------------------------------------------------
; Uncomment the line below if you have AppIcon.ico in the assets folder
;!define MUI_ICON "..\assets\AppIcon.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "CGTerm 3.0 - C64 Scene Edition"
!define MUI_WELCOMEPAGE_TEXT "This will install CGTerm 3.0 on your computer.$\r$\n$\r$\nCGTerm is the legendary C64 BBS terminal, now with Punter upload support, Rainbow protocol, and improved rendering.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_RUN "$INSTDIR\cgterm.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch CGTerm"

;-----------------------------------------------------
; Pages
;-----------------------------------------------------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;-----------------------------------------------------
; Install section
;-----------------------------------------------------
Section "CGTerm (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Binaries
  File "..\bin\cgterm.exe"
  File /nonfatal "..\bin\cgchat.exe"
  File /nonfatal "..\bin\cgedit.exe"

  ; SDL runtime DLL
  File "..\dist\staging\SDL.dll"

  ; Assets
  SetOutPath "$INSTDIR\assets"
  File "..\assets\*.bmp"
  File "..\assets\*.kbd"
  File "..\assets\*.wav"
  File /nonfatal "..\assets\*.txt"
  File /nonfatal "..\assets\*.md"

  ; Documentation
  SetOutPath "$INSTDIR"
  File "..\README.txt"

  ; Default config
  SetOutPath "$INSTDIR"
  IfFileExists "$INSTDIR\cgterm.cfg" +2
    File /oname=cgterm.cfg "..\scripts\cgterm-default.cfg"

  ; Registry keys
  WriteRegStr HKLM "Software\CGTerm" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CGTerm" \
    "DisplayName" "CGTerm 3.0 - C64 Scene Edition"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CGTerm" \
    "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CGTerm" \
    "DisplayIcon" "$INSTDIR\cgterm.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CGTerm" \
    "Publisher" "Genesis Project"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CGTerm" \
    "DisplayVersion" "3.0"
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CGTerm" \
    "EstimatedSize" "$0"

  ; Uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

;-----------------------------------------------------
; Start Menu shortcuts (optional)
;-----------------------------------------------------
Section "Start Menu Shortcuts" SecShortcuts
  CreateDirectory "$SMPROGRAMS\CGTerm"
  CreateShortcut "$SMPROGRAMS\CGTerm\CGTerm.lnk" "$INSTDIR\cgterm.exe"
  CreateShortcut "$SMPROGRAMS\CGTerm\Uninstall.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

;-----------------------------------------------------
; Desktop shortcut (optional)
;-----------------------------------------------------
Section /o "Desktop Shortcut" SecDesktop
  CreateShortcut "$DESKTOP\CGTerm.lnk" "$INSTDIR\cgterm.exe"
SectionEnd

;-----------------------------------------------------
; Uninstall section
;-----------------------------------------------------
Section "Uninstall"
  Delete "$INSTDIR\cgterm.exe"
  Delete "$INSTDIR\cgchat.exe"
  Delete "$INSTDIR\cgedit.exe"
  Delete "$INSTDIR\SDL.dll"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\cgterm.cfg"
  Delete "$INSTDIR\uninstall.exe"

  Delete "$INSTDIR\assets\*.bmp"
  Delete "$INSTDIR\assets\*.kbd"
  Delete "$INSTDIR\assets\*.wav"
  Delete "$INSTDIR\assets\*.txt"
  Delete "$INSTDIR\assets\*.md"
  RMDir "$INSTDIR\assets"

  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\CGTerm\CGTerm.lnk"
  Delete "$SMPROGRAMS\CGTerm\Uninstall.lnk"
  RMDir "$SMPROGRAMS\CGTerm"
  Delete "$DESKTOP\CGTerm.lnk"

  DeleteRegKey HKLM "Software\CGTerm"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CGTerm"
SectionEnd

;-----------------------------------------------------
; Section descriptions
;-----------------------------------------------------
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Core CGTerm files (required)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecShortcuts} "Create Start Menu shortcuts"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create a Desktop shortcut"
!insertmacro MUI_FUNCTION_DESCRIPTION_END
