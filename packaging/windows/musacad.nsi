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
; "Run Musa CAD" on the finish page. MUI_FINISHPAGE_RUN would start the program with the
; installer's administrator token (drag-and-drop from Explorer stops working, every file it
; writes is owned by an elevated process). Starting it through Explorer -- the user's own,
; un-elevated shell -- launches it as the normal user, which is how the shortcut runs it.
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APPNAME}"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchAsUser
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; The install is machine-wide (Program Files, HKLM, admin), so the Start-menu entry must be
; too. Without this $SMPROGRAMS is the INSTALLING user's menu: other accounts never see the
; program, and a standard user who elevated with an administrator's credentials finds the
; shortcut in the administrator's menu. Set in both halves, so the uninstaller removes what
; the installer created.
Function .onInit
  SetShellVarContext all
FunctionEnd
Function un.onInit
  SetShellVarContext all
FunctionEnd

Function LaunchAsUser
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${EXENAME}"'
FunctionEnd

; ---------------------------------------------------------------------------
Section "Musa CAD (required)" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGING}\*.*"        ; the whole windeployqt staging tree (backslash: NSIS /r glob)
  File "assets\branding\musacad.ico"

  ; windeployqt --compiler-runtime puts the Visual C++ redistributable INSTALLER into the
  ; staging tree (not the runtime DLLs); copying it along is not enough -- it has to run, or
  ; a machine without the 2015-2022 runtime fails to start musacad_app.exe with
  ; "VCRUNTIME140.dll was not found". Quiet install; exit 1638 means a newer runtime is
  ; already there and 3010 that a reboot is pending -- both fine. The 19 MB installer is
  ; not kept in the install folder afterwards.
  IfFileExists "$INSTDIR\vc_redist.x64.exe" 0 +3
    ExecWait '"$INSTDIR\vc_redist.x64.exe" /install /quiet /norestart'
    Delete "$INSTDIR\vc_redist.x64.exe"

  WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  ; Earlier installers (v0.1.0) put the shortcut in the installing user's own menu; take
  ; that one away so an upgrade does not leave two entries.
  SetShellVarContext current
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  SetShellVarContext all
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
