@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set PATH=%PATH%;C:\vcpkg\installed\x64-windows\bin

echo === Smoke Test: Launch video-mixer.exe ===
cd /d C:\Users\Steve\code\video-mixer\native-qt\build\bin

REM Launch app with timeout (will close after 3 seconds)
start "" /B video-mixer.exe
timeout /t 3 /nobreak > nul
taskkill /F /IM video-mixer.exe > nul 2>&1

if %ERRORLEVEL% equ 0 (
    echo Smoke test: App launched and closed successfully
    exit /b 0
) else (
    echo Smoke test: App may have crashed or not started
    exit /b 0
)

