; Video Mixer Native Qt Installer Script
; NSIS 3.x

!include "MUI2.nsh"

; General
Name "Video Mixer"
!ifndef VERSION
!define VERSION "0.1.0"
!endif
!ifndef BUILD_BIN_DIR
!define BUILD_BIN_DIR "build\bin"
!endif
!ifndef OUTFILE
!define OUTFILE "dist\video-mixer-setup.exe"
!endif
!define FIREWALL_RULE_UDP "Video Mixer WebRTC UDP"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Video Mixer"
InstallDirRegKey HKLM "Software\VideoMixer" "InstallDir"
RequestExecutionLevel admin

; Version Info
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Video Mixer"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "Video Mixer - Windows Video Mixer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026"

; Interface Settings
!define MUI_ABORTWARNING
!define MUI_ICON "resources\vdoninja.ico"
!define MUI_UNICON "resources\vdoninja.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

Function EnsureVideoMixerClosed
    ; Avoid "Error opening file for writing" when Qt DLLs are still locked.
    nsExec::ExecToLog 'taskkill /F /T /IM video-mixer.exe'
    Pop $0
FunctionEnd

Function un.EnsureVideoMixerClosed
    ; Uninstaller needs its own function namespace in NSIS.
    nsExec::ExecToLog 'taskkill /F /T /IM video-mixer.exe'
    Pop $0
FunctionEnd

Function RemoveExistingInstall
    IfFileExists "$INSTDIR\uninstall.exe" 0 done
    DetailPrint "Existing install detected; running previous uninstaller..."
    ExecWait '"$INSTDIR\uninstall.exe" /S _?=$INSTDIR'
done:
FunctionEnd

Function AddFirewallRules
    DetailPrint "Adding Windows Firewall rule for Video Mixer WebRTC..."
    ; Replace stale rules so upgrades keep the installed executable path current.
    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${FIREWALL_RULE_UDP}"'
    Pop $0
    nsExec::ExecToLog 'netsh advfirewall firewall add rule name="${FIREWALL_RULE_UDP}" dir=in action=allow program="$INSTDIR\video-mixer.exe" protocol=UDP enable=yes profile=any'
    Pop $0
    StrCmp $0 "0" firewall_added
    DetailPrint "WARNING: Windows Firewall rule could not be added (netsh result: $0)."
    MessageBox MB_ICONEXCLAMATION|MB_OK "Video Mixer was copied, but Windows Firewall could not be configured (result: $0). Direct viewer connections may be blocked. Allow inbound UDP for $INSTDIR\video-mixer.exe in Windows Firewall, or ask your administrator if policy manages these rules." /SD IDOK
    Goto firewall_done
firewall_added:
    DetailPrint "Windows Firewall rule added for $INSTDIR\video-mixer.exe."
firewall_done:
FunctionEnd

Function un.RemoveFirewallRules
    DetailPrint "Removing Windows Firewall rule for Video Mixer WebRTC..."
    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${FIREWALL_RULE_UDP}" program="$INSTDIR\video-mixer.exe"'
    Pop $0
    StrCmp $0 "0" firewall_removed
    DetailPrint "Windows Firewall rule was absent or could not be removed (netsh result: $0)."
firewall_removed:
FunctionEnd

; Installer Section
Section "Install"
    Call EnsureVideoMixerClosed
    Call RemoveExistingInstall
    Call EnsureVideoMixerClosed
    Sleep 600

    SetOutPath "$INSTDIR"

    ; Best effort cleanup of stale/legacy file names before copy.
    Delete /REBOOTOK "$INSTDIR\video-mixer.exe"
    Delete /REBOOTOK "$INSTDIR\game-catpure.exe"
    Delete /REBOOTOK "$INSTDIR\game-catpure.ede"

    ; Main executable
    ClearErrors
    File "${BUILD_BIN_DIR}\video-mixer.exe"
    IfErrors 0 +3
    MessageBox MB_ICONSTOP|MB_OK "Video Mixer is still running or files are locked. Close the app and retry setup."
    Abort
    File /nonfatal "${BUILD_BIN_DIR}\*.dll"
    File /nonfatal "${BUILD_BIN_DIR}\vdoninja.ico"
    File /nonfatal "${BUILD_BIN_DIR}\RELEASE-NOTES.txt"

    ; Bundled FFmpeg - isolated under $INSTDIR\ffmpeg, never added to PATH.
    SetOutPath "$INSTDIR\ffmpeg"
    File /nonfatal /r "${BUILD_BIN_DIR}\ffmpeg\*.*"
    SetOutPath "$INSTDIR"

    Call AddFirewallRules

    ; Qt plugins - platforms
    SetOutPath "$INSTDIR\platforms"
    File "${BUILD_BIN_DIR}\platforms\qwindows.dll"

    ; Qt plugins - styles
    SetOutPath "$INSTDIR\styles"
    File /nonfatal "${BUILD_BIN_DIR}\styles\qmodernwindowsstyle.dll"

    ; Qt plugins - generic input
    SetOutPath "$INSTDIR\generic"
    File /nonfatal "${BUILD_BIN_DIR}\generic\*.dll"

    ; Qt plugins - image formats
    SetOutPath "$INSTDIR\imageformats"
    File /nonfatal "${BUILD_BIN_DIR}\imageformats\*.dll"

    ; Qt plugins - network information
    SetOutPath "$INSTDIR\networkinformation"
    File /nonfatal "${BUILD_BIN_DIR}\networkinformation\*.dll"

    ; Qt plugins - TLS
    SetOutPath "$INSTDIR\tls"
    File /nonfatal "${BUILD_BIN_DIR}\tls\*.dll"

    ; Create shortcuts
    SetOutPath "$INSTDIR"
    CreateDirectory "$SMPROGRAMS\Video Mixer"
    CreateShortcut "$SMPROGRAMS\Video Mixer\Video Mixer.lnk" "$INSTDIR\video-mixer.exe" "" "$INSTDIR\video-mixer.exe"
    CreateShortcut "$SMPROGRAMS\Video Mixer\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    CreateShortcut "$DESKTOP\Video Mixer.lnk" "$INSTDIR\video-mixer.exe" "" "$INSTDIR\video-mixer.exe"

    ; Registry
    WriteRegStr HKLM "Software\VideoMixer" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "DisplayName" "Video Mixer"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "DisplayIcon" "$INSTDIR\video-mixer.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "Publisher" "VDO.Ninja"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "NoRepair" 1

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; Uninstaller Section
Section "Uninstall"
    Call un.EnsureVideoMixerClosed
    Call un.RemoveFirewallRules

    ; Remove files
    Delete "$INSTDIR\*.*"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\generic"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\networkinformation"
    RMDir /r "$INSTDIR\tls"
    RMDir /r "$INSTDIR\ffmpeg"
    Delete "$INSTDIR\uninstall.exe"

    ; Remove directories
    RMDir "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\Video Mixer\Video Mixer.lnk"
    Delete "$SMPROGRAMS\Video Mixer\Uninstall.lnk"
    Delete "$DESKTOP\Video Mixer.lnk"
    RMDir "$SMPROGRAMS\Video Mixer"

    ; Remove registry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer"
    DeleteRegKey HKLM "Software\VideoMixer"
SectionEnd

