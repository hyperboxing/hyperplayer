@echo off
cls
echo Deleting exe
taskkill /im hyperplayer_v1.1.exe /f >nul 2>&1
ping 127.0.0.1 -n 2 >nul
cd /d C:\winprog\C\projects\modplayer >nul 2>&1

echo Compiling resources
if exist resources.o del /f /q resources.o >nul 2>&1
C:\winprog\C\bin\windres.exe resources.rc -O coff -o resources.o
if errorlevel 1 goto :fail

echo Compiling exe
C:\winprog\C\bin\gcc.exe -B C:\winprog\C\bin\ -std=c11 -O2 -Wall -Wextra -municode -mwindows main.c app.c ui.c directory_listing_win32.c action_buttons.c player.c pattern_view.c sample_list.c sample_list_usage_trigger.c sample_display.c spectrumanalyzer.c vumeter.c quadrascope.c tunnelvisualizer.c mousecursor.c urls.c pt2play\pt2play.c pt2play\pt2_replayer.c pt2play\pt2_paula.c pt2play\pt2_blep.c pt2play\pt2_rcfilters.c pt2play\pt2_downsample2x.c pt2play\pt2_tables_replay.c resources.o -o hyperplayer_v1.1.exe -lgdi32 -lmsimg32 -lole32 -luuid -lwindowscodecs -lwinmm -lshell32 -lm
if errorlevel 1 goto :fail

if /I "%~1"=="--no-run" goto :success

echo Starting exe
start "" "hyperplayer_v1.1.exe"
pause
exit

:success
echo Build succeeded.
exit /b 0

:fail
echo.
echo Build failed.
pause
exit /b 1
