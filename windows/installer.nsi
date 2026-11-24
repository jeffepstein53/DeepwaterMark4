; Deepwater Mark 4's installer: one .exe, per user, no administrator's consent.
;
;   makensis /DVERSION=0.4.0 /DSTAGE=<folder to pack> /DICON=<Deepwater.ico> /DOUTFILE=<installer.exe> installer.nsi
;
; (`cmake --build <build> --target installer` does it.) The installer's command line:
;   /S         silent: how the update inside Deepwater runs it
;   /D=<dir>   the folder to install into; must come last, without quotes
;   /NOREG     touch neither the registry nor the Start menu: for tests only
;
; It never writes to Program Files\Deepwater (the original Deepwater's folder), and it never touches the user's data
; or settings: those live in %APPDATA% and in the registry, not in the install folder.

Unicode true

!ifndef VERSION
  !error "pass /DVERSION=x.y.z"
!endif
!ifndef STAGE
  !error "pass /DSTAGE=<folder to pack>"
!endif
!ifndef ICON
  !error "pass /DICON=<Deepwater.ico>"
!endif
!ifndef OUTFILE
  !define OUTFILE "Deepwater-Mark4-${VERSION}-win64.exe"
!endif

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!insertmacro GetParameters
!insertmacro GetOptions
!insertmacro GetSize

!define APPNAME "Deepwater Mark 4"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
RequestExecutionLevel user
ManifestDPIAware true
SetCompressor /SOLID lzma
SetCompressorDictSize 32
ShowInstDetails nevershow
ShowUninstDetails nevershow
BrandingText "${APPNAME} ${VERSION}"

Var NoReg

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=${LANG_FRENCH} "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=${LANG_FRENCH} "FileDescription" "Installateur de ${APPNAME}"
VIAddVersionKey /LANG=${LANG_FRENCH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_FRENCH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_FRENCH} "LegalCopyright" "Licence GNU AGPL 3.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${APPNAME} installer"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "GNU AGPL 3.0 licence"

LangString MSG_CLOSE_FIRST ${LANG_FRENCH} "Deepwater est ouvert. Fermez-le, puis cliquez sur Réessayer."
LangString MSG_CLOSE_FIRST ${LANG_ENGLISH} "Deepwater is open. Close it, then click Retry."
LangString MSG_ORIGINAL ${LANG_FRENCH} "Ce dossier est celui de Deepwater d'origine : l'installateur n'y touche pas."
LangString MSG_ORIGINAL ${LANG_ENGLISH} "This is the original Deepwater's folder: the installer does not touch it."

Function .onInit
  SetAutoClose true   ; no "finished" page: the window closes by itself
  StrCpy $NoReg 0
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/NOREG" $1
  ${If} ${Errors}
    ClearErrors
  ${Else}
    StrCpy $NoReg 1
  ${EndIf}
  ; installed already: update it where it is (a /D= on the command line has set $INSTDIR already, and wins)
  ${If} $INSTDIR == "$LOCALAPPDATA\Programs\${APPNAME}"
    ReadRegStr $1 HKCU "${UNINSTKEY}" "InstallLocation"
    ${If} $1 != ""
      StrCpy $INSTDIR $1
    ${EndIf}
  ${EndIf}
  ; the original Deepwater's folder is never ours
  ${If} $INSTDIR == "$PROGRAMFILES64\Deepwater"
  ${OrIf} $INSTDIR == "$PROGRAMFILES\Deepwater"
    MessageBox MB_OK|MB_ICONSTOP "$(MSG_ORIGINAL)" /SD IDOK
    SetErrorLevel 6
    Abort
  ${EndIf}
FunctionEnd

Section "Install"
  SetOutPath "$INSTDIR"
  ; Deepwater still running from this folder: its files are locked
  tryAgain:
  ${If} ${FileExists} "$INSTDIR\Deepwater.exe"
    Delete "$INSTDIR\Deepwater.exe"
    ${If} ${FileExists} "$INSTDIR\Deepwater.exe"
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(MSG_CLOSE_FIRST)" /SD IDCANCEL IDRETRY tryAgain
      SetErrorLevel 5
      Abort
    ${EndIf}
  ${EndIf}
  SetOverwrite on
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  ${If} $NoReg != 1
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "${APPNAME}"
    WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\Deepwater.exe"
    WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" $0
    CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\Deepwater.exe" "" "$INSTDIR\Deepwater.exe" 0
  ${EndIf}
SectionEnd

Function .onInstSuccess
  ; a silent run (the update inside Deepwater, or a test) starts nothing: the caller does
  ${IfNot} ${Silent}
    ${If} $NoReg != 1
      Exec '"$INSTDIR\Deepwater.exe"'
    ${EndIf}
  ${EndIf}
FunctionEnd

Function un.onInit
  ; never empty a folder that is not Deepwater's own
  ${If} $INSTDIR == $PROFILE
  ${OrIf} $INSTDIR == $LOCALAPPDATA
  ${OrIf} $INSTDIR == $APPDATA
  ${OrIf} $INSTDIR == $DOCUMENTS
  ${OrIf} $INSTDIR == $DESKTOP
  ${OrIf} $INSTDIR == $PROGRAMFILES
  ${OrIf} $INSTDIR == $PROGRAMFILES64
  ${OrIf} $INSTDIR == $WINDIR
  ${OrIf} $INSTDIR == "$PROGRAMFILES64\Deepwater"
  ${OrIf} $INSTDIR == "$PROGRAMFILES\Deepwater"
    SetErrorLevel 6
    Abort
  ${EndIf}
FunctionEnd

Section "Uninstall"
  untryAgain:
  ${If} ${FileExists} "$INSTDIR\Deepwater.exe"
    Delete "$INSTDIR\Deepwater.exe"
    ${If} ${FileExists} "$INSTDIR\Deepwater.exe"
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(MSG_CLOSE_FIRST)" /SD IDCANCEL IDRETRY untryAgain
      SetErrorLevel 5
      Abort
    ${EndIf}
  ${EndIf}
  ; the Start menu entry and the "installed apps" line belong to the install they were written for: a copy
  ; installed with /NOREG (a test) must not remove another install's
  ReadRegStr $0 HKCU "${UNINSTKEY}" "InstallLocation"
  ${If} $0 == $INSTDIR
    Delete "$SMPROGRAMS\${APPNAME}.lnk"
    DeleteRegKey HKCU "${UNINSTKEY}"
  ${EndIf}
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"
SectionEnd
