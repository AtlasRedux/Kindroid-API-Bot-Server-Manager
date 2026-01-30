@echo off
echo ========================================
echo Kindroid Discord Bot - Build Script
echo ========================================
echo.

REM Find Visual Studio 2022
set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022"

if exist "%VS_PATH%\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=%VS_PATH%\Community\MSBuild\Current\Bin\MSBuild.exe"
) else if exist "%VS_PATH%\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=%VS_PATH%\Professional\MSBuild\Current\Bin\MSBuild.exe"
) else if exist "%VS_PATH%\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=%VS_PATH%\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
) else (
    echo ERROR: Could not find MSBuild.exe
    echo Please make sure Visual Studio 2022 is installed
    pause
    exit /b 1
)

echo Found MSBuild at: %MSBUILD%
echo.

echo Building Release x64...
"%MSBUILD%" KindroidDiscordBot.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Clean,Build

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Build completed successfully!
    echo Output: x64\Release\KindroidDiscordBot.exe
    echo ========================================
) else (
    echo.
    echo ========================================
    echo Build FAILED! Check errors above.
    echo ========================================
)

echo.
pause
