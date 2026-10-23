@echo off
rem Compila Stackly + installer con Visual Studio 2019/2022 (workload "Sviluppo di applicazioni desktop con C++").
rem Risultato in dist\: Stackly.exe, StacklySetup.exe, Stackly-<versione>-portable.zip
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VSDIR=%%i"
    if not defined VSDIR (
        echo Visual Studio non trovato. Apri il "Developer Command Prompt for VS" e rilancia.
        exit /b 1
    )
    call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
)

for /f "tokens=3 delims= " %%v in ('findstr /c:"STACKLY_VERSION_STR" src\resource.h') do set "VER=%%~v"

if not exist build mkdir build
if not exist dist mkdir dist
if not exist installer\payload mkdir installer\payload

set CFLAGS=/nologo /O2 /MT /EHsc /std:c++17 /utf-8 /W4 /permissive- /DUNICODE /D_UNICODE

rem --- applicazione ---
rc /nologo /i src /fo build\app.res src\app.rc || exit /b 1
cl %CFLAGS% src\main.cpp build\app.res /Fo:build\ /Fe:build\Stackly.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib shell32.lib ole32.lib winhttp.lib windowscodecs.lib ^
   uuid.lib uxtheme.lib dwmapi.lib gdiplus.lib user32.lib gdi32.lib advapi32.lib || exit /b 1

rem --- installer ---
copy /y build\Stackly.exe installer\payload\Stackly.exe >nul
rc /nologo /i installer /fo build\setup.res installer\setup.rc || exit /b 1
cl %CFLAGS% installer\setup.cpp build\setup.res /Fo:build\ /Fe:build\StacklySetup.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib shell32.lib ole32.lib uuid.lib shlwapi.lib msimg32.lib ^
   user32.lib gdi32.lib advapi32.lib || exit /b 1

copy /y build\Stackly.exe dist\ >nul
copy /y build\StacklySetup.exe dist\StacklySetup-%VER%.exe >nul
powershell -NoProfile -Command "Compress-Archive -Force -Path build\Stackly.exe -DestinationPath dist\Stackly-%VER%-portable.zip"

echo.
echo Fatto: dist\ (versione %VER%)
