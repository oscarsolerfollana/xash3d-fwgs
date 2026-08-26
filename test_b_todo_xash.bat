@echo off
cls
echo ===========================================================
echo   PRUEBA B  -  todo Xash3D
echo ===========================================================
echo.
echo  Se abre UNA sola ventana. Haz EXACTAMENTE lo mismo que en A:
echo    - elige equipo
echo    - la MISMA ametralladora que en la prueba A
echo    - ponte delante de una pared, QUIETO
echo    - RAFAGA 1: aguanta el gatillo ~30 tiros SIN bipode
echo    - suelta, espera 2-3 segundos
echo    - despliega el bipode (tecla V)
echo    - RAFAGA 2: otros ~30 tiros CON bipode
echo    - no recargues, el M60 lleva 100 balas
echo    - sal con la consola:  quit
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game firearms -dev 2 -log -windowed -width 800 -height 600 +maxplayers 16 +dbg_weapondata 2 +map ps_island
copy /y engine.log log_xash.log >nul
cls
echo  LISTO. Guardado en  log_xash.log
echo  Avisa a Claude: ya estan los dos logs.
pause
