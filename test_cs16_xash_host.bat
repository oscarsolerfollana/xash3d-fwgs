@echo off
cls
echo ===========================================================
echo   PIEZA 1: Xash - CS 1.6 (ANFITRION) - medir cono y cadencia
echo ===========================================================
echo.
echo  Xash es el servidor: ESTE es el caso que tocaba el bug del
echo  bipode (el anfitrion del listenserver). Yo leo el log.
echo.
echo   1. Unete a un equipo (mapa aim: ya tienes AK y colt).
echo   2. Saca el AK y dispara el protocolo de rafagas:
echo        - cargador entero QUIETO DE PIE
echo        - pausa, cargador AGACHADO
echo        - pausa, cargador CORRIENDO (manten A o D al disparar)
echo        - pausa, unos tiros SALTANDO
echo   3. Salte. Leere cs16_host.log.
echo.
echo  Pulsa una tecla.
pause >nul
cd /d "%~dp0"
xash3d.exe -game cstrike -console -fullscreen +sv_lan 1 +sv_cheats 1 +mp_autoteambalance 0 +mp_limitteams 0 +mp_freezetime 0 +mp_roundtime 9 +mp_startmoney 16000 +maxplayers 2 +dbg_weapondata 2 -log cs16_host.log +map aim_ak-colt +wallhack 0 +aimbot 0
