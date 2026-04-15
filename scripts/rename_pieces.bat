@echo off
REM Rename chess7_pieces SVG files to match chess_viewer naming convention

cd /d "%~dp0..\pieces\chess7_pieces"

if not exist "wK.svg" (
    echo No files to rename - they may already be renamed
    goto :eof
)

echo Renaming SVG files to chess_viewer format...

move wK.svg Chess_klt.svg
move bK.svg Chess_kdt.svg
move wQ.svg Chess_qlt.svg
move bQ.svg Chess_qdt.svg
move wR.svg Chess_rlt.svg
move bR.svg Chess_rdt.svg
move wB.svg Chess_blt.svg
move bB.svg Chess_bdt.svg
move wN.svg Chess_nlt.svg
move bN.svg Chess_ndt.svg
move wP.svg Chess_plt.svg
move bP.svg Chess_pdt.svg

REM Move renamed files to pieces directory
move Chess_*.svg ..\

echo Renaming complete!
echo.
