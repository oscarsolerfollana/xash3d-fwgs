@echo off
cls
echo ===========================================================
echo   PIEZA 2: Xash CLIENTE  -^>  servidor CS 1.6 de STEAM (GoldSrc)
echo ===========================================================
echo.
echo  Este es el metodo que funciono con Firearms (sin gdb): el
echo  cliente Xash instrumentado se conecta a un servidor GoldSrc y
echo  yo leo las lineas [wd:RX] = lo que GoldSrc manda por el cable.
echo.
echo  ANTES de pulsar tecla:
echo   1. Arranca CS 1.6 desde STEAM.
echo   2. Crea partida LAN (New Game / crear servidor) en el mapa
echo      aim_ak-colt, con sv_lan 1 (LAN). Dejalo en el juego.
echo   3. VUELVE aqui y pulsa una tecla: Xash se conecta a 127.0.0.1.
echo.
echo  Dentro de Xash: unete a un equipo y dispara el MISMO protocolo
echo  de rafagas que en la pieza 1 (pie/agachado/corriendo/saltando).
echo  Leere cs16_vs_steam.log.
echo.
echo  Pulsa una tecla cuando el servidor de Steam este creado.
pause >nul
cd /d "%~dp0"
xash3d.exe -rodir C:/Users/Oscar/Proyectos/xash3d -game cstrike -console -fullscreen +dbg_weapondata 2 -log cs16_vs_steam.log +connect 127.0.0.1:27015 +wallhack 0 +aimbot 0
