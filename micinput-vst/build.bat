@echo off
setlocal EnableDelayedExpansion
title MicInput VST - Build
cd /d "%~dp0"

echo.
echo ============================================================
echo  MicInput VST v0.1 - Build
echo ============================================================
echo.

:: ── Find vswhere.exe ───────────────────────────────────────────────────────
:: IMPORTANT: Do NOT use !var! with paths containing parentheses.
:: Use %var% only for these assignments.

set VS86=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe
set VS64=C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe

set VSWHERE=
if exist "%VS86%" set VSWHERE=%VS86%
if exist "%VS64%" if "%VSWHERE%"=="" set VSWHERE=%VS64%

if "%VSWHERE%"=="" (
    echo [ERROR] vswhere.exe not found. Install Visual Studio 2022.
    pause & exit /b 1
)
echo [OK] Found vswhere.

:: ── Write vswhere output to a temp file to avoid backtick + space issues ───
set TMPOUT=%TEMP%\micinput_vcvars.txt
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "VC\Auxiliary\Build\vcvars64.bat" > "%TMPOUT%" 2>nul

set VCVARS=
for /f "usebackq tokens=*" %%P in ("%TMPOUT%") do (
    if "!VCVARS!"=="" set "VCVARS=%%P"
)
del "%TMPOUT%" >nul 2>&1

if "!VCVARS!"=="" (
    echo [ERROR] MSVC C++ tools not found.
    echo Open Visual Studio Installer and add workload:
    echo   "Desktop development with C++"
    pause & exit /b 1
)

echo [OK] VCVARS: !VCVARS!
call "!VCVARS!" >nul 2>&1
echo [OK] MSVC x64 environment ready.

:: ── Check CMake ────────────────────────────────────────────────────────────
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake not found in PATH.
    echo Download from https://cmake.org/download/
    echo Tick "Add CMake to system PATH" during install.
    pause & exit /b 1
)
for /f "tokens=3" %%V in ('cmake --version 2^>nul ^| findstr "cmake version"') do set CMAKEVER=%%V
echo [OK] CMake !CMAKEVER!

:: ── Check Git ──────────────────────────────────────────────────────────────
where git >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Git not found. Download from https://git-scm.com/
    pause & exit /b 1
)
echo [OK] Git found.
echo.

:: ── Configure ──────────────────────────────────────────────────────────────
echo [1/2] Configuring...
echo       First run downloads JUCE ~500MB - takes a few minutes.
echo.

if not exist build mkdir build

cmake -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configure failed.
    echo Common fixes:
    echo  - Check internet connection (JUCE downloads on first run)
    echo  - If VS2019 installed, change "17 2022" to "16 2019" in build.bat line above
    pause & exit /b 1
)
echo [OK] Configured.
echo.

:: ── Build ──────────────────────────────────────────────────────────────────
echo [2/2] Building (Release)...

cmake --build build --config Release --target MicInputVST_VST3 --parallel
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. Read errors above.
    pause & exit /b 1
)

echo.
echo ============================================================
echo  BUILD SUCCESSFUL
echo ============================================================
echo.
echo Next steps:
echo   1. Run install.bat as Administrator
echo   2. Bitwig: Settings - Plugins - Rescan
echo   3. Load MicInput on an Audio Track
echo   4. Select mic, arm track, press Record
echo.
pause
