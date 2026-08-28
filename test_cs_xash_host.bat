@echo off
cls
echo ===========================================================
echo   Xash - CS 1.3 (anfitrion) - medir cono y cadencia (dbg)
echo ===========================================================
echo.
echo  Xash es el servidor (el caso del bipode). Yo leo el log.
echo.
echo   1. Unete a un equipo (mapa aim: ya tienes AK y colt).
echo   2. Saca el AK y dispara una RAFAGA LARGA a donde sea
echo      (da igual la direccion; solo necesito los datos).
echo   3. Salte. Leere cs_xash.log.
echo.
echo  Con el primer disparo del AK confirmo que la instrumentacion
echo  captura el cono/dispersion. Si va, seguimos con el resto.
echo.
echo  Pulsa una tecla.
pause >nul
cd /d "%~dp0"
xash3d.exe -game cstrike -console -window -width 1024 -height 768 +sv_lan 1 +sv_cheats 1 +mp_autoteambalance 0 +mp_limitteams 0 +maxplayers 2 +dbg_weapondata 2 -log cs_xash.log +map aim_ak-colt
