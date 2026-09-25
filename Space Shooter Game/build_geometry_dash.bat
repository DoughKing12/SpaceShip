@echo off
title Geometry Dash Build Script
echo ==============================
echo   GEOMETRY DASH - Build
echo ==============================
echo.

where g++ >nul 2>&1
if %errorlevel% neq 0 if exist "C:\msys64\mingw64\bin\g++.exe" set "PATH=C:\msys64\mingw64\bin;%PATH%"

where g++ >nul 2>&1
if %errorlevel% equ 0 (
    echo Found MinGW g++. Building with g++...
    echo.
    g++ -O2 -static -o geometry_dash.exe geometry_dash.cpp -lgdi32 -lwinmm
    if %errorlevel% equ 0 (
        echo.
        echo BUILD SUCCESSFUL! Run: geometry_dash.exe
        pause
        exit /b 0
    )
)

where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo Found MSVC compiler. Building with cl.exe...
    echo.
    cl /O2 /EHsc /utf-8 /MT geometry_dash.cpp /Fe:geometry_dash.exe /link user32.lib gdi32.lib winmm.lib
    if %errorlevel% equ 0 (
        echo.
        echo BUILD SUCCESSFUL! Run: geometry_dash.exe
        pause
        exit /b 0
    )
)

echo.
echo ERROR: No C++ compiler found!
echo.
echo Install MinGW via https://www.msys2.org/ then run:
echo   pacman -S mingw-w64-x86_64-gcc
echo.
pause
