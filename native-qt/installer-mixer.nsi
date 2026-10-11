; Per-user setup. WebView2 Evergreen is installed and updated independently.
Unicode True
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!ifndef VERSION
!define VERSION "0.3.0"
!endif
!ifndef BUILD_BIN_DIR
!define BUILD_BIN_DIR "dist\video-mixer-portable"
!endif
!ifndef OUTFILE
!define OUTFILE "dist\video-mixer-0.3.0-setup.exe"
!endif
Name "Video Mixer"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Video Mixer"
InstallDirRegKey HKCU "Software\VideoMixer" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Video Mixer"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "Video Mixer setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 VDO.Ninja contributors"
!define MUI_ICON "resources\vdoninja.ico"
!define MUI_UNICON "resources\vdoninja.ico"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\video-mixer.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_LINK "Install Microsoft Edge WebView2 Runtime if browser sources need it"
!define MUI_FINISHPAGE_LINK_LOCATION "https://developer.microsoft.com/microsoft-edge/webview2/"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Video Mixer requires 64-bit Windows."
        Abort
    ${EndIf}
    SetRegView 64
    SetShellVarContext current
FunctionEnd
Function un.onInit
    SetRegView 64
    SetShellVarContext current
FunctionEnd
!macro RequireClosed
    FindWindow $0 "" "Video Mixer - Powered by VDO.Ninja"
    ${If} $0 != 0
        MessageBox MB_ICONEXCLAMATION|MB_OK "Close Video Mixer before continuing. Stop active recordings in the app first."
        Abort
    ${EndIf}
!macroend
Section "Video Mixer"
    !insertmacro RequireClosed
    SetOutPath "$INSTDIR"
    File "${BUILD_BIN_DIR}\video-mixer.exe"
    File "${BUILD_BIN_DIR}\*.dll"
    File "${BUILD_BIN_DIR}\LICENSE"
    File "${BUILD_BIN_DIR}\WebView2-LICENSE.txt"
    File "${BUILD_BIN_DIR}\README.md"
    File /r "${BUILD_BIN_DIR}\docs"
    File /nonfatal "${BUILD_BIN_DIR}\vdoninja.ico"
    File /r "${BUILD_BIN_DIR}\platforms"
    File /nonfatal /r "${BUILD_BIN_DIR}\styles"
    File /nonfatal /r "${BUILD_BIN_DIR}\imageformats"
    File /nonfatal /r "${BUILD_BIN_DIR}\networkinformation"
    File /nonfatal /r "${BUILD_BIN_DIR}\tls"
    File /nonfatal /r "${BUILD_BIN_DIR}\generic"
    File /r "${BUILD_BIN_DIR}\ffmpeg"
    CreateDirectory "$SMPROGRAMS\Video Mixer"
    CreateShortcut "$SMPROGRAMS\Video Mixer\Video Mixer.lnk" "$INSTDIR\video-mixer.exe"
    CreateShortcut "$SMPROGRAMS\Video Mixer\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    WriteRegStr HKCU "Software\VideoMixer" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "DisplayName" "Video Mixer"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "QuietUninstallString" '$\"$INSTDIR\uninstall.exe$\" /S'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "DisplayIcon" "$INSTDIR\video-mixer.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "Publisher" "VDO.Ninja"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer" "NoRepair" 1
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd
Section "Uninstall"
    !insertmacro RequireClosed
    Delete "$INSTDIR\video-mixer.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\WebView2-LICENSE.txt"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\vdoninja.ico"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\networkinformation"
    RMDir /r "$INSTDIR\tls"
    RMDir /r "$INSTDIR\generic"
    RMDir /r "$INSTDIR\ffmpeg"
    RMDir /r "$INSTDIR\docs"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\Video Mixer\Video Mixer.lnk"
    Delete "$SMPROGRAMS\Video Mixer\Uninstall.lnk"
    RMDir "$SMPROGRAMS\Video Mixer"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoMixer"
    DeleteRegKey HKCU "Software\VideoMixer"
    ; Keep shows, browser profiles and recordings in the user's data folders.
SectionEnd
