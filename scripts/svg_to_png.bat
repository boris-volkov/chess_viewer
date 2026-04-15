@echo off
REM SVG to PNG Converter for Chess Pieces (Windows Batch Version)
REM
REM This batch script converts SVG chess piece files to PNG format.
REM It requires ImageMagick to be installed and available in PATH.
REM
REM Usage: svg_to_png.bat [size]
REM
REM Arguments:
REM     size: Optional PNG size in pixels (default: 64)
REM
REM The script will:
REM 1. Find all SVG files in the pieces\ directory
REM 2. Convert them to PNG format with the specified size
REM 3. Preserve the original naming convention

setlocal enabledelayedexpansion

REM Default size
set SIZE=64

REM Parse command line arguments
if "%~1" neq "" (
    set SIZE=%~1
)

echo SVG to PNG Converter for Chess Pieces
echo ======================================
echo.
echo Converting SVG files to PNG with size %SIZE%x%SIZE%...
echo.

REM Check if we're in the right directory
if not exist "pieces\" (
    echo ERROR: pieces\ directory not found!
    echo Please run this script from the chess_viewer root directory.
    pause
    exit /b 1
)

REM Check if ImageMagick is available
convert -version >nul 2>&1
if errorlevel 1 (
    echo ERROR: ImageMagick not found!
    echo Please install ImageMagick and make sure 'convert' is in your PATH.
    echo Download from: https://imagemagick.org/script/download.php
    pause
    exit /b 1
)

REM Find and convert SVG files
set CONVERTED=0
set TOTAL=0

for %%f in (pieces\*.svg) do (
    set /a TOTAL+=1
    set "SVG_FILE=%%f"
    set "PNG_FILE=%%~nf.png"

    echo Converting: %%~nf.svg -^> %%~nf.png
    convert "%%f" -resize %SIZE%x%SIZE% "pieces\%%~nf.png"

    if errorlevel 0 (
        echo   SUCCESS
        set /a CONVERTED+=1
    ) else (
        echo   FAILED
    )
)

echo.
if %TOTAL% equ 0 (
    echo No SVG files found in pieces\ directory.
    echo.
    echo Place your SVG chess piece files in the pieces\ directory.
    echo Expected naming convention: Chess_[piece][theme].svg
    echo Examples: Chess_klt.svg, Chess_qdt.svg, Chess_pdt.svg, etc.
) else (
    echo Conversion complete! %CONVERTED%/%TOTAL% files converted successfully.
    echo.
    if %CONVERTED% gtr 0 (
        echo PNG files are ready for use with chess_viewer.
        echo Original SVG files are preserved alongside the PNG files.
        echo You can now run chess_viewer and it will use the new PNG pieces.
    )
)

echo.
pause