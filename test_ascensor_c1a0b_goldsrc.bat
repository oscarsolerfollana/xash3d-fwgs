@echo off
cls
cd /d "%~dp0"
set "HLDIR=C:\Sierra\Half-Life"
echo ===========================================================
echo   GoldSrc (WON) - ascensor c1a0b
echo ===========================================================
echo.
echo   1. GoldSrc tal cual        (su hl.dll de WON, junio 2002)
echo   2. GoldSrc + hl.dll nueva  (prueba espejo: la del proyecto)
echo.
echo   La 2 mete la DLL moderna en Sierra y la restaura al salir.
echo   Si el motor de 2002 no traga con ella, cerrara solo: no pasa
echo   nada, la DLL original vuelve a su sitio igual.
echo.
set /p OP="Elige (1/2): "
echo.
echo   Bloquea el ascensor NADA MAS arrancar la bajada, en el primer
echo   palmo del recorrido. Camara lenta: host_timescale 0.1
echo.
echo  Pulsa una tecla.
pause >nul

if not "%OP%"=="2" goto run
copy /y "%HLDIR%\valve\dlls\hl.dll" "%HLDIR%\valve\dlls\hl.dll.orig" >nul
copy /y "%~dp0valve\dlls\hl.dll" "%HLDIR%\valve\dlls\hl.dll" >nul
echo   DLL moderna instalada en Sierra.

:run
if exist "%HLDIR%\valve\qconsole.log" del "%HLDIR%\valve\qconsole.log" >nul
"%HLDIR%\hl.exe" -game valve -console -condebug -full +developer 2 +sv_cheats 1 +map c1a0b

if not "%OP%"=="2" goto log
copy /y "%HLDIR%\valve\dlls\hl.dll.orig" "%HLDIR%\valve\dlls\hl.dll" >nul
del "%HLDIR%\valve\dlls\hl.dll.orig" >nul
echo   DLL de WON restaurada.

:log
if "%OP%"=="2" (set "DEST=ascensor_goldsrc_dllnueva.log") else (set "DEST=ascensor_goldsrc.log")
if exist "%HLDIR%\valve\qconsole.log" copy /y "%HLDIR%\valve\qconsole.log" "%~dp0%DEST%" >nul
echo.
echo   Log copiado a %DEST%
pause
