@echo off
rem Compile dist\SoundZ.exe avec le compilateur portable de tools\llvm-mingw.
setlocal
cd /d "%~dp0"
set BIN=tools\llvm-mingw\bin
if not exist "%BIN%\clang++.exe" (
  echo Compilateur introuvable : %BIN%\clang++.exe
  exit /b 1
)
if not exist build mkdir build
if not exist dist mkdir dist

set COMMON=-O2 -DNDEBUG -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -Ithird_party -Wall -Wno-unused-function
"%BIN%\clang.exe" %COMMON% -c src\miniaudio_impl.c -o build\miniaudio.o || exit /b 1
for %%f in (engine default_output main selftest voice_engine voice_page sound_page sound_image sound_claude process_loopback theme shell_integration overlay remote myinstants) do (
  "%BIN%\clang++.exe" %COMMON% -std=c++17 -ffast-math -c src\%%f.cpp -o build\%%f.o || exit /b 1
)
"%BIN%\clang++.exe" %COMMON% -std=c++17 -c third_party\qrcodegen\qrcodegen.cpp -o build\qrcodegen.o || exit /b 1
"%BIN%\windres.exe" -i src\app.rc -o build\app.o --include-dir src || exit /b 1
"%BIN%\clang++.exe" -municode -mwindows -static -s build\*.o -o dist\SoundZ.exe -lole32 -lcomctl32 -luuid -lshell32 -lcomdlg32 -lmsimg32 -lwindowscodecs -ld2d1 -ldwrite -lshlwapi -lgdiplus -ldwmapi -luxtheme -lversion -lws2_32 -liphlpapi -lbcrypt -lwinhttp || exit /b 1
echo OK : dist\SoundZ.exe
rem Mise à jour de la version installée (C:\Outils\SoundZ), si SoundZ n'est pas ouvert.
if exist "C:\Outils\SoundZ\" (
  copy /y dist\SoundZ.exe "C:\Outils\SoundZ\SoundZ.exe" >nul 2>&1 && echo Installe : C:\Outils\SoundZ\SoundZ.exe || echo SoundZ est ouvert : ferme-le pour mettre a jour C:\Outils\SoundZ
)
