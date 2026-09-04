@echo off
cls
echo ===========================================================
echo   Xash - HL c1a0b - ascensor giratorio (traza de pushers)
echo ===========================================================
echo.
echo   1. Vete al ascensor que gira al bajar (tienes sv_cheats 1,
echo      puedes usar noclip para llegar rapido).
echo   2. Llamalo y ponte donde se atasca, hasta que te quite vida.
echo   3. Dejalo subir, tocar techo y hacer lo que haga.
echo   4. Salte. Leere ascensor_xash.log.
echo.
echo   A camara lenta se ve mucho mejor:  sys_timescale 0.1
echo   Para apagar la traza:              sv_trace_pushers ""
echo.
echo  Pulsa una tecla.
pause >nul
cd /d "%~dp0"
xash3d.exe -game valve -console -fullscreen -log ascensor_xash.log -dev 2 +sv_cheats 1 +sv_trace_pushers func_platrot +map c1a0b
