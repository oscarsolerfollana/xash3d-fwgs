@echo off
cls
echo ===========================================================
echo   PIEZA 3a: Xash SERVIDOR dedicado CS 1.6 (control todo-Xash)
echo ===========================================================
echo.
echo  Servidor Xash de CS 1.6 al que se conectara el cliente Xash
echo  instrumentado (pieza 3b). Es el CONTROL frente a la pieza 2
echo  (mismo cliente, pero contra el servidor de Steam/GoldSrc).
echo.
echo  Arranca ESTE primero, deja la consola abierta, y luego lanza
echo  test_cs16_xash_vs_xash.bat. No cierres esta ventana.
echo.
echo  Si el modo dedicado no arranca CS bien, usa como servidor el
echo  propio test_cs16_xash_host.bat (listenserver) en su lugar.
echo.
echo  Pulsa una tecla.
pause >nul
cd /d "%~dp0"
xash3d.exe -dedicated -game cstrike +sv_lan 1 +sv_cheats 1 +mp_autoteambalance 0 +mp_limitteams 0 +mp_freezetime 0 +mp_roundtime 9 +mp_startmoney 16000 +maxplayers 2 +map aim_ak-colt
