; NSIS installer for Musa CAD (Windows x86_64).
; Invoked by .github/workflows/build-windows.yml from the repo root:
;   makensis /DVERSION=0.1.0 /DSTAGING=staging packaging\windows\musacad.nsi
; All relative paths below resolve against the makensis working directory (the repo root).

Unicode true
!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "0.1.0"
!endif
!ifndef STAGING
  !define STAGING "staging"      ; dir of musacad_app.exe + Qt DLLs/plugins (windeployqt output)
!endif

!define APPNAME "Musa CAD"
!define EXENAME "musacad_app.exe"
!define COMPANY "Musa CAD"
!define PROGID  "MusaCAD.Drawing"

Name "${APPNAME} ${VERSION}"
; Compile-time source paths use forward slashes (portable across makensis on Windows + Linux).
; Runtime install paths ($INSTDIR etc.) keep Windows backslashes. makensis must be invoked with
; /NOCD so these resolve against the repo root, not the script's directory.
OutFile "packaging/windows/MusaCAD-${VERSION}-x86_64-setup.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${APPNAME}" "InstallDir"
RequestExecutionLevel admin          ; Program Files + HKLM uninstall entry
SetCompressor /SOLID lzma

!define MUI_ICON "assets/branding/musacad.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
Section "Musa CAD (required)" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGING}\*.*"        ; the whole windeployqt staging tree (backslash: NSIS /r glob)
  File "assets\branding\musacad.ico"

  WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\musacad.ico"

  ; Add/Remove Programs entry
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "${COMPANY}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayIcon" "$INSTDIR\musacad.ico"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; Optional, UNCHECKED by default: register file types. The user owns the default-app
; choice -- we claim Musa's own .musa, and only ADD Musa to the "Open with" list for the
; shared .dxf/.dwg (never force-stealing those defaults).
Section /o "Register .musa / .dxf file types" SecAssoc
  WriteRegStr HKLM "Software\Classes\${PROGID}" "" "Musa CAD Drawing"
  WriteRegStr HKLM "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\musacad.ico"
  WriteRegStr HKLM "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\${EXENAME}" "%1"'

  ; .musa -> our format, claim as default handler
  WriteRegStr HKLM "Software\Classes\.musa" "" "${PROGID}"
  ; .dxf / .dwg -> only offer in the Open-With list, do not steal the default
  WriteRegStr HKLM "Software\Classes\.dxf\OpenWithProgids" "${PROGID}" ""
  WriteRegStr HKLM "Software\Classes\.dwg\OpenWithProgids" "${PROGID}" ""

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'  ; SHCNE_ASSOCCHANGED
SectionEnd

LangString DESC_Core  ${LANG_ENGLISH} "The Musa CAD application and its Qt runtime."
LangString DESC_Assoc ${LANG_ENGLISH} "Associate .musa (and offer Musa CAD for .dxf/.dwg in Open With)."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore}  $(DESC_Core)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} $(DESC_Assoc)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------------------
Section "Uninstall"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
  DeleteRegKey HKLM "Software\${APPNAME}"
  DeleteRegKey HKLM "Software\Classes\${PROGID}"
  DeleteRegValue HKLM "Software\Classes\.musa" ""
  DeleteRegValue HKLM "Software\Classes\.dxf\OpenWithProgids" "${PROGID}"
  DeleteRegValue HKLM "Software\Classes\.dwg\OpenWithProgids" "${PROGID}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
