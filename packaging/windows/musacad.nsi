; NSIS installer for Musa CAD (Windows x86_64).
; Invoked by .github/workflows/build-windows.yml from the repo root:
;   makensis /DVERSION=0.1.0 /DSTAGING=staging packaging\windows\musacad.nsi
; All relative paths below resolve against the makensis working directory (the repo root).
;
; Upgrades. An existing installation (the InstallDir the last setup wrote, holding
; musacad_app.exe) is updated where it is: the directory page is skipped, the previous
; version is removed in place first (so files it had and this one has not are gone too),
; and its file-type registration is kept. The program itself does the same from its
; Update dialog: it downloads the new setup, checks it, and runs it as
;   MusaCAD-<ver>-x86_64-setup.exe /S /UPDATE /D=<install dir>
; -- silent, with /UPDATE the setup waits for the running Musa CAD to exit, updates, and
; starts the new version as the user (not as the administrator the setup runs as).

Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

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

; The version resource. The program's in-place update reads ProductVersion from the
; downloaded setup and refuses to run one that does not say the version the release
; announced; Windows shows the same fields in the file's Properties and in SmartScreen's
; prompt, where an unnamed file looks worse than a named one.
VIProductVersion "${VERSION}.0"
VIFileVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APPNAME} ${VERSION} setup"
VIAddVersionKey "CompanyName" "${COMPANY}"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2026 Pranay Kiran and contributors. LGPL-3.0-or-later."

Var UpdateMode    ; 1 when started by the program with /UPDATE
Var ExistingDir   ; the installation being updated ("" for a fresh install)
Var WelcomeText

!define MUI_ICON "assets/branding/musacad.ico"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TEXT "$WelcomeText"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_PRE DirectoryPre
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

Function un.onInit
  SetShellVarContext all
FunctionEnd

Function LaunchAsUser
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${EXENAME}"'
FunctionEnd

; An existing installation is updated where it is: no directory to choose.
Function DirectoryPre
  ${If} $ExistingDir != ""
    Abort
  ${EndIf}
FunctionEnd

; Musa CAD must not be running while its files are replaced: neither the program nor the
; console front-end (musacad.exe, alive while a --plot runs). The program that started an
; update quits right after starting the setup, so this is normally over in a moment; a
; user running the setup by hand while Musa CAD is open is asked to close it.
; Call with the file name in $R7.
Function WaitForFileFree
  StrCpy $R9 0
  wait_loop:
    IfFileExists "$INSTDIR\$R7" 0 wait_done
    ClearErrors
    FileOpen $R8 "$INSTDIR\$R7" a          ; write access, which a running program refuses
    IfErrors wait_busy
    FileClose $R8
    Goto wait_done
  wait_busy:
    IntOp $R9 $R9 + 1
    ${If} $R9 >= 120                       ; a minute
      ${If} $UpdateMode == 1
        Abort                              ; silent: nothing was changed
      ${EndIf}
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
        "${APPNAME} is still running. Close it, then click Retry." IDRETRY wait_retry
      Abort
    wait_retry:
      StrCpy $R9 0
    ${EndIf}
    Sleep 500
    Goto wait_loop
  wait_done:
FunctionEnd

Function WaitForAppExit
  StrCpy $R7 "${EXENAME}"
  Call WaitForFileFree
  StrCpy $R7 "musacad.exe"
  Call WaitForFileFree
FunctionEnd

; ---------------------------------------------------------------------------
Section "Musa CAD (required)" SecCore
  SectionIn RO
  ${If} $ExistingDir != ""
    Call WaitForAppExit
    ; The previous version, removed in place before the new files land, so nothing of
    ; it lingers. Its uninstaller also drops the Start-menu entry, the registry entries
    ; and the file types; all of them are written again below.
    IfFileExists "$INSTDIR\uninstall.exe" 0 +2
      ExecWait '"$INSTDIR\uninstall.exe" /S _?=$INSTDIR'
    Delete "$INSTDIR\uninstall.exe"
  ${EndIf}
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
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; Optional, UNCHECKED by default: register file types. The user owns the default-app
; choice -- we claim Musa's own .musa, and only ADD Musa to the "Open with" list for the
; shared .dxf/.dwg (never force-stealing those defaults). An upgrade pre-selects it when
; the previous version had registered them (see .onInit).
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

; Last, after everything else in this section ran: an update started by the program
; brings the new version back up, as the user.
Section "-Relaunch"
  ${If} $UpdateMode == 1
    Call LaunchAsUser
  ${EndIf}
SectionEnd

LangString DESC_Core  ${LANG_ENGLISH} "The Musa CAD application and its Qt runtime."
LangString DESC_Assoc ${LANG_ENGLISH} "Associate .musa (and offer Musa CAD for .dxf/.dwg in Open With)."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore}  $(DESC_Core)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} $(DESC_Assoc)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; The install is machine-wide (Program Files, HKLM, admin), so the Start-menu entry must be
; too. Without this $SMPROGRAMS is the INSTALLING user's menu: other accounts never see the
; program, and a standard user who elevated with an administrator's credentials finds the
; shortcut in the administrator's menu. Set in both halves (see un.onInit), so the
; uninstaller removes what the installer created.
;
; Placed after the sections: it refers to ${SecAssoc}, which exists once that section is
; declared.
Function .onInit
  SetShellVarContext all
  StrCpy $UpdateMode 0
  StrCpy $ExistingDir ""
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  ${IfNot} ${Errors}
    StrCpy $UpdateMode 1
  ${EndIf}
  ; $INSTDIR already holds the last setup's InstallDir (InstallDirRegKey), or the /D=
  ; given on the command line. When Musa CAD is installed there, this is an update.
  ${If} ${FileExists} "$INSTDIR\${EXENAME}"
    StrCpy $ExistingDir $INSTDIR
    ReadRegStr $R2 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion"
    ${If} $R2 == ""
      StrCpy $R2 "an earlier version"
    ${Else}
      StrCpy $R2 "version $R2"
    ${EndIf}
    StrCpy $WelcomeText "Setup found ${APPNAME} $R2 in$\r$\n$INSTDIR$\r$\n$\r$\nand will update it to \
version ${VERSION} there. Your settings and drawings are not touched.$\r$\n$\r$\nClose ${APPNAME} before \
you continue, then click Next."
    ; Keep the file types the previous version registered.
    ReadRegStr $R3 HKLM "Software\Classes\.musa" ""
    ${If} $R3 == "${PROGID}"
      SectionGetFlags ${SecAssoc} $R4
      IntOp $R4 $R4 | ${SF_SELECTED}
      SectionSetFlags ${SecAssoc} $R4
    ${EndIf}
  ${Else}
    StrCpy $WelcomeText "Setup will install ${APPNAME} ${VERSION} on your computer.$\r$\n$\r$\nClick Next \
to continue."
  ${EndIf}
FunctionEnd

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
