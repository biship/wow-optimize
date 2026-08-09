@echo off
echo ============================================
echo   wow_optimize build script
echo   Builds: wow_optimize.dll + version.dll
echo   Requires: Visual Studio 2026 + CMake
echo ============================================
echo.

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake not found.
    echo Install Visual Studio 2026 with "Desktop development with C++"
    pause
    exit /b 1
)

if not exist build mkdir build
cd build

echo [1/3] Configuring (32-bit)...
cmake -G "Visual Studio 18 2026" -A Win32 .. 2>&1
if errorlevel 1 (
    rd /s /q CMakeFiles
    del /q CMakeCache.txt
    echo Trying Visual Studio 2026...
    cmake -G "Visual Studio 18 2026" -A Win32 .. 2>&1
    if errorlevel 1 (
        echo ERROR: CMake configure failed.
        cd ..
        exit /b 1
    )
)

echo.
echo [2/3] Building Release...
cmake --build . --config Release 2>&1
if errorlevel 1 (
    echo ERROR: Build failed.
    cd ..
    exit /b 1
)

echo.
echo [3/3] Done!
echo.
echo Output files:
echo   build\Release\wow_optimize.dll  - optimization DLL
echo   build\Release\version.dll       - auto-loader proxy
echo.
echo OPTION A (recommended):
echo   Copy BOTH files to your WoW folder. No injector needed.
echo.
echo OPTION B (manual):
echo   Copy wow_optimize.dll + use inject.bat or any DLL injector.
echo.
cd ..

echo.
echo [4/4] Compiling Launcher...
C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe /noconfig /nowarn:1701,1702 /nostdlib+ /errorreport:prompt /warn:4 /define:TRACE /debug:full /pdb:build\Release\wow_optimize_launcher.pdb /reference:C:\Windows\Microsoft.NET\Framework\v4.0.30319\mscorlib.dll /reference:C:\Windows\Microsoft.NET\Framework\v4.0.30319\System.Core.dll /reference:C:\Windows\Microsoft.NET\Framework\v4.0.30319\System.dll /reference:C:\Windows\Microsoft.NET\Framework\v4.0.30319\System.Drawing.dll /reference:C:\Windows\Microsoft.NET\Framework\v4.0.30319\System.Windows.Forms.dll /resource:src\launcher\wotlk_background.jpg,wotlk_background.jpg /target:winexe /out:build\Release\wow_optimize_launcher.exe src\launcher\Launcher.cs
if errorlevel 1 (
    echo ERROR: Launcher compilation failed.
    exit /b 1
)

copy /y src\launcher\wotlk_background.jpg build\Release\wotlk_background.jpg >nul

echo.
echo Output files:
echo   build\Release\wow_optimize.dll  - optimization DLL
echo   build\Release\version.dll       - auto-loader proxy
echo   build\Release\wow_optimize.pdb - native symbols for wow_optimize.dll
echo   build\Release\version.pdb       - native symbols for version.dll
echo   build\Release\wow_optimize_launcher.pdb - launcher symbols
echo   build\Release\wow_optimize_launcher.exe - modular launcher dashboard
echo   build\Release\wotlk_background.jpg - launcher background asset
echo.
echo OPTION A (recommended):
echo   Copy ALL files to your WoW folder. Run wow_optimize_launcher.exe.
echo.
