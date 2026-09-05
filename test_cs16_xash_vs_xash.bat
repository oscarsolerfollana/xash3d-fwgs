@echo off
cls
echo ===========================================================
echo   PIEZA 3b: Xash CLIENTE  -^>  servidor Xash CS 1.6 (control)
echo ===========================================================
echo.
echo  Mismo cliente instrumentado que la pieza 2, pero conectado a
echo  un servidor XASH (no Steam). Comparar cs16_vs_xash.log con
echo  cs16_vs_steam.log aisla lo que aporta el motor del servidor.
echo.
echo  ANTES: arranca test_cs16_xash_server.bat (o el host) y espera
echo  a que cargue aim_ak-colt. Luego pulsa tecla aqui.
echo.
echo  Dentro: unete a un equipo y dispara el MISMO protocolo de
echo  rafagas (pie/agachado/corriendo/saltando). Leere el log.
echo.
echo  Pulsa una tecla cuando el servidor Xash este listo.
pause >nul
cd /d "%~dp0"
xash3d.exe -game cstrike -console -fullscreen +dbg_weapondata 2 -log cs16_vs_xash.log +connect 127.0.0.1:27015 +wallhack 0 +aimbot 0
