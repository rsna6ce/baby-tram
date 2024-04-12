echo off
chcp 65001
set inputFile=%1
set outputFile="%inputFile%.h"

(for /f "usebackq delims=" %%a in (%inputFile%) do (
    set "line=%%a"
    echo "%%a\n"
)) > %outputFile%
