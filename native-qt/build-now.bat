@echo off
echo Starting build...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\Steve\code\video-mixer\native-qt\build
echo Running ninja...
ninja video-mixer
echo Build complete. Exit code: %ERRORLEVEL%
echo.
dir bin\video-mixer.exe

